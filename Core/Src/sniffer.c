/*
 * sniffer.c — STM32G474 HRTIM-based USB-PD sniffer
 * Pure hardware: COMP→EEV→CPT1DE+ROVDE→DMA circular → no inject_sentinel
 */

#include "sniffer.h"
#include "main.h"
#include "usb_device_app.h"
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

/* Metadata arrays are indexed as channel * 4 + dma_half * 2 + group_in_half. */
static volatile uint16_t sample_tstamp[8];
static volatile uint16_t sample_seq[8];
static volatile uint16_t snap_seq[8];
static volatile uint16_t snap_tstamp[8];
static volatile uint32_t sample_tstamp32[8];
static volatile uint32_t snap_tstamp32[8];
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
  if (g_sniffer_state != SNIFFER_STATE_IDLE)
    return;

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
  uint8_t meta_base;
  uint64_t mask;
  uint64_t next;
  uint64_t base_bit = channel * 32;

  __DMB(); /* ensure DMA writes to samples[] are visible */

  if (buffer_idx == 0) {
    meta_base = channel * 4;                 /* CCx first DMA half */
    mask = (uint64_t)0x0000FFFF << base_bit; /* first 16 chunks    */
    next = (uint64_t)0x00010000 << base_bit; /* check second half  */
  } else {
    meta_base = (channel * 4) + 2;           /* CCx second DMA half */
    mask = (uint64_t)0xFFFF0000 << base_bit; /* second 16 chunks    */
    next = (uint64_t)0x00000001 << base_bit; /* check first half    */
  }

  uint16_t ts = get_timestamp();
  uint32_t ts32 = get_timestamp32();
  uint16_t sq0 = ((seq++ << 4) & 0x0FF0) | ((uint16_t)channel << 14);
  uint16_t sq1 = ((seq++ << 4) & 0x0FF0) | ((uint16_t)channel << 14);

  if (filled_dma & next) {
    overflow_count++;
    sq0 |= 0x8000; /* overflow flag for host */
    sq1 |= 0x8000;
  }

  if (filled_dma & mask) {
    overflow_count++;
    sq0 |= 0x8000;
    sq1 |= 0x8000;
  }

  /* Snap: always safe to update here — __WFI ensures main loop
   * drains previous group before this fires again in normal operation */
  snap_seq[meta_base] = sq0;
  snap_tstamp[meta_base] = ts;
  snap_tstamp32[meta_base] = ts32;
  snap_seq[meta_base + 1u] = sq1;
  snap_tstamp[meta_base + 1u] = ts;
  snap_tstamp32[meta_base + 1u] = ts32;

  sample_seq[meta_base] = sq0;
  sample_tstamp[meta_base] = ts;
  sample_tstamp32[meta_base] = ts32;
  sample_seq[meta_base + 1u] = sq1;
  sample_tstamp[meta_base + 1u] = ts;
  sample_tstamp32[meta_base + 1u] = ts32;

  __DMB(); /* ensure DMA writes to samples[] are visible */

  filled_dma |= mask;
}

void sniffer_task(void) {
  static uint8_t u = 0;
  static uint8_t d = 0;
  uint8_t scanned;

  while (filled_dma && free_usb) {

    /* ── Find next filled sub-buffer ─────────────────────── */
    scanned = 0;
    while (!(filled_dma & (1ULL << d))) {
      d = (d + 1) & 63;
      if (++scanned >= 64)
        return;
    }

    /* ── Find free USB buffer ─────────────────────────────── */
    if (!(free_usb & (1u << u)))
      u = !u;
    if (!(free_usb & (1u << u)))
      return;

    uint8_t ch = d >> 5;
    uint8_t half = (d >> 4) & 1;
    uint8_t slot_idx = d & 0x1F;
    uint8_t group_idx = (slot_idx >> 3) & 0x01;
    uint8_t tidx = (ch * 4) + (half * 2) + group_idx;
    uint8_t chunk_idx = d & 0x07;
    uint16_t item_off = (uint16_t)(slot_idx * EDGES_PER_SLOT);

    uint16_t seq_word = snap_seq[tidx] | (chunk_idx & 0x07);
    uint32_t ts32 = snap_tstamp32[tidx];
    ep_buf[u][0] = (uint8_t)(seq_word & 0xFF);
    ep_buf[u][1] = (uint8_t)(seq_word >> 8);
    ep_buf[u][2] = (uint8_t)(ts32 & 0xFF);
    ep_buf[u][3] = (uint8_t)((ts32 >> 8) & 0xFF);
    ep_buf[u][4] = (uint8_t)((ts32 >> 16) & 0xFF);
    ep_buf[u][5] = (uint8_t)((ts32 >> 24) & 0xFF);
    ep_buf[u][6] = EP_EDGE_PACKET_MARKER;
    ep_buf[u][7] = (uint8_t)(EP_CHUNK_MARKER_BASE - chunk_idx);

    memcpy(&ep_buf[u][EP_PACKET_HEADER_SIZE], &samples[ch][item_off],
           EP_PAYLOAD_SIZE);

    atomic_clear_bit_u32(&free_usb, u);

    if (USB_Sniffer_Send_Packet(u) == 0) {
      atomic_clear_bit_u64(&filled_dma, d);
      d = (d + 1) & 63; // ✅ Always advance AFTER successful send
      u ^= 1u;
    } else {
      free_usb |= (1u << u);
      // ✅ Do NOT advance d — retry same slot next time
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
  memset((void *)sample_tstamp, 0, sizeof(sample_tstamp));
  memset((void *)sample_seq, 0, sizeof(sample_seq));
  memset((void *)snap_seq, 0, sizeof(snap_seq));       // ✅ Add
  memset((void *)snap_tstamp, 0, sizeof(snap_tstamp)); // ✅ Add
  memset((void *)sample_tstamp32, 0, sizeof(sample_tstamp32));
  memset((void *)snap_tstamp32, 0, sizeof(snap_tstamp32));
}

uint8_t sniffer_get_dma_status() { return (filled_dma != 0); }

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
