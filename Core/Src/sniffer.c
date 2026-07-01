/*
 * sniffer.c — STM32G474 HRTIM-based USB-PD sniffer
 * Pure hardware: COMP→EEV→CPT1DE+ROVDE→DMA circular → no inject_sentinel
 */

#include "sniffer.h"
#include "main.h"
#include "usb_device_app.h"
#include "usb_thunder_if.h"
#include <string.h>

#ifdef REV1
static uint16_t samples[2][RX_ITEM_COUNT];
#else
/* ── DMA capture buffers (hardware writes here via DMA) ──────────── */
static uint16_t samples[2][RX_ITEM_COUNT];

/* ── Half-complete metadata ring ─────────────────────────────────── *
 * ISR pushes one entry each half-complete event.
 * Task reads it via index (no pop needed, indexed by half number).   */
typedef struct {
  uint16_t ts; /* SysTick timestamp when half filled            */
  uint8_t ovf; /* 1 if DMA overflow detected at this point      */
} half_meta_t;

static volatile half_meta_t meta[2][META_DEPTH]; /* [channel][half#] */
#endif

typedef struct tim64_t {
  uint16_t prev_cnt;
  uint64_t rollover;
} tim64_t;

typedef struct __attribute__((packed)) analog_packet_t {
  uint8_t marker0;      /*  1 — 0x01, F0-compatible analog marker */
  uint8_t marker1;      /*  1 — 0x20, F0-compatible analog marker */
  uint32_t timestamp;   /*  4 — 1 MHz packet timestamp */
  uint16_t vbus_mv;     /*  2 */
  int16_t vbus_ma;      /*  2 */
  uint16_t cc1_mv;      /*  2 */
  int16_t cc1_ma;       /*  2 */
  uint16_t cc2_mv;      /*  2 */
  int16_t cc2_ma;       /*  2 */
  uint8_t reserved[46]; /* 46 */
} analog_packet_t;      /* total = 64 bytes */

typedef char analog_packet_size_check[(sizeof(analog_packet_t) == EP_BUF_SIZE)
                                          ? 1
                                          : -1];

static analog_packet_t analog_pkt;

#ifdef REV1
static volatile uint64_t filled_dma = 0;
static volatile uint32_t seq = 0;

#define CAPTURE_BANK_COUNT 16u
#define CAPTURE_BANK_MASK (CAPTURE_BANK_COUNT - 1u)
#define CAPTURE_HALF_ITEMS (RX_EDGE_CHUNKS_PER_HALF * EDGES_PER_SLOT)

typedef struct {
  uint16_t data[CAPTURE_HALF_ITEMS];
  uint16_t seq_group[2];
  uint32_t timestamp32[2];
  uint8_t channel;
  uint8_t overflow;
} capture_bank_t;

static capture_bank_t capture_banks[CAPTURE_BANK_COUNT];
static volatile uint8_t capture_bank_wr = 0;
static volatile uint8_t capture_bank_rd = 0;
static volatile uint8_t capture_bank_count = 0;
static volatile uint8_t capture_overflow_pending = 0;
static uint8_t capture_tx_active = 0;
static uint8_t capture_tx_chunk = 0;
#else
/* ── Ring buffer absolute counters ───────────────────────────────── *
 * uint32_t never wraps in practice (160 slots/102ms → 4B slots/~8yrs)
 * available = wr_slot_abs[ch] - rd_slot_abs[ch]  (unsigned subtract) */
static volatile uint32_t wr_slot_abs[2]; /* advanced by ISR          */
static volatile uint32_t rd_slot_abs[2]; /* advanced by sniffer_task */
static volatile uint32_t meta_wr_abs[2]; /* ISR: how many halves pushed */
#endif

/* ── Global state ────────────────────────────────────────────────── */
static volatile uint32_t overflow_count = 0;
static volatile uint32_t free_usb = 3; /* bit0=ep_buf[0], bit1=ep_buf[1] */
static volatile uint32_t analog_usb_free = 1;
volatile sniffer_state_t g_sniffer_state = 3; // SNIFFER_STATE_IDLE;

/* ── USB double-buffer ───────────────────────────────────────────── */
static uint8_t ep_buf[2][EP_BUF_SIZE];
static uint8_t ep_adc_buf[EP_BUF_SIZE];
char analog[] = {"VBUS = 5000 mV ; 1500 mA"};

