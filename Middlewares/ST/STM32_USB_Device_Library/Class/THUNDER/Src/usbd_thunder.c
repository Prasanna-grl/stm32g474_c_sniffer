/**
******************************************************************************
* @file    usbd_thunder.c
* @brief   Vendor-specific USB Device Class — Phase 1 Implementation
*
* Interface 0 — CC Sniffer    : EP1 IN
* Interface 1 — Command/Analog: EP2 IN + EP2 OUT
*
* EP2 IN Priority:
*   Command responses take priority over analog packets.
*   If a response is pending when DataIn fires, it is sent first.
*   Analog resumes on the next free EP2 IN slot (max 1ms delay).
*
* EP1 OUT (0x01) is reserved for future use — not opened, not in descriptor.
*
* Phase 2 migration: Replace EP1 IN with double-buffered EP1 IN,
* add 2-bit packet type header to mux CC + Analog on one endpoint.
******************************************************************************
*/

#include "usbd_ctlreq.h"
#include "usbd_thunder.h"
#include <string.h>

/* Private function prototypes -----------------------------------------------*/
static uint8_t USBD_THUNDER_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_THUNDER_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_THUNDER_Setup(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req);
static uint8_t USBD_THUNDER_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_THUNDER_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_THUNDER_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t *USBD_THUNDER_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_THUNDER_GetDeviceQualifierDescriptor(uint16_t *length);

/* Class object --------------------------------------------------------------*/
USBD_ClassTypeDef USBD_THUNDER = {
    USBD_THUNDER_Init,
    USBD_THUNDER_DeInit,
    USBD_THUNDER_Setup,
    NULL, /* EP0_TxSent       */
    USBD_THUNDER_EP0_RxReady,
    USBD_THUNDER_DataIn,
    USBD_THUNDER_DataOut,
    NULL,                      /* SOF              */
    NULL,                      /* IsoINIncomplete  */
    NULL,                      /* IsoOUTIncomplete */
    USBD_THUNDER_GetFSCfgDesc, /* FS config        */
    USBD_THUNDER_GetFSCfgDesc, /* HS config (same) */
    USBD_THUNDER_GetFSCfgDesc, /* Other speed      */
    USBD_THUNDER_GetDeviceQualifierDescriptor,
};

/* Device Qualifier Descriptor -----------------------------------------------*/
__ALIGN_BEGIN static uint8_t
    USBD_THUNDER_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
        USB_LEN_DEV_QUALIFIER_DESC,
        USB_DESC_TYPE_DEVICE_QUALIFIER,
        0x00,
        0x02, /* bcdUSB 2.0        */
        0xFF, /* bDeviceClass      */
        0xFF, /* bDeviceSubClass   */
        0xFF, /* bDeviceProtocol   */
        0x40, /* bMaxPacketSize0   */
        0x01, /* bNumConfigurations*/
        0x00,
};

/* Configuration Descriptor --------------------------------------------------
 *
 *  [Config — 9 bytes]
 *  [IF0: CC Sniffer — 9 bytes]
 *    [EP1 IN  — 7 bytes]  CC Sniffer → Host (libsigrok)
 *  [IF1: Command/Analog — 9 bytes]
 *    [EP2 IN  — 7 bytes]  Analog + CMD response → Host
 *    [EP2 OUT — 7 bytes]  Commands ← Host
 *
 *  Total = 9 + 16 + 23 = 48 bytes  (unchanged from old layout)
 *
 *  EP1 OUT (0x01) is FUTURE — not listed in descriptor, not opened.
 *
 *  Phase 2: IF0 and IF1 will be merged. EP1 IN becomes double-buffered.
 *  bNumInterfaces changes from 2 to 1, wTotalLength from 48 to 32.
 */
