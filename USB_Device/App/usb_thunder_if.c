/**
 ******************************************************************************
 * @file    usb_thunder_if.c
 * @brief   Application interface layer — Phase 1
 *
 *  Responsibilities:
 *   1. USBD_THUNDER_CMD_Received() — parse & dispatch commands from EP1 OUT
 *      Sends response via USBD_THUNDER_Response_Transmit() on EP1 IN.
 *      Response priority is enforced in usbd_thunder.c DataIn handler.
 *
 *   2. USB_Thunder_Analog_Send()   — wrap analog TX for application layer
 *   3. USB_Thunder_Sniffer_Send()  — wrap sniffer TX for sniffer layer
 *   4. USB_Sniffer_TxCompleteCallback() — release sniffer ping-pong buffer
 ******************************************************************************
 */

#include "usb_thunder_if.h"
#include "usbd_thunder.h"
#include <stdio.h>
#include <string.h>

/* USER CODE BEGIN Includes */
/* #include "sniffer.h"    */
/* #include "adc_task.h"   */
/* #include "gpio_ctrl.h"  */
/* USER CODE END Includes */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* ============================================================================
 * Private Helper: Build and send a command response on EP1 IN
 * ============================================================================*/
static void SendResponse(uint8_t cmd_id, uint8_t status, const char *msg,
                         uint8_t msg_len) {
  USBD_THUNDER_CmdRespTypeDef resp;
  memset(&resp, 0, sizeof(resp));

  resp.cmd_id = cmd_id;
  resp.status = status;
  resp.length =
      (msg_len > sizeof(resp.payload)) ? sizeof(resp.payload) : msg_len;
  if (msg && resp.length > 0U) {
    memcpy(resp.payload, msg, resp.length);
  }

  USBD_THUNDER_Response_Transmit(&hUsbDeviceFS, (uint8_t *)&resp,
                                 (uint16_t)sizeof(USBD_THUNDER_CmdRespTypeDef));
}

/* ============================================================================
 * Command Received Callback — overrides weak symbol in usbd_thunder.c
 *
 * Called from DataOut ISR context when EP1 OUT receives a 64-byte packet.
 * Parse cmd_id, execute, send response via EP1 IN, re-arm EP1 OUT.
 *
 * IMPORTANT: Keep processing minimal in ISR context.
 *            For heavy operations, set a flag and handle in main loop.
 * ============================================================================*/
