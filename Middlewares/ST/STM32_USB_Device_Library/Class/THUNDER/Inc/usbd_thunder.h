/**
******************************************************************************
* @file    usbd_thunder.h
* @brief   Vendor-specific USB Device Class — Phase 1
*
* Interface 0 — CC Sniffer    : EP1 IN  (libsigrok)
* Interface 1 — Command/Analog: EP2 IN  (Analog + CMD Response)
*                               EP2 OUT (Commands)
*
* EP1 OUT (0x01) reserved for future use — not opened.
*
* Phase 2 note: EP1 IN and EP2 IN will be merged into a single double-buffered
* endpoint with 2-bit packet type muxing in the sniffer frame header.
******************************************************************************
*/

#ifndef __USBD_THUNDER_H
#define __USBD_THUNDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_core.h"

/* ============================================================================
 * Interface Numbers
 * ============================================================================*/
#define THUNDER_INTERFACE_SNIFFER 0x00U    /* IF0: CC Sniffer              */
#define THUNDER_INTERFACE_CMD_ANALOG 0x01U /* IF1: Command/Analog          */

/* ============================================================================
 * Endpoint Addresses
 * ============================================================================*/
#define THUNDER_EP_SNIFFER_IN                                                  \
  0x81U /* EP1 IN  — CC Sniffer → Host (libsigrok)          */
#define THUNDER_EP_FUTURE_OUT                                                  \
  0x01U /* EP1 OUT — Future use (NOT opened, NOT in desc)   */
#define THUNDER_EP_CMD_ANALOG_IN                                               \
  0x82U /* EP2 IN  — Analog data + CMD responses → Host     */
#define THUNDER_EP_CMD_OUT                                                     \
  0x02U /* EP2 OUT — Commands ← Host                        */

/* ============================================================================
 * Packet Sizes — all Bulk FS
 * ============================================================================*/
#define THUNDER_DATA_FS_MAX_PACKET_SIZE 64U
#define THUNDER_SNIFFER_IN_PACKET_SIZE THUNDER_DATA_FS_MAX_PACKET_SIZE
#define THUNDER_ANALOG_IN_PACKET_SIZE THUNDER_DATA_FS_MAX_PACKET_SIZE
#define THUNDER_CMD_OUT_PACKET_SIZE THUNDER_DATA_FS_MAX_PACKET_SIZE

/* ============================================================================
 * String Descriptor Indices
 * ============================================================================*/
#define THUNDER_STR_SNIFFER_IDX 0x04U
#define THUNDER_STR_CMD_ANALOG_IDX 0x05U

/* ============================================================================
 * Configuration Descriptor Total Size
 *  9  (config)
 * +9  (IF0: Sniffer)   + 7 (EP1 IN)              = 16
 * +9  (IF1: Cmd/Analog)+ 7 (EP2 IN) + 7 (EP2 OUT)= 23
 * Total = 9 + 16 + 23 = 48 bytes  (unchanged)
 * ============================================================================*/
#define USB_THUNDER_CONFIG_DESC_SIZ 48U

/* ============================================================================
 * Analog Parameters Payload — exactly 64 bytes (one full Bulk packet)
 *
 * All raw ADC values direct from hardware.
 * 16-bit fields for voltage/current channels with higher resolution.
 * 8-bit fields for lower-range current measurements.
 * Timestamp + metadata in final bytes.
 *
 * Phase 2: This struct will gain a 2-byte header (type + reserved) at offset 0,
 * shifting all fields by 2 bytes. Plan for this when designing host parser.
 * ============================================================================*/
typedef struct __attribute__((packed)) {
  /* VBUS */
  uint16_t vbus_voltage_raw; /* 16-bit ADC — VBUS Voltage (e.g. 0-20V range) */
  uint16_t vbus_current_raw; /* 16-bit ADC — VBUS Current                     */

  /* CC1 */
  uint16_t cc1_voltage_raw; /* 16-bit ADC — CC1 Voltage  */
  uint8_t cc1_current_raw;  /* 8-bit  ADC — CC1 Current  */

  /* CC2 */
  uint16_t cc2_voltage_raw; /* 16-bit ADC — CC2 Voltage  */
  uint8_t cc2_current_raw;  /* 8-bit  ADC — CC2 Current  */

  /* D+ */
  uint16_t dp_voltage_raw; /* 16-bit ADC — D+ Voltage   */
  uint8_t dp_current_raw;  /* 8-bit  ADC — D+ Current   */

  /* D- */
  uint16_t dm_voltage_raw; /* 16-bit ADC — D- Voltage   */
  uint8_t dm_current_raw;  /* 8-bit  ADC — D- Current   */

  /* Metadata */
  uint32_t timestamp_ms; /* Device-side millisecond timestamp              */
  uint8_t sample_index;  /* Rolling 0-255 counter — detect dropped samples */
  uint8_t status_flags;  /* Bit0=VBUS_OVR, Bit1=CC1_OVR, Bit2=CC2_OVR    */

  /* Pad to 64 bytes: 2+2+2+1+2+1+2+1+2+1+4+1+1 = 22 bytes data + 42 pad */
  uint8_t reserved[42];
} USBD_THUNDER_AnalogTypeDef; /* Total: 64 bytes exactly */