static void cc1_dma_half(DMA_HandleTypeDef *hdma);
static void cc1_dma_full(DMA_HandleTypeDef *hdma);
static void cc2_dma_half(DMA_HandleTypeDef *hdma);
static void cc2_dma_full(DMA_HandleTypeDef *hdma);
/* ── Helpers ─────────────────────────────────────────────────────── */
static inline void atomic_clear_bit_u32(volatile uint32_t *reg, uint8_t bit) {
  uint32_t p = __get_PRIMASK();
  __disable_irq();
  *reg &= ~(1u << bit);
  __set_PRIMASK(p);
}

static inline void atomic_clear_bit_u64(volatile uint64_t *reg, uint8_t bit) {
  uint32_t p = __get_PRIMASK();
  __disable_irq();
  *reg &= ~(1ULL << bit);
  __set_PRIMASK(p);
}

static inline uint16_t get_timestamp(void) {
  // return (uint16_t)(SysTick->LOAD - SysTick->VAL);
  return (uint16_t)HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CNTxR;
}

static inline uint32_t get_timestamp32(void) {
  uint32_t load = SysTick->LOAD + 1u;
  uint32_t val = SysTick->VAL;
  uint32_t ticks_per_ms = PACKET_TIMESTAMP_CLOCK_HZ / 1000u;
  uint32_t sub_ticks = ((load - val) * ticks_per_ms) / load;
  return (HAL_GetTick() * ticks_per_ms) + sub_ticks;
}
// return (uint16_t)(SysTick->LOAD - SysTick->VAL);
#if 0
static inline uint64_t get_timestamp64(uint16_t cnt) {

  if (cnt < prev_cnt) {
    g_rollover++;
  }
  return (uint64_t)(g_rollover * 65536ULL) + cnt;
  return (uint64_t)(g_rollover << 16) | cnt;
}
#endif
// ── Host activity control ─────────────────────────────────────────
void sniffer_host_start(void) {
  if (g_sniffer_state != SNIFFER_STATE_IDLE) {
    sniffer_host_stop();
  }

  // Reset ring counters so stale data isn't sent
  sniffer_init();

  // Re-arm DMA capture
  sniffer_start_capture();

  g_sniffer_state = SNIFFER_STATE_CAPTURING;
}

void sniffer_host_stop(void) {
  if (g_sniffer_state == SNIFFER_STATE_IDLE)
    return;

  // Stop DMA — no new edges captured after this
  HAL_HRTIM_SimpleCaptureStop_DMA(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A,
                                  HRTIM_CAPTUREUNIT_1);
  HAL_HRTIM_SimpleCaptureStop_DMA(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B,
                                  HRTIM_CAPTUREUNIT_1);
  HAL_HRTIM_WaveformCounterStop(&hhrtim1,
                                HRTIM_TIMERID_TIMER_A | HRTIM_TIMERID_TIMER_B);
  HAL_COMP_Stop(&hcomp1);
  HAL_COMP_Stop(&hcomp2);

  g_sniffer_state = SNIFFER_STATE_DRAINING;
}

sniffer_state_t sniffer_get_state(void) { return g_sniffer_state; }

#ifdef REV1

void sniffer_process_dma_buffer(uint8_t channel, uint8_t buffer_idx) {
  __DMB(); /* ensure DMA writes to samples[] are visible */

  uint32_t ts32 = get_timestamp32();
  uint16_t sq0 = ((seq++ << 4) & 0x0FF0) | ((uint16_t)channel << 14);
  uint16_t sq1 = ((seq++ << 4) & 0x0FF0) | ((uint16_t)channel << 14);

  if (capture_bank_count >= CAPTURE_BANK_COUNT) {
    overflow_count++;
    capture_overflow_pending = 1u;
    return;
  }

  uint8_t ovf = capture_overflow_pending;
  if (ovf) {
    sq0 |= 0x8000;
    sq1 |= 0x8000;
    capture_overflow_pending = 0u;
  }

  capture_bank_t *bank = &capture_banks[capture_bank_wr];
  uint16_t item_off =
      (uint16_t)(buffer_idx * RX_EDGE_CHUNKS_PER_HALF * EDGES_PER_SLOT);

  memcpy(bank->data, &samples[channel][item_off], sizeof(bank->data));
  bank->seq_group[0] = sq0;
  bank->seq_group[1] = sq1;
  bank->timestamp32[0] = ts32;
  bank->timestamp32[1] = ts32;
  bank->channel = channel;
  bank->overflow = ovf;

  __DMB(); /* ensure bank data is visible before publishing it */

  capture_bank_wr = (uint8_t)((capture_bank_wr + 1u) & CAPTURE_BANK_MASK);
  capture_bank_count++;
}