uint8_t USBD_THUNDER_CMD_Received(uint8_t *buf, uint32_t len) {
  if (buf == NULL || len == 0U)
    goto rearm;

  USBD_THUNDER_CmdTypeDef *cmd = (USBD_THUNDER_CmdTypeDef *)buf;
  char msg[61] = {0};
  uint8_t msg_len = 0U;

  switch (cmd->cmd_id) {
  /* ------------------------------------------------------------------ */
  case CMD_ID_START_CAPTURE:
    /* USER CODE BEGIN CMD_START_CAPTURE */
    /* sniffer_start_capture();          */
    /* adc_task_start();                 */
    /* USER CODE END CMD_START_CAPTURE   */
    msg_len = (uint8_t)snprintf(msg, sizeof(msg), "OK:CAPTURE_STARTED");
    SendResponse(cmd->cmd_id, THUNDER_RESP_OK, msg, msg_len);
    break;

  /* ------------------------------------------------------------------ */
  case CMD_ID_STOP_CAPTURE:
    /* USER CODE BEGIN CMD_STOP_CAPTURE */
    /* sniffer_stop_capture();           */
    /* adc_task_stop();                  */
    /* USER CODE END CMD_STOP_CAPTURE   */
    msg_len = (uint8_t)snprintf(msg, sizeof(msg), "OK:CAPTURE_STOPPED");
    SendResponse(cmd->cmd_id, THUNDER_RESP_OK, msg, msg_len);
    break;

  /* ------------------------------------------------------------------ */
  case CMD_ID_SET_SAMPLE_RATE:
    /* payload[0..3] = sample rate in Hz, uint32_t little-endian */
    if (cmd->length >= 4U) {
      uint32_t rate = (uint32_t)(cmd->payload[0]) |
                      ((uint32_t)(cmd->payload[1]) << 8U) |
                      ((uint32_t)(cmd->payload[2]) << 16U) |
                      ((uint32_t)(cmd->payload[3]) << 24U);
      (void)rate;
      /* USER CODE BEGIN CMD_SET_SAMPLE_RATE           */
      /* adc_task_set_sample_rate(rate);               */
      /* USER CODE END CMD_SET_SAMPLE_RATE             */
      msg_len = (uint8_t)snprintf(msg, sizeof(msg), "OK:RATE:%lu",
                                  (unsigned long)rate);
      SendResponse(cmd->cmd_id, THUNDER_RESP_OK, msg, msg_len);
    } else {
      msg_len = (uint8_t)snprintf(msg, sizeof(msg), "ERR:RATE:PAYLOAD_SHORT");
      SendResponse(cmd->cmd_id, THUNDER_RESP_ERR, msg, msg_len);
    }
    break;

  /* ------------------------------------------------------------------ */
  case CMD_ID_SET_GPIO:
    /* payload[0] = pin number, payload[1] = state (0=LOW, 1=HIGH) */
    if (cmd->length >= 2U) {
      uint8_t pin = cmd->payload[0];
      uint8_t state = cmd->payload[1];
      (void)pin;
      (void)state;
      /* USER CODE BEGIN CMD_SET_GPIO                  */
      /* gpio_ctrl_set(pin, state);                    */
      /* USER CODE END CMD_SET_GPIO                    */
      msg_len =
          (uint8_t)snprintf(msg, sizeof(msg), "OK:GPIO:P%d=%d", pin, state);
      SendResponse(cmd->cmd_id, THUNDER_RESP_OK, msg, msg_len);
    } else {
      msg_len = (uint8_t)snprintf(msg, sizeof(msg), "ERR:GPIO:PAYLOAD_SHORT");
      SendResponse(cmd->cmd_id, THUNDER_RESP_ERR, msg, msg_len);
    }
    break;

  /* ------------------------------------------------------------------ */
  case CMD_ID_GET_STATUS:
    /* USER CODE BEGIN CMD_GET_STATUS                    */
    /* Populate from actual application state            */
    /* USER CODE END CMD_GET_STATUS                      */
    msg_len = (uint8_t)snprintf(msg, sizeof(msg), "STATUS:IDLE:FW_v1.0");
    SendResponse(cmd->cmd_id, THUNDER_RESP_OK, msg, msg_len);
    break;

  /* ------------------------------------------------------------------ */
  case CMD_ID_RESET:
    /* USER CODE BEGIN CMD_RESET                         */
    /* sniffer_stop_capture();                           */
    /* adc_task_stop();                                  */
    /* USER CODE END CMD_RESET                           */
    msg_len = (uint8_t)snprintf(msg, sizeof(msg), "OK:RESET");
    SendResponse(cmd->cmd_id, THUNDER_RESP_OK, msg, msg_len);
    break;

  /* ------------------------------------------------------------------ */
  default:
    msg_len = (uint8_t)snprintf(msg, sizeof(msg), "ERR:UNKNOWN_CMD:0x%02X",
                                cmd->cmd_id);
    SendResponse(cmd->cmd_id, THUNDER_RESP_UNKNOWN_CMD, msg, msg_len);
    break;
  }

rearm:
  /* Always re-arm EP2 OUT to receive next command */
  USBD_THUNDER_CMD_ReceivePacket(&hUsbDeviceFS);
  return (uint8_t)USBD_OK;
}

/* ============================================================================
 * Public: EP2 IN — Analog Parameters → Host
 * Call from 1kHz ADC timer callback.
 * ============================================================================*/
uint8_t USB_Thunder_Analog_Send(uint8_t *buf, uint16_t len) {
  return USBD_THUNDER_Analog_Transmit(&hUsbDeviceFS, buf, len);
}

/* ============================================================================
 * Public: EP1 IN — CC Sniffer → Host
 * Call from sniffer layer when capture buffer is full.
 * ============================================================================*/
uint8_t USB_Thunder_Sniffer_Send(uint8_t *buf, uint16_t len) {
  return USBD_THUNDER_Sniffer_Transmit(&hUsbDeviceFS, buf, len);
}
#if 0
/* ============================================================================
 * Sniffer TX Complete Callback
 * Called from DataIn handler when EP2 IN transfer completes.
 * Release the ping-pong buffer so sniffer DMA can refill it.
 * ============================================================================*/
void USB_Sniffer_TxCompleteCallback(void)
{
    /* USER CODE BEGIN SNIFFER_TX_COMPLETE                       */
    /* extern uint8_t g_current_sniffer_tx_buffer;               */
    /* if (g_current_sniffer_tx_buffer != 0xFF) {                */
    /*     uint32_t free = sniffer_get_free_usb_status();         */
    /*     free |= (1U << g_current_sniffer_tx_buffer);           */
    /*     sniffer_set_free_usb_status(free);                     */
    /*     g_current_sniffer_tx_buffer = 0xFF;                    */
    /* }                                                          */
    /* USER CODE END SNIFFER_TX_COMPLETE                         */
}
#endif