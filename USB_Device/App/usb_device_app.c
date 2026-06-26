/**
 * @file usb_device_app.c
 * @brief Application-layer USB integration — Phase 1
 *
 * Responsibilities:
 *   - USB_Sniffer_Send_Packet()       : called from sniffer DMA callback
 *   - USB_Sniffer_TxCompleteCallback(): implements weak symbol from
 * usbd_thunder.c releases ping-pong buffer on EP2 IN completion
 *
 * Command handling (EP1 OUT/IN) lives entirely in usb_thunder_if.c
 */

#include "sniffer.h"
#include "usb_device.h"
#include "usbd_thunder.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

/* Track which sniffer ping-pong buffer is currently in-flight */
static uint8_t current_sniffer_tx_buffer = 0xFF; /* 0xFF = idle */

/**
 * @brief Send one sniffer capture buffer via EP2 IN
 * @param buffer_idx  ping-pong buffer index (0 or 1)
 * @return 0 = queued OK, 1 = busy/invalid
 */
uint8_t USB_Analog_Send_Packet(uint8_t buffer_idx) {
  if (buffer_idx >= 2U) {
    return 1U;
  }

  uint8_t *pkt_buffer = sniffer_get_usb_buffer(buffer_idx);
  if (pkt_buffer == NULL) {
    return 1U;
  }

  if (USB_Thunder_Analog_Send(pkt_buffer, EP_BUF_SIZE) == USBD_OK) {
    current_sniffer_tx_buffer = buffer_idx;
    return 0U;
  }

  return 1U; /* USBD_BUSY or USBD_FAIL */
}

void USB_Analog_TxCompleteCallback(void) {
  /* Signal ADC task that EP2 IN is free.
   * Use a flag — do NOT call USBD_THUNDER_Analog_Transmit directly here
   * unless you are certain the buffer is already filled and ready. */
  analog_set_free_usb_status();
}

/**
 * @brief Send one sniffer capture buffer via EP2 IN
 * @param buffer_idx  ping-pong buffer index (0 or 1)
 * @return 0 = queued OK, 1 = busy/invalid
 */
uint8_t USB_Sniffer_Send_Packet(uint8_t buffer_idx) {
  if (buffer_idx >= 2U) {
    return 1U;
  }

  uint8_t *pkt_buffer = sniffer_get_usb_buffer(buffer_idx);
  if (pkt_buffer == NULL) {
    return 1U;
  }

  if (USB_Thunder_Sniffer_Send(pkt_buffer, EP_BUF_SIZE) == USBD_OK) {
    current_sniffer_tx_buffer = buffer_idx;
    return 0U;
  }

  return 1U; /* USBD_BUSY or USBD_FAIL */
}
// #if 0
/**
 * @brief EP2 IN TX complete — overrides weak symbol in usbd_thunder.c
 *
 * Called from DataIn ISR when EP2 IN transfer completes.
 * Releases the ping-pong buffer so sniffer DMA can refill it.
 */
void USB_Sniffer_TxCompleteCallback(void) {
  if (current_sniffer_tx_buffer != 0xFFU) {
    uint32_t free_status = sniffer_get_free_usb_status();
    free_status |= (1UL << current_sniffer_tx_buffer);
    sniffer_set_free_usb_status(free_status);
    current_sniffer_tx_buffer = 0xFFU;
  }
}
// #endif