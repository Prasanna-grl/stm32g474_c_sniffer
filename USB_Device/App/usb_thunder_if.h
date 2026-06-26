/**
 ******************************************************************************
 * @file    usb_thunder_if.h
 * @brief   Application interface layer header — Phase 1
 ******************************************************************************
 */

#ifndef __USB_THUNDER_IF_H
#define __USB_THUNDER_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_thunder.h"

/* ============================================================================
 * Command IDs — Host → Device via EP1 OUT
 * Keep in sync with host-side Python/libusb application.
 * Phase 2: same command set, no changes needed.
 * ============================================================================*/
#define CMD_ID_START_CAPTURE 0x01U /* Start CC + Analog capture             */
#define CMD_ID_STOP_CAPTURE 0x02U  /* Stop all capture                      */
#define CMD_ID_SET_SAMPLE_RATE                                                 \
  0x03U                         /* payload[0..3] = rate Hz (uint32_t LE)       \
                                 */
#define CMD_ID_SET_GPIO 0x04U   /* payload[0]=pin, [1]=state(0/1)        */
#define CMD_ID_GET_STATUS 0x05U /* Request device status string          */
#define CMD_ID_RESET 0x06U      /* Soft reset capture engine             */

/* ============================================================================
 * Public API — called from application (adc_task.c, sniffer.c, main.c)
 * ============================================================================*/

/**
 * @brief  Send one analog parameter snapshot to host via EP1 IN
 *         Call from your 1kHz ADC timer/DMA callback.
 *         If USBD_BUSY: increment overflow counter, drop sample, never block.
 */
uint8_t USB_Thunder_Analog_Send(uint8_t *buf, uint16_t len);

/**
 * @brief  Analog TX complete callback — EP1 IN transfer done, buffer is free.
 *         Implement in sniffer.c to release ping-pong buffer.
 *         Called automatically from DataIn handler in usbd_thunder.c.
 */
void USB_Analog_TxCompleteCallback(void);

/**
 * @brief  Send CC sniffer packet to host via EP2 IN
 *         Call from sniffer layer when capture buffer is full.
 */
uint8_t USB_Thunder_Sniffer_Send(uint8_t *buf, uint16_t len);

/**
 * @brief  Sniffer TX complete callback — EP1 IN transfer done, buffer is free.
 *         Implement in sniffer.c to release ping-pong buffer.
 *         Called automatically from DataIn handler in usbd_thunder.c.
 */
void USB_Sniffer_TxCompleteCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_THUNDER_IF_H */