__ALIGN_BEGIN static uint8_t
    USBD_THUNDER_CfgFSDesc[USB_THUNDER_CONFIG_DESC_SIZ] __ALIGN_END = {
        /*---------- Configuration Descriptor (9 bytes) ----------*/
        0x09, USB_DESC_TYPE_CONFIGURATION,
        USB_THUNDER_CONFIG_DESC_SIZ, /* wTotalLength LSB = 48 */
        0x00,                        /* wTotalLength MSB      */
        0x02,                        /* bNumInterfaces = 2    */
        0x01,                        /* bConfigurationValue   */
        0x00,                        /* iConfiguration        */
        0x80,                        /* bmAttributes: Bus Powered */
        0xFA,                        /* MaxPower: 500mA       */

        /*---------- Interface 0: CC Sniffer (9 bytes) ----------*/
        0x09, USB_DESC_TYPE_INTERFACE,
        THUNDER_INTERFACE_SNIFFER, /* bInterfaceNumber = 0  */
        0x00,                      /* bAlternateSetting     */
        0x01,                      /* bNumEndpoints = 1     */
        0xFF,                      /* bInterfaceClass: Vendor */
        0xFF,                      /* bInterfaceSubClass    */
        0x00,                      /* bInterfaceProtocol    */
        THUNDER_STR_SNIFFER_IDX,   /* iInterface            */

        /* EP1 IN — CC Sniffer -> Host (7 bytes) */
        0x07, USB_DESC_TYPE_ENDPOINT, THUNDER_EP_SNIFFER_IN, /* 0x81 */
        0x02, /* bmAttributes: Bulk    */
        LOBYTE(THUNDER_SNIFFER_IN_PACKET_SIZE),
        HIBYTE(THUNDER_SNIFFER_IN_PACKET_SIZE), 0x01, /* bInterval */

        /*---------- Interface 1: Command/Analog (9 bytes) ----------*/
        0x09, USB_DESC_TYPE_INTERFACE,
        THUNDER_INTERFACE_CMD_ANALOG, /* bInterfaceNumber = 1  */
        0x00,                         /* bAlternateSetting     */
        0x02,                         /* bNumEndpoints = 2     */
        0xFF,                         /* bInterfaceClass: Vendor */
        0x00,                         /* bInterfaceSubClass    */
        0x00,                         /* bInterfaceProtocol    */
        THUNDER_STR_CMD_ANALOG_IDX,   /* iInterface            */

        /* EP2 IN — Analog + CMD Response -> Host (7 bytes) */
        0x07, USB_DESC_TYPE_ENDPOINT, THUNDER_EP_CMD_ANALOG_IN, /* 0x82 */
        0x02, /* bmAttributes: Bulk    */
        LOBYTE(THUNDER_ANALOG_IN_PACKET_SIZE),
        HIBYTE(THUNDER_ANALOG_IN_PACKET_SIZE),
        0x00, /* bInterval (Bulk: ignore) */

        /* EP2 OUT — Commands <- Host (7 bytes) */
        0x07, USB_DESC_TYPE_ENDPOINT, THUNDER_EP_CMD_OUT, /* 0x02 */
        0x02, /* bmAttributes: Bulk    */
        LOBYTE(THUNDER_CMD_OUT_PACKET_SIZE),
        HIBYTE(THUNDER_CMD_OUT_PACKET_SIZE),
        0x00, /* bInterval (Bulk: ignore) */
};

/* ===========================================================================
 * USBD_THUNDER_Init
 * Opens EP1 IN, EP2 IN, EP2 OUT. Arms EP2 OUT for first command reception.
 * EP1 OUT (0x01) is NOT opened — reserved for future use.
 * ===========================================================================*/
