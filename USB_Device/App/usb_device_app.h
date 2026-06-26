/**
 * @file usb_device_integration.h
 * @brief Integration layer header
 */

#ifndef USB_DEVICE_APP_H
#define USB_DEVICE_APP_H

#include <stdint.h>

/* USB transmission functions */
uint8_t USB_Sniffer_Send_Packet(uint8_t buffer_idx);
void USB_Sniffer_TxComplete_Callback(void);

/* Command handlers */
void USB_Console_Command_Handler(uint8_t *buf, uint32_t len);
void USB_Command_Data_Handler(uint8_t *buf, uint32_t len);

#endif /* USB_DEVICE_APP_H */