void sniffer_task(void) {
  static uint8_t u = 0;

  while (free_usb) {
    if (!capture_tx_active) {
      if (capture_bank_count == 0u) {
        return;
      }
      capture_tx_active = 1u;
      capture_tx_chunk = 0u;
    }

    capture_bank_t *bank = &capture_banks[capture_bank_rd];

    /* ── Find free USB buffer ─────────────────────────────── */
    if (!(free_usb & (1u << u)))
      u = !u;
    if (!(free_usb & (1u << u)))
      return;

    uint8_t group_idx = (capture_tx_chunk >> 3) & 0x01;
    uint8_t group_chunk = capture_tx_chunk & 0x07u;
    uint16_t item_off = (uint16_t)(capture_tx_chunk * EDGES_PER_SLOT);

    uint16_t seq_word = bank->seq_group[group_idx] | group_chunk;
    uint32_t ts32 = bank->timestamp32[group_idx];
    ep_buf[u][0] = (uint8_t)(seq_word & 0xFF);
    ep_buf[u][1] = (uint8_t)(seq_word >> 8);
    ep_buf[u][2] = (uint8_t)(ts32 & 0xFF);
    ep_buf[u][3] = (uint8_t)((ts32 >> 8) & 0xFF);
    ep_buf[u][4] = (uint8_t)((ts32 >> 16) & 0xFF);
    ep_buf[u][5] = (uint8_t)((ts32 >> 24) & 0xFF);
    ep_buf[u][6] = EP_EDGE_PACKET_MARKER;
    ep_buf[u][7] = (uint8_t)(EP_CHUNK_MARKER_BASE - group_chunk);

    memcpy(&ep_buf[u][EP_PACKET_HEADER_SIZE], &bank->data[item_off],
           EP_PAYLOAD_SIZE);

    atomic_clear_bit_u32(&free_usb, u);

    if (USB_Sniffer_Send_Packet(u) == 0) {
      u ^= 1u;
      capture_tx_chunk++;
      if (capture_tx_chunk >= RX_EDGE_CHUNKS_PER_HALF) {
        uint32_t p = __get_PRIMASK();
        __disable_irq();
        capture_bank_rd = (uint8_t)((capture_bank_rd + 1u) & CAPTURE_BANK_MASK);
        capture_bank_count--;
        __set_PRIMASK(p);
        capture_tx_active = 0u;
      }
    } else {
      free_usb |= (1u << u);
      break;
    }
  }
}
/* ─────────────────────────────────────────────────────────────────────────
 * sniffer_init
 * ─────────────────────────────────────────────────────────────────────────
 */
void sniffer_init(void) {
  filled_dma = 0;
  free_usb = 3;
  seq = 0;
  overflow_count = 0;
  memset(samples, 0, sizeof(samples));
  memset(ep_buf, 0, sizeof(ep_buf));
  memset(capture_banks, 0, sizeof(capture_banks));
  capture_bank_wr = 0;
  capture_bank_rd = 0;
  capture_bank_count = 0;
  capture_overflow_pending = 0;
  capture_tx_active = 0;
  capture_tx_chunk = 0;
}

uint8_t sniffer_get_dma_status() { return (capture_bank_count != 0u); }

#else

void sniffer_process_dma_buffer(uint8_t ch, uint8_t half_done) {
  __DMB(); /* ensure DMA writes to samples[] are visible */

  /* ── Overflow detection ────────────────────────────────────────
   * If task has not drained enough slots, the next DMA half-write
   * will overwrite data the task hasn't sent yet.                */
  uint32_t pending = wr_slot_abs[ch] - rd_slot_abs[ch];
  uint8_t ovf = (pending > (uint32_t)(NUM_SLOTS - NUM_HALF_SLOTS)) ? 1u : 0u;
  if (ovf)
    overflow_count++;

  /* ── Push metadata for this half ──────────────────────────────
   * mi is the ring index: cycles 0..META_DEPTH-1 repeatedly.
   * Task will index this same mi via (cur_half & (META_DEPTH-1))  */
  uint8_t mi = (uint8_t)(meta_wr_abs[ch] & (META_DEPTH - 1u));
  meta[ch][mi].ts = get_timestamp(); //(uint16_t)(SysTick->LOAD - SysTick->VAL);
  meta[ch][mi].ovf = ovf;

  __DMB(); /* meta[] committed before advancing counters         */

  /* ── Advance write pointers ────────────────────────────────── */
  meta_wr_abs[ch]++;                 /* signal meta is ready  */
  wr_slot_abs[ch] += NUM_HALF_SLOTS; /* signal data is ready  */
}