/* ============================================================================
 * Command Packet — Host → Device via EP2 OUT (64 bytes)
 * cmd_id defines the operation, payload carries parameters.
 * Phase 2: same struct, no changes needed.
 * ============================================================================*/
#define THUNDER_CMD_MAX_PAYLOAD 62U

typedef struct __attribute__((packed)) {
  uint8_t cmd_id; /* Command identifier — see CMD_ID_* in usb_thunder_if.h */
  uint8_t length; /* Valid bytes in payload (0-62)                         */
  uint8_t payload[THUNDER_CMD_MAX_PAYLOAD]; /* Command-specific parameters */
} USBD_THUNDER_CmdTypeDef;                  /* Total: 64 bytes */

/* ============================================================================
 * Command Response Packet — Device → Host via EP2 IN (64 bytes)
 * Shares EP2 IN with analog data.
 * Priority: response > analog (see usb_thunder_if.c)
 * ============================================================================*/
typedef struct __attribute__((packed)) {
  uint8_t cmd_id;              /* Echo of the command being acknowledged */
  uint8_t status;              /* 0x00 = OK, 0x01 = ERR, 0x02 = BUSY    */
  uint8_t length;              /* Valid bytes in payload                  */
  uint8_t payload[61];         /* Response data (status string, values, etc.) */
} USBD_THUNDER_CmdRespTypeDef; /* Total: 64 bytes */

/* ============================================================================
 * Response Status Codes
 * ============================================================================*/
#define THUNDER_RESP_OK 0x00U
#define THUNDER_RESP_ERR 0x01U
#define THUNDER_RESP_BUSY 0x02U
#define THUNDER_RESP_UNKNOWN_CMD 0x03U

/* ============================================================================
 * Class Handle — internal state per endpoint
 * ============================================================================*/
typedef struct {
  /* EP1 IN — CC Sniffer */
  uint8_t SnifferTxState; /* 0=idle, 1=busy */

  /* EP2 IN — Analog + Command Response */
  uint8_t EP2_TxBuffer[THUNDER_ANALOG_IN_PACKET_SIZE];
  uint8_t EP2_TxState;     /* 0=idle, 1=busy */
  uint8_t ResponsePending; /* 1 = command response waiting to be sent */
  uint8_t ResponseBuffer[THUNDER_ANALOG_IN_PACKET_SIZE];
  uint16_t ResponseLength;

  /* EP2 OUT — Commands */
  uint8_t CmdRxBuffer[THUNDER_CMD_OUT_PACKET_SIZE];
  uint32_t CmdRxLength;
  uint8_t CmdRxState; /* 1 = new command received, not yet processed */

} USBD_THUNDER_HandleTypeDef;

/* ============================================================================
 * Exported Class Object
 * ============================================================================*/
extern USBD_ClassTypeDef USBD_THUNDER;

/* ============================================================================
 * Public API — called from usb_thunder_if.c and application layer
 * ============================================================================*/

/* EP1 IN: Send CC sniffer packet (called from sniffer layer) */
uint8_t USBD_THUNDER_Sniffer_Transmit(USBD_HandleTypeDef *pdev, uint8_t *pbuf,
                                      uint16_t length);

/* EP2 IN: Send analog packet (called from 1kHz ADC task) */
uint8_t USBD_THUNDER_Analog_Transmit(USBD_HandleTypeDef *pdev, uint8_t *pbuf,
                                     uint16_t length);

/* EP2 IN: Send command response (called from command dispatcher) */
uint8_t USBD_THUNDER_Response_Transmit(USBD_HandleTypeDef *pdev, uint8_t *pbuf,
                                       uint16_t length);

/* Re-arm EP2 OUT for next command */
uint8_t USBD_THUNDER_CMD_ReceivePacket(USBD_HandleTypeDef *pdev);

/* Weak callback — overridden in usb_thunder_if.c */
uint8_t USBD_THUNDER_CMD_Received(uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_THUNDER_H */