static uint8_t USBD_THUNDER_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx) {
  UNUSED(cfgidx);
  USBD_THUNDER_HandleTypeDef *hthunder;

  hthunder = (USBD_THUNDER_HandleTypeDef *)USBD_malloc(
      sizeof(USBD_THUNDER_HandleTypeDef));
  if (hthunder == NULL) {
    pdev->pClassData = NULL;
    return (uint8_t)USBD_FAIL;
  }

  (void)memset(hthunder, 0, sizeof(USBD_THUNDER_HandleTypeDef));
  pdev->pClassData = (void *)hthunder;

  /* IF0: EP1 IN — CC Sniffer */
  USBD_LL_OpenEP(pdev, THUNDER_EP_SNIFFER_IN, USBD_EP_TYPE_BULK,
                 THUNDER_SNIFFER_IN_PACKET_SIZE);
  pdev->ep_in[THUNDER_EP_SNIFFER_IN & 0xFU].is_used = 1U;

  /* IF1: EP2 IN — Analog + CMD Response */
  USBD_LL_OpenEP(pdev, THUNDER_EP_CMD_ANALOG_IN, USBD_EP_TYPE_BULK,
                 THUNDER_ANALOG_IN_PACKET_SIZE);
  pdev->ep_in[THUNDER_EP_CMD_ANALOG_IN & 0xFU].is_used = 1U;

  /* IF1: EP2 OUT — Commands */
  USBD_LL_OpenEP(pdev, THUNDER_EP_CMD_OUT, USBD_EP_TYPE_BULK,
                 THUNDER_CMD_OUT_PACKET_SIZE);
  pdev->ep_out[THUNDER_EP_CMD_OUT & 0xFU].is_used = 1U;

  /* Arm EP2 OUT — ready to receive first command from host */
  USBD_LL_PrepareReceive(pdev, THUNDER_EP_CMD_OUT, hthunder->CmdRxBuffer,
                         THUNDER_CMD_OUT_PACKET_SIZE);
  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * USBD_THUNDER_DeInit
 * Closes EP1 IN, EP2 IN, EP2 OUT. Frees handle memory.
 * ===========================================================================*/
static uint8_t USBD_THUNDER_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx) {
  UNUSED(cfgidx);

  USBD_LL_CloseEP(pdev, THUNDER_EP_SNIFFER_IN);
  pdev->ep_in[THUNDER_EP_SNIFFER_IN & 0xFU].is_used = 0U;

  USBD_LL_CloseEP(pdev, THUNDER_EP_CMD_ANALOG_IN);
  pdev->ep_in[THUNDER_EP_CMD_ANALOG_IN & 0xFU].is_used = 0U;

  USBD_LL_CloseEP(pdev, THUNDER_EP_CMD_OUT);
  pdev->ep_out[THUNDER_EP_CMD_OUT & 0xFU].is_used = 0U;

  if (pdev->pClassData != NULL) {
    USBD_free(pdev->pClassData);
    pdev->pClassData = NULL;
  }

  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * USBD_THUNDER_Setup
 * Standard GET_STATUS / GET_INTERFACE only — all commands via Bulk EP2 OUT
 * ===========================================================================*/
static uint8_t USBD_THUNDER_Setup(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req) {
  uint16_t status_info = 0U;
  uint8_t ifalt = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bmRequest & USB_REQ_TYPE_MASK) {
  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest) {
    case USB_REQ_GET_STATUS:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
        USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
      else {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case USB_REQ_GET_INTERFACE:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
        USBD_CtlSendData(pdev, &ifalt, 1U);
      else {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case USB_REQ_SET_INTERFACE:
      if (pdev->dev_state != USBD_STATE_CONFIGURED) {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case USB_REQ_CLEAR_FEATURE:
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    ret = USBD_FAIL;
    break;
  }

  return (uint8_t)ret;
}

/* ===========================================================================
 * USBD_THUNDER_DataIn
 * Called on any IN transfer completion (EP1 IN or EP2 IN).
 *
 * EP2 IN priority logic:
 *   After any TX completes on EP2 IN, check if a command response is pending.
 *   If yes — send the response immediately (before next analog packet).
 *   Response flag is cleared after it is queued for transmission.
 * ===========================================================================*/
static uint8_t USBD_THUNDER_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum) {
  USBD_THUNDER_HandleTypeDef *hthunder;

  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;
  hthunder = (USBD_THUNDER_HandleTypeDef *)pdev->pClassData;

  /* ZLP handling — send zero-length packet if last packet was full size */
  if ((pdev->ep_in[epnum & 0xFU].total_length > 0U) &&
      ((pdev->ep_in[epnum & 0xFU].total_length %
        pdev->ep_in[epnum & 0xFU].maxpacket) == 0U)) {
    pdev->ep_in[epnum & 0xFU].total_length = 0U;
    (void)USBD_LL_Transmit(pdev, epnum, NULL, 0U);
    return (uint8_t)USBD_OK;
  }

  /* ---- EP1 IN: CC Sniffer TX complete ---- */
  if ((epnum & 0x7FU) == (THUNDER_EP_SNIFFER_IN & 0x7FU)) {
    hthunder->SnifferTxState = 0U;

    /* Notify sniffer layer — buffer is free for next capture */
    extern void USB_Sniffer_TxCompleteCallback(void);
    USB_Sniffer_TxCompleteCallback();
  }

  /* ---- EP2 IN: Analog + CMD Response TX complete ---- */
  else if ((epnum & 0x7FU) == (THUNDER_EP_CMD_ANALOG_IN & 0x7FU)) {
    hthunder->EP2_TxState = 0U;

    /* If a command response is waiting, send it now before next analog */
    if (hthunder->ResponsePending) {
      hthunder->ResponsePending = 0U;
      hthunder->EP2_TxState = 1U;
      pdev->ep_in[THUNDER_EP_CMD_ANALOG_IN & 0xFU].total_length =
          hthunder->ResponseLength;
      (void)USBD_LL_Transmit(pdev, THUNDER_EP_CMD_ANALOG_IN,
                             hthunder->ResponseBuffer,
                             hthunder->ResponseLength);
    } else {
      /* EP2 IN is truly free — notify analog layer */
      USB_Analog_TxCompleteCallback();
    }
  }

  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * USBD_THUNDER_DataOut
 * Called when EP2 OUT receives a command packet from host.
 * Fires USBD_THUNDER_CMD_Received() callback — DataIn handles response.
 * ===========================================================================*/
static uint8_t USBD_THUNDER_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum) {
  USBD_THUNDER_HandleTypeDef *hthunder;

  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;
  hthunder = (USBD_THUNDER_HandleTypeDef *)pdev->pClassData;

  if ((epnum & 0x7FU) == (THUNDER_EP_CMD_OUT & 0x7FU)) {
    hthunder->CmdRxLength = USBD_LL_GetRxDataSize(pdev, epnum);
    hthunder->CmdRxState = 1U;
    USBD_THUNDER_CMD_Received(hthunder->CmdRxBuffer, hthunder->CmdRxLength);
  }

  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * EP0 Rx Ready — nothing needed for this class
 * ===========================================================================*/
static uint8_t USBD_THUNDER_EP0_RxReady(USBD_HandleTypeDef *pdev) {
  UNUSED(pdev);
  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * Descriptor Accessors
 * ===========================================================================*/
static uint8_t *USBD_THUNDER_GetFSCfgDesc(uint16_t *length) {
  *length = (uint16_t)sizeof(USBD_THUNDER_CfgFSDesc);
  return USBD_THUNDER_CfgFSDesc;
}

static uint8_t *USBD_THUNDER_GetDeviceQualifierDescriptor(uint16_t *length) {
  *length = (uint16_t)sizeof(USBD_THUNDER_DeviceQualifierDesc);
  return USBD_THUNDER_DeviceQualifierDesc;
}

/* ===========================================================================
 * Public Transmit: EP1 IN — CC Sniffer
 * Called from sniffer layer when a capture buffer is full.
 * Returns USBD_BUSY if previous packet not yet sent — caller must retry.
 * ===========================================================================*/
uint8_t USBD_THUNDER_Sniffer_Transmit(USBD_HandleTypeDef *pdev, uint8_t *pbuf,
                                      uint16_t length) {
  USBD_THUNDER_HandleTypeDef *hthunder;
  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;
  hthunder = (USBD_THUNDER_HandleTypeDef *)pdev->pClassData;

  if (hthunder->SnifferTxState != 0U)
    return (uint8_t)USBD_BUSY;

  hthunder->SnifferTxState = 1U;
  pdev->ep_in[THUNDER_EP_SNIFFER_IN & 0xFU].total_length = length;
  (void)USBD_LL_Transmit(pdev, THUNDER_EP_SNIFFER_IN, pbuf, length);
  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * Public Transmit: EP2 IN — Analog Parameters
 * Called from 1kHz ADC task.
 * Returns USBD_BUSY if previous packet not sent — drop sample, never block.
 * ===========================================================================*/
uint8_t USBD_THUNDER_Analog_Transmit(USBD_HandleTypeDef *pdev, uint8_t *pbuf,
                                     uint16_t length) {
  USBD_THUNDER_HandleTypeDef *hthunder;
  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;
  hthunder = (USBD_THUNDER_HandleTypeDef *)pdev->pClassData;

  if (hthunder->EP2_TxState != 0U)
    return (uint8_t)USBD_BUSY;

  hthunder->EP2_TxState = 1U;
  pdev->ep_in[THUNDER_EP_CMD_ANALOG_IN & 0xFU].total_length = length;
  (void)USBD_LL_Transmit(pdev, THUNDER_EP_CMD_ANALOG_IN, pbuf, length);
  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * Public Transmit: EP2 IN — Command Response
 * Stores response in handle buffer.
 * If EP2 is currently busy (sending analog), sets ResponsePending —
 * DataIn will send it as soon as EP2 is free.
 * If EP2 is idle, sends immediately.
 * ===========================================================================*/
uint8_t USBD_THUNDER_Response_Transmit(USBD_HandleTypeDef *pdev, uint8_t *pbuf,
                                       uint16_t length) {
  USBD_THUNDER_HandleTypeDef *hthunder;
  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;
  hthunder = (USBD_THUNDER_HandleTypeDef *)pdev->pClassData;

  uint16_t safe_len = (length > THUNDER_ANALOG_IN_PACKET_SIZE)
                          ? THUNDER_ANALOG_IN_PACKET_SIZE
                          : length;

  /* Always copy into response buffer first */
  (void)memcpy(hthunder->ResponseBuffer, pbuf, safe_len);
  hthunder->ResponseLength = safe_len;

  if (hthunder->EP2_TxState != 0U) {
    /* EP2 busy sending analog — queue response, DataIn will fire it */
    hthunder->ResponsePending = 1U;
    return (uint8_t)USBD_OK;
  }

  /* EP2 idle — send response immediately */
  hthunder->EP2_TxState = 1U;
  hthunder->ResponsePending = 0U;
  pdev->ep_in[THUNDER_EP_CMD_ANALOG_IN & 0xFU].total_length = safe_len;
  (void)USBD_LL_Transmit(pdev, THUNDER_EP_CMD_ANALOG_IN,
                         hthunder->ResponseBuffer, safe_len);
  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * Re-arm EP2 OUT for next command reception
 * MUST be called at end of every USBD_THUNDER_CMD_Received() implementation
 * ===========================================================================*/
uint8_t USBD_THUNDER_CMD_ReceivePacket(USBD_HandleTypeDef *pdev) {
  USBD_THUNDER_HandleTypeDef *hthunder;
  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;
  hthunder = (USBD_THUNDER_HandleTypeDef *)pdev->pClassData;

  if (pdev->dev_state == USBD_STATE_CONFIGURED) {
    USBD_LL_PrepareReceive(pdev, THUNDER_EP_CMD_OUT, hthunder->CmdRxBuffer,
                           THUNDER_CMD_OUT_PACKET_SIZE);
  }

  return (uint8_t)USBD_OK;
}

/* ===========================================================================
 * Weak default CMD_Received callback
 * Override in usb_thunder_if.c with actual command dispatch logic
 * ===========================================================================*/
__attribute__((weak)) uint8_t USBD_THUNDER_CMD_Received(uint8_t *buf,
                                                        uint32_t len) {
  UNUSED(buf);
  UNUSED(len);
  extern USBD_HandleTypeDef hUsbDeviceFS;
  USBD_THUNDER_CMD_ReceivePacket(&hUsbDeviceFS);
  return (uint8_t)USBD_OK;
}