void sniffer_task(void) {
  static uint8_t u = 0;
#if 0
  // Only run task if capturing or draining
  if (g_sniffer_state == SNIFFER_STATE_IDLE)
    return;
#endif
  for (uint8_t ch = 0; ch < 2; ch++) {
    while (1) {
      uint32_t available = wr_slot_abs[ch] - rd_slot_abs[ch];
      if (available == 0)
        break;

      uint32_t slot_abs = rd_slot_abs[ch];
      uint32_t cur_half = slot_abs / NUM_HALF_SLOTS;
      if (cur_half >= meta_wr_abs[ch])
        break;

      uint8_t mi = (uint8_t)(cur_half & (META_DEPTH - 1u));
      uint16_t ts = meta[ch][mi].ts;
      uint8_t ovf = meta[ch][mi].ovf;

      if (!(free_usb & (1u << u))) {
        u ^= 1u;
        if (!(free_usb & (1u << u))) {
          // USB not accepting — if capturing, stop DMA to prevent overflow
          if (g_sniffer_state == SNIFFER_STATE_CAPTURING) {
            sniffer_host_stop(); // transition to DRAINING
          }
          return;
        }
      }

      // Build header + copy payload (unchanged from current code)
      uint8_t bidx = (uint8_t)(slot_abs & 7u);
      uint32_t seqno = slot_abs / 8u;
      uint16_t seq_word =
          (uint16_t)(((seqno << 3) & 0x0FF8u) | ((uint16_t)ch << 12) | bidx);
      if (ovf)
        seq_word |= 0x8000u;

      ep_buf[u][0] = (uint8_t)(seq_word & 0xFF);
      ep_buf[u][1] = (uint8_t)(seq_word >> 8);
      ep_buf[u][2] = (uint8_t)(ts & 0xFF);
      ep_buf[u][3] = (uint8_t)(ts >> 8);

      uint16_t item_off =
          (uint16_t)((slot_abs % (uint32_t)NUM_SLOTS) * EDGES_PER_SLOT);
      memcpy(&ep_buf[u][4], &samples[ch][item_off], EP_PAYLOAD_SIZE);

      atomic_clear_bit_u32(&free_usb, u);

      if (USB_Sniffer_Send_Packet(u) == 0) {
        rd_slot_abs[ch]++;
        u ^= 1u;
      } else {
        free_usb |= (1u << u);
        // USB rejected packet — host stopped polling
        if (g_sniffer_state == SNIFFER_STATE_CAPTURING) {
          sniffer_host_stop();
        }
        return;
      }
    }
  }

  // If draining and all slots consumed — go fully idle
  if (g_sniffer_state == SNIFFER_STATE_DRAINING) {
    if ((wr_slot_abs[0] == rd_slot_abs[0]) &&
        (wr_slot_abs[1] == rd_slot_abs[1])) {
      g_sniffer_state = SNIFFER_STATE_IDLE;
    }
  }
}

void sniffer_init(void) {
  memset(samples, 0, sizeof(samples));
  memset(ep_buf, 0, sizeof(ep_buf));
  memset((void *)meta, 0, sizeof(meta));

  wr_slot_abs[0] = wr_slot_abs[1] = 0;
  rd_slot_abs[0] = rd_slot_abs[1] = 0;
  meta_wr_abs[0] = meta_wr_abs[1] = 0;

  overflow_count = 0;
  free_usb = 3;
}

