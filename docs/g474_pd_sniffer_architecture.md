# STM32G474 PD Sniffer Capture Architecture

This document describes the current STM32G474 USB-PD sniffer data path after
the move to 16-bit HRTIM capture samples and the STM32F072B-compatible USB
packet header.

## Overview

The firmware captures USB-PD CC transitions on both CC lines and streams the
captured timing data over a vendor USB bulk endpoint.

The design is conceptually based on the STM32F072B sniffer architecture, but the
G474 implementation uses HRTIM instead of the F0 timer setup:

- CC1 is sampled through `COMP1` into HRTIM Timer A capture.
- CC2 is sampled through `COMP2` into HRTIM Timer B capture.
- HRTIM captures both edges from the comparator output.
- HRTIM rollover DMA requests are enabled so sentinel samples are inserted by
  hardware.
- DMA writes captured `uint16_t` HRTIM values into circular buffers.
- DMA half/full callbacks immediately copy completed halves into a software
  bank FIFO.
- The foreground `sniffer_task()` frames copied FIFO banks into 64-byte USB
  packets.

The key migration difference from STM32F072B is sample width:

```text
STM32F072B:  8-bit timing sample, 2.4 MHz timer model
STM32G474:  16-bit timing sample, 24 MHz HRTIM capture clock
```

One G474 edge/sentinel sample is therefore two bytes.

## Capture Clock

The active G474 timer model is:

```text
SYSCLK / HRTIM clock: 96 MHz
HRTIM prescaler:      DIV4
Capture tick:         24 MHz
Tick period:          41.67 ns
16-bit rollover:      65536 / 24 MHz = 2.73 ms
```

The capture clock constants are documented in `Core/Inc/sniffer.h`, but the
hardware source of truth is `MX_HRTIM1_Init()` in `Core/Src/main.c`.

## USB Packet Format

All sniffer packets are 64-byte USB full-speed bulk packets.

Edge packets use the same 8-byte header shape as the STM32F072B sniffer:

| Bytes | Size | Meaning |
| --- | ---: | --- |
| `0..1` | 2 | little-endian sequence/channel/overflow word |
| `2..5` | 4 | little-endian 32-bit packet timestamp at 1 MHz |
| `6` | 1 | edge packet marker, `0x13` |
| `7` | 1 | chunk marker, `0x18 - chunk_index` |
| `8..63` | 56 | 28 little-endian `uint16_t` HRTIM samples |

Host-side parsing can reuse the F072B packet header parser. The payload decoder
must change from 56 one-byte samples to 28 little-endian 16-bit samples, and the
timing scale must be 24 MHz.

## Buffer Sizing

The active `REV1` configuration is:

```c
#define EP_BUF_SIZE 64
#define EP_PACKET_HEADER_SIZE 8
#define EP_PAYLOAD_SIZE 56
#define RX_EDGE_CHUNKS_PER_HALF 16
#define RX_COUNT (2 * RX_EDGE_CHUNKS_PER_HALF * EP_PAYLOAD_SIZE)
#define RX_ITEM_COUNT (RX_COUNT / sizeof(uint16_t))
#define EDGES_PER_SLOT (EP_PAYLOAD_SIZE / sizeof(uint16_t))
#define NUM_SLOTS (2 * RX_EDGE_CHUNKS_PER_HALF)
```

This gives:

```text
Payload per USB packet:      56 bytes
Samples per USB packet:      28 uint16_t samples
DMA chunks per half-buffer:  16 USB packets
DMA half-buffer size:        16 * 56 = 896 bytes
DMA half-buffer samples:     448 uint16_t samples
DMA full buffer per channel: 1792 bytes
DMA full samples per channel:896 uint16_t samples
```

There are two capture buffers:

```c
static uint16_t samples[2][RX_ITEM_COUNT];
```

`samples[0]` is CC1 and `samples[1]` is CC2.

## DMA Copy Bank FIFO

The active `REV1` path does not send directly from the live DMA circular
buffers. That older approach allowed USB backpressure to leave pending records
inside `samples[][]` until DMA wrapped and overwrote them. The symptom was
zero or low host-side chunk warnings but occasional stale/out-of-order samples
inside otherwise valid PD packets.

The current path decouples DMA capture from USB transmit using copied banks:

```c
#define CAPTURE_BANK_COUNT 16
#define CAPTURE_HALF_ITEMS (RX_EDGE_CHUNKS_PER_HALF * EDGES_PER_SLOT)

typedef struct {
  uint16_t data[CAPTURE_HALF_ITEMS];
  uint16_t seq_group[2];
  uint32_t timestamp32[2];
  uint8_t channel;
  uint8_t overflow;
} capture_bank_t;
```

Each bank stores one completed DMA half:

```text
edge data:     16 chunks * 28 uint16_t samples = 448 samples
payload bytes: 896 bytes
metadata:      channel, overflow, two sequence groups, two timestamps
```

