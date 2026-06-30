/*
 * Sniffer Module Header
 * USB PD Sniffer for CC1 and CC2 capture
 * Migrated to STM32G474RE
 */

#ifndef SNIFFER_H
#define SNIFFER_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

#define REV1 /** This will be the version with smalled DMA Buffer Size */

// #define GPIO_CC_MOD

#ifdef REV1
/* STM32G474 HRTIM sniffer framing.
 *
 * The G4 port intentionally uses 16-bit HRTIM capture values at 24 MHz
 * instead of the original STM32F072 8-bit/2.4 MHz timing model. HRTIM Timer A
 * captures CC1 and Timer B captures CC2; rollover DMA requests are enabled as
 * the hardware sentinel source.
 *
 * Sniffer EP packets are still 64 bytes. Edge packets use the same 8-byte
 * header shape as the STM32F072B sniffer:
 *   bytes 0..1: sequence/channel/overflow word, little-endian
 *   bytes 2..5: 32-bit packet timestamp at 1 MHz, little-endian
 *   byte 6:     edge packet marker
 *   byte 7:     chunk marker
 *   bytes 8..63: 28 captured uint16_t edge/sentinel samples
 *
 * Each DMA half holds 16 edge packets. The USB header still exposes them as
 * two F072B-style 8-chunk groups so existing host framing can be reused.
 */
#define EP_BUF_SIZE 64
#define EP_PACKET_HEADER_SIZE 8
#define EP_PAYLOAD_SIZE (EP_BUF_SIZE - EP_PACKET_HEADER_SIZE)
#define RX_EDGE_CHUNKS_PER_HALF 16u
#define USB_EDGE_CHUNKS_PER_GROUP 8u
#define RX_COUNT (2u * RX_EDGE_CHUNKS_PER_HALF * EP_PAYLOAD_SIZE)
#define RX_ITEM_COUNT (RX_COUNT / sizeof(uint16_t))
#define EDGES_PER_SLOT (EP_PAYLOAD_SIZE / sizeof(uint16_t)) // 28 edges
#define NUM_SLOTS (2u * RX_EDGE_CHUNKS_PER_HALF) // total ring slots per channel
#define EP_EDGE_PACKET_MARKER 0x13u
#define EP_CHUNK_MARKER_BASE 0x18u
#else
/* Sniffer Configuration */
#define EP_BUF_SIZE 64
#define EP_PACKET_HEADER_SIZE 4
#define EP_PAYLOAD_SIZE (EP_BUF_SIZE - EP_PACKET_HEADER_SIZE)
#define OVERFLOW_BUFFER_SIZE 64

#define EDGES_PER_SLOT (EP_PAYLOAD_SIZE / sizeof(uint16_t)) // 30 edges
#define NUM_SLOTS 160u                 // total ring slots per channel
#define NUM_HALF_SLOTS (NUM_SLOTS / 2) // 80 — DMA HT fires every 80 slots
#define RX_COUNT (NUM_SLOTS * EP_PAYLOAD_SIZE)      // 160×60 = 9600 bytes
#define RX_ITEM_COUNT (RX_COUNT / sizeof(uint16_t)) // 4800 uint16_t items
#define META_DEPTH 16u // ⬆ was 8, increased for burst
#endif
/* Active G474 clock model: SYSCLK/HRTIM is 96 MHz and HRTIM DIV4 gives a
 * 24 MHz capture tick. One tick is ~41.7 ns; a 16-bit counter rolls over every
 * ~2.73 ms. These defines are documentation constants; MX_HRTIM1_Init() is the
 * hardware source of truth.
 */
#define HRTIM_CAPTURE_CLOCK_HZ 24000000U
#define HRTIM_CAPTURE_ROLLOVER_TICKS 65536U
#define PACKET_TIMESTAMP_CLOCK_HZ 1000000U
#define ANALOG_DUMMY_PERIOD_MS 10U

/* Channel definitions */
#define SNIFFER_CHANNEL_CC1 0
#define SNIFFER_CHANNEL_CC2 1

/* INA237 VBUS ADC */
#define INA237_ADDR (0x40 << 1)
#define INA237_REG_CONFIG 0x00     /* Configuration */
#define INA237_REG_ADC_CONFIG 0x01 /* ADC Configuration */
#define INA237_REG_SHUNT_CAL 0x02  /* Shunt Calibration */
#define INA237_REG_VSHUNT 0x04     /* Shunt Voltage Measurement */
#define INA237_REG_VBUS 0x05       /* Bus Voltage Measurement */
#define INA237_REG_DIETEMP 0x06    /* Temperature Measurement */
#define INA237_REG_CURRENT 0x07    /* Current Result */
#define INA237_REG_POWER 0x08      /* Power Result */
#define INA237_REG_DIAG_ALRT 0x0B  /* Diagnostic Flags and Alert */

/* External handles (defined in main.c) */

extern COMP_HandleTypeDef hcomp1;
extern COMP_HandleTypeDef hcomp2;

extern HRTIM_HandleTypeDef hhrtim1;
extern DMA_HandleTypeDef hdma_hrtim1_tima;
extern DMA_HandleTypeDef hdma_hrtim1_timb;

extern I2C_HandleTypeDef hi2c1;

// ADD after existing defines:
typedef enum {
  SNIFFER_STATE_IDLE = 0,
  SNIFFER_STATE_CAPTURING = 1,
  SNIFFER_STATE_DRAINING = 2,
} sniffer_state_t;

// ADD to function prototypes:
void sniffer_host_start(void);
void sniffer_host_stop(void);
sniffer_state_t sniffer_get_state(void);

/* Sniffer buffer access functions */
uint8_t *sniffer_get_usb_buffer(uint8_t buf_idx);
uint32_t sniffer_get_free_usb_status(void);
uint8_t sniffer_get_dma_status(void);
void sniffer_set_free_usb_status(uint32_t status);

/* Sniffer initialization and control */
void sniffer_init(void);
void analog_data_init(void);
void sniffer_start_capture(void);

/* Buffer management */
void sniffer_process_dma_buffer(uint8_t channel, uint8_t buffer_idx);
void sniffer_task(void);
void analog_task(void);
/* Status and statistics */
uint32_t sniffer_get_overflow_count(void);
void sniffer_reset_overflow_count(void);

void INA237_Config(void);
void vbus_adc_read();
#endif /* SNIFFER_H */