uint8_t sniffer_get_dma_status(void) {
  return (wr_slot_abs[0] != rd_slot_abs[0]) ||
         (wr_slot_abs[1] != rd_slot_abs[1]);
}
#endif
/* ── sniffer_start_capture ───────────────────────────────────────── */
void sniffer_start_capture(void) {
  HAL_COMP_Start(&hcomp1);
  HAL_COMP_Start(&hcomp2);

  /* SrcAddr = CPT1xR register of each timer
   * DMA reads this register on EVERY CPT1DE (edge) AND RSTDE (roll-over)
   * On roll-over: CPT1xR = stale last-edge value = overflow sentinel        */

  /* Timer A → CC1 line (COMP1 / PA1) → samples[0][] */
  if (HAL_HRTIM_SimpleCaptureStart_DMA(
          &hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_CAPTUREUNIT_1,
          (uint32_t)&HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A]
              .CPT1xR,          /* SrcAddr */
          (uint32_t)samples[0], /* DestAddr */
          RX_ITEM_COUNT) != HAL_OK)
    Error_Handler();

  /* Timer B → CC2 line (COMP2 / PA7) → samples[1][] */
  if (HAL_HRTIM_SimpleCaptureStart_DMA(
          &hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_CAPTUREUNIT_1,
          (uint32_t)&HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B]
              .CPT1xR,          /* SrcAddr */
          (uint32_t)samples[1], /* DestAddr */
          RX_ITEM_COUNT) != HAL_OK)
    Error_Handler();

  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].CPT1xCR =
      HRTIM_CAPTURETRIGGER_EEV_4;
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CPT1xCR =
      HRTIM_CAPTURETRIGGER_EEV_1;

  /* Register callbacks on the DMA handles directly */
  hdma_hrtim1_tima.XferHalfCpltCallback = cc1_dma_half;
  hdma_hrtim1_tima.XferCpltCallback = cc1_dma_full;
  __HAL_DMA_ENABLE_IT(&hdma_hrtim1_tima, DMA_IT_HT);

  hdma_hrtim1_timb.XferHalfCpltCallback = cc2_dma_half;
  hdma_hrtim1_timb.XferCpltCallback = cc2_dma_full;
  __HAL_DMA_ENABLE_IT(&hdma_hrtim1_timb, DMA_IT_HT);
  // #if 0
  /* HAL_HRTIM_SimpleCaptureStart_DMA only sets CPT1DE.
   * We also need RSTDE (roll-over DMA) for sentinel injection.
   * Add it AFTER the HAL call so HAL doesn't overwrite TIMxDIER.           */
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].TIMxDIER |= HRTIM_TIM_DMA_RST;
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxDIER |= HRTIM_TIM_DMA_RST;
// #endif
#if 0
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].TIMxDIER |= HRTIM_TIM_DMA_REP;
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxDIER |= HRTIM_TIM_DMA_REP;
#endif
  /* Start both timer counters */
  if (HAL_HRTIM_WaveformCounterStart(
          &hhrtim1, HRTIM_TIMERID_TIMER_A | HRTIM_TIMERID_TIMER_B) != HAL_OK)
    Error_Handler();
}

/* Implement them */
static void cc1_dma_half(DMA_HandleTypeDef *hdma) {
  sniffer_process_dma_buffer(SNIFFER_CHANNEL_CC1, 0); /* first half */
}
static void cc1_dma_full(DMA_HandleTypeDef *hdma) {
  sniffer_process_dma_buffer(SNIFFER_CHANNEL_CC1, 1); /* second half */
}
static void cc2_dma_half(DMA_HandleTypeDef *hdma) {
  sniffer_process_dma_buffer(SNIFFER_CHANNEL_CC2, 0);
}
static void cc2_dma_full(DMA_HandleTypeDef *hdma) {
  sniffer_process_dma_buffer(SNIFFER_CHANNEL_CC2, 1);
}

/* ── Accessors ───────────────────────────────────────────────────── */

uint8_t *sniffer_get_usb_buffer(uint8_t idx) {
  return (idx < 2) ? ep_buf[idx] : (idx == 2 ? ep_adc_buf : NULL);
}
uint32_t sniffer_get_free_usb_status(void) { return free_usb; }
void sniffer_set_free_usb_status(uint32_t s) { free_usb = s; }
uint32_t sniffer_get_overflow_count(void) { return overflow_count; }
void sniffer_reset_overflow_count(void) { overflow_count = 0; }
uint32_t analog_get_free_usb_status(void) { return analog_usb_free; }
void analog_set_free_usb_status(void) { analog_usb_free = 1; }

void analog_data_init(void) {
  memset(&analog_pkt, 0, sizeof(analog_pkt));
  analog_pkt.marker0 = 0x01U;
  analog_pkt.marker1 = 0x20U;
  analog_pkt.timestamp = get_timestamp32();
  analog_pkt.vbus_mv = 8000;
  analog_pkt.vbus_ma = 1600;
  analog_pkt.cc1_mv = 900;
  analog_pkt.cc1_ma = 50;
  analog_pkt.cc2_mv = 800;
  analog_pkt.cc2_ma = 80;
}

void analog_task() {
  static uint32_t last_send_ms = 0;
  uint32_t now_ms = HAL_GetTick();

  if ((now_ms - last_send_ms) < ANALOG_DUMMY_PERIOD_MS)
    return;

  if (!analog_get_free_usb_status())
    return;

  last_send_ms = now_ms;
  analog_pkt.timestamp = get_timestamp32();
  memcpy(ep_adc_buf, &analog_pkt, sizeof(analog_pkt));
  analog_usb_free = 0;
  USB_Thunder_Analog_Send(ep_adc_buf, EP_BUF_SIZE);
}