DMA callbacks perform only the critical preservation step:

```text
HRTIM DMA half/full interrupt
  -> copy completed half from samples[ch][offset] into next capture bank
  -> store timestamp and sequence metadata with that bank
  -> publish bank to FIFO
```

The foreground task then drains copied banks:

```text
sniffer_task()
  -> take oldest copied bank
  -> emit chunk 0..15 as 16 EP1 packets
  -> release bank after all chunks are accepted by USB
```

This adds one block `memcpy()` per DMA half-complete/full-complete event, but
the copy is only 896 bytes. The important behavior is that USB latency can no
longer corrupt the live DMA capture buffer. If the bank FIFO itself becomes
full, the next published bank is dropped and the following transmitted group is
marked with the overflow bit.

## F072B-Compatible Grouping

One DMA half now contains 16 G474 edge packets. The USB header still exposes the
data as two F072B-style 8-chunk groups:

```text
DMA half chunks 0..7:   sequence group A, chunk index 0..7
DMA half chunks 8..15:  sequence group B, chunk index 0..7
```

This keeps the host-facing sequence/chunk rhythm compatible with the F072B
packet family while preserving the larger G474 DMA buffer.

Each copied bank carries two sequence bases and two timestamps, one for chunks
`0..7` and one for chunks `8..15`. The chunk index is ORed into the low three
bits when the USB packet is built.

## Sequence Word

The sequence word is little-endian in bytes `0..1`.

The active packing is:

```text
bits 0..2:   chunk index inside the 8-chunk group
bits 4..11:  rolling sequence counter
bit 14:      channel, 0 = CC1, 1 = CC2
bit 15:      overflow/backpressure flag
```

The G474 stream intentionally keeps the low three chunk bits compatible with the
F072B header convention.

The overflow bit now means the software capture-bank FIFO became full before a
completed DMA half could be preserved. With the copied-bank architecture, a
zero-overflow capture indicates that DMA data was preserved before USB
packetization.

## Timestamp

Bytes `2..5` carry a 32-bit packet timestamp. It is generated from `HAL_GetTick`
plus the current SysTick sub-millisecond position, scaled to 1 MHz. Each tick of
this metadata timestamp is 1 us.

This timestamp is packet metadata. The actual edge timing data is the `uint16_t`
HRTIM capture payload. Host-side PD decoding should use the payload samples and
the 24 MHz timing scale for edge reconstruction.

## Sentinel Behavior

`sniffer_start_capture()` starts HRTIM capture DMA for both timers and then
enables HRTIM rollover DMA requests:

```c
HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].TIMxDIER |= HRTIM_TIM_DMA_RST;
HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxDIER |= HRTIM_TIM_DMA_RST;
```

The DMA source address remains the HRTIM capture register. On rollover, DMA
reads the stale capture value and places it into the same stream as edge
captures. The host can treat these repeated/stale values as timing continuity
sentinels, following the same idea as the F072B/Twinkie-style design.

## Analog/Status Packets

Inline dummy analog/status packets are not part of this framing yet. The current
G474 sniffer stream sends edge packets only on the sniffer endpoint.

Analog/status is carried on EP2 IN, not inline on the EP1 edge stream. The EP2
analog packet keeps the F072B `01 20` packet family:

| Bytes | Size | Meaning |
| --- | ---: | --- |
| `0` | 1 | marker `0x01` |
| `1` | 1 | marker `0x20` |
| `2..5` | 4 | little-endian 32-bit timestamp at 1 MHz |
| `6..7` | 2 | VBUS mV |
| `8..9` | 2 | VBUS mA |
| `10..11` | 2 | CC1 mV |
| `12..13` | 2 | CC1 mA |
| `14..15` | 2 | CC2 mV |
| `16..17` | 2 | CC2 mA |
| `18..63` | 46 | reserved |

The current dummy analog path is rate-limited to 10 ms, or 100 packets/second.
The final INA237 path should replace this timer gate with "send when a fresh
INA237 DMA snapshot is published".

## Host-Side Expectations

The host parser should:

- keep the F072B 8-byte packet header parser;
- identify edge packets using marker byte `0x13`;
- use byte `7` to recover the chunk marker/index;
- decode bytes `8..63` as 28 little-endian `uint16_t` samples;
- use a 24 MHz sample clock for captured edge values;
- handle 16-bit rollover/sentinel behavior.

The biggest host-side change from F072B is payload width, not packet framing.

For validation, prefer `--decode-at-end` when checking packet loss. Live decode
can temporarily print a `missing_eop` diagnostic for the newest SOP candidate
before enough future edge records have arrived; if the same record/offset later
prints as `CRC_OK`, that diagnostic was only an incomplete-tail artifact. A
clean offline decode with `overflow_records=0` is the stronger packet-integrity
check.
