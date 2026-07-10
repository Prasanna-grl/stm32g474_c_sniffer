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

## Planned SBU Hardware Mode-Select

CC capture stays active independently of the SBU mode. The SBU lines are routed
to both a UART receive path and a comparator tap, but firmware enables only one
SBU receiver mode at a time. The default state keeps every SBU-related STM32
pin in analog high-Z.

```text
                               STM32G474

                 USB4 UART path                 DP AUX differential path
              enabled only in USB4 mode        enabled only in DP_AUX mode

DUT SBU1 node ──┬──[Rtap 470R..2.2k]──► PC11 / UART4_RX
                │
                └──[AC coupling + bias]──► PC1 / COMP3_INP (+)

DUT SBU2 node ──┬──[Rtap 470R..2.2k]──► PD2  / UART5_RX
                │
                └──[AC coupling + bias]──► PC0 / COMP3_INM (-)

COMP3_OUT ───────────────────► HRTIM external event ─► capture DMA
```

The SBU connection should be a passive high-impedance tap from the DUT SBU
node, not a low-impedance drive path. Each branch should have its own small
series resistor close to the tap point so the UART pin and comparator pin do
not directly add fault current or pad capacitance to the DUT line. The UART
branch can use a lower value because the USART input is a digital receiver. The
COMP3 branch is described in the AC-coupled SBU / DP AUX appendix below.

For bench bring-up, start conservatively:

```text
SBU1 node -> 1k or 2.2k -> PC11 UART4_RX
SBU1 node -> 100nF AC coupling + 1.65 V bias -> PC1 COMP3_INP

SBU2 node -> 1k or 2.2k -> PD2 UART5_RX
SBU2 node -> 100nF AC coupling + 1.65 V bias -> PC0 COMP3_INM
```

If UART edges are too slow at 1 Mbps, reduce only the UART branch resistor
toward `470R..1k`. If DP AUX comparator edges are noisy or rounded, tune the
AC-coupling, bias, protection, and layout described in the appendix. Keep all
tap wires short.

Protection should also be placed as a tap-side network, not as a heavy clamp
directly across SBU1/SBU2:

```text
DUT SBU node -> series Rtap -> STM32 pin
                         |
                         +-> low-capacitance ESD clamp to GND/chassis
```

Use low-capacitance ESD/TVS protection for a real board. Avoid high-capacitance
signal diodes directly on the SBU node for DP AUX validation, because their
capacitance can become the dominant load. Series `Rtap` limits accidental fault
current into STM32 pad clamps, but it is not a substitute for proper Type-C ESD
and over-voltage protection.

The intended mode behavior is:

```text
SBU_IDLE:
  UART4/UART5 receivers disabled
  COMP3 disabled
  PC11, PD2, PC1, PC0 configured analog/no-pull high-Z

USB4_UART:
  PC11 and PD2 configured as UART RX alternate-function pins
  COMP3 disabled
  PC1 and PC0 left analog/no-pull high-Z

DP_AUX:
  PC11 and PD2 configured analog/no-pull high-Z
  PC1 and PC0 used as COMP3 differential inputs
  COMP3 output routed internally to HRTIM capture
```

This means SBU traffic is mutually exclusive at the receiver level: USB4 UART
capture and DP AUX capture are selected by host/API command, while CC1/CC2 edge
capture continues in parallel. For first bring-up, AUX polarity should be kept
raw and corrected in the host decoder; firmware can later invert `COMP3` output
polarity based on CC orientation once the timing stream is validated.

Endpoint ownership for this plan is:

```text
EP1 IN: CC1/CC2 edge stream, current F072B-compatible packet family
EP2 IN: auxiliary/status stream selected by packet header
```

EP2 can carry the existing analog/status packet family, USB4 SBU data, or DP AUX
edge packets. The active EP2 payload type is identified by its packet header, so
the host can demultiplex mixed or mode-selected EP2 traffic.

```text
EP2 analog/status: existing F072B-style `01 20` packet family
EP2 USB4 SBU:      existing or planned USB4 SBU packet family
EP2 DP AUX:        planned AUX edge packet family
```

The planned EP2 packet framing for DP AUX keeps the same timing payload shape
as EP1, but uses the EP2 packet family marker in bytes `0..1`:

| Bytes | Size | Meaning |
| --- | ---: | --- |
| `0..1` | 2 | DP AUX packet marker, `0x01 0x40` |
| `2..5` | 4 | little-endian 32-bit packet timestamp at 1 MHz |
| `6..7` | 2 | little-endian sequence/channel/overflow word |
| `8..63` | 56 | 28 little-endian `uint16_t` HRTIM samples |

Bytes `6..7` replace the EP1 edge marker/chunk-marker fields for DP AUX. The
host should identify the packet as DP AUX from bytes `0..1`, then use the
sequence word and ordered `uint16_t` capture values to unwrap edge timing and
Manchester-decode the AUX stream.

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

EP2 IN is the auxiliary/status endpoint. It can carry analog/status alone,
analog/status plus USB4 SBU packets, or DP AUX packets selected by an explicit
EP2 header. The host must demultiplex EP2 by packet marker before handing a
packet to the analog/status, USB4 SBU, or DP AUX decoder.

The existing dummy analog packet keeps the F072B `01 20` packet family:

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

## Appendix: AC-Coupled SBU / DP AUX Tap

This is a clearer version of the proposed SBU front-end. The hardware is still
passive: both SBU lines are tapped, then firmware chooses whether the STM32G474
listens with UART receivers or with `COMP3`.

The important mental model is:

```text
SBU1 raw node -> direct protected UART tap -> PC11 / UART4_RX
SBU1 raw node -> AC-coupled + biased AUX tap -> PC1  / COMP3_INP

SBU2 raw node -> direct protected UART tap -> PD2  / UART5_RX
SBU2 raw node -> AC-coupled + biased AUX tap -> PC0  / COMP3_INM
```

### Per-Line Schematic

`SBU1` and `SBU2` use the same circuit. Only the STM32 pin names change.

```text
SBUx from Type-C
   |
   +--[R_uart 100R..1k]---------------------> STM32 UART RX pin
   |                                           SBU1: PC11 / UART4_RX
   |                                           SBU2: PD2  / UART5_RX
   |
   +--|| C_ac 100nF --o--[R_comp 1k]--------> STM32 COMP3 input
                    AUX_BIAS_NODE             SBU1: PC1 / COMP3_INP
                         |                    SBU2: PC0 / COMP3_INM
                         |
                         +--[R_bias 100k]----> V_BIAS = 1.65 V
                         |
                         +-- low-cap ESD / clamp network
                             to GND and 3.3 V, placed near STM32 pin
```

The two comparator inputs form the DP AUX differential receiver:

```text
SBU1 AC-coupled biased node -> PC1 / COMP3_INP (+)
SBU2 AC-coupled biased node -> PC0 / COMP3_INM (-)

COMP3_OUT -> HRTIM external event -> capture DMA -> EP2 DP AUX packets
```

The local bias rail is shared by both AC-coupled AUX inputs:

```text
Nucleo 3.3 V --[10k]--o--[10k]-- GND
                      |
                      +---- V_BIAS = 1.65 V
                      |
                    [10uF]
                      |
                     GND
```

### BOM

| Reference | Value | Qty | Purpose |
| --- | --- | ---: | --- |
| `C_ac1`, `C_ac2` | 100 nF ceramic | 2 | AC-couple the DP AUX waveform and remove the original SBU DC offset. |
| `R_comp1`, `R_comp2` | 1k typical | 2 | Limit transient current into `PC1`/`PC0` and isolate the comparator pins. |
| `R_bias1`, `R_bias2` | 100k typical | 2 | Bias the AC-coupled AUX inputs to the local 1.65 V midpoint. |
| `R_uart1`, `R_uart2` | 100R..1k | 2 | Series protection/damping for the direct UART RX taps. |
| `R_div1`, `R_div2` | 10k, 1% | 2 | Generate the 1.65 V local bias from the Nucleo 3.3 V rail. |
| `C_filter` | 10 uF | 1 | Filter the 1.65 V bias node. |
| ESD/clamp parts | low-capacitance type | as needed | Protect STM32 pins without heavily loading SBU/DP AUX. |

The earlier wide sketch showed four `100k` bias resistors, two per SBU line.
For this AC-coupled single-ended bias point, one `100k` resistor from each
post-capacitor AUX node to `V_BIAS` is the clearer implementation. The `10k /
10k` divider plus `10uF` capacitor creates the shared low-noise midpoint.

### Mode Behavior

In `USB4_UART` mode:

- `PC11` and `PD2` are configured as UART RX alternate-function pins.
- `COMP3` is disabled.
- `PC1` and `PC0` stay analog/no-pull.
- The AC-coupled branch presents mostly capacitor leakage plus small pin/ESD
  capacitance to the SBU line.

In `DP_AUX` mode:

- `PC11` and `PD2` are configured analog/no-pull high-Z.
- `PC1` and `PC0` are used as `COMP3` differential inputs.
- `C_ac1` and `C_ac2` strip away the raw SBU DC offsets.
- `R_bias1` and `R_bias2` re-center both AUX waveforms around 1.65 V.
- `COMP3` compares `PC1 - PC0` and feeds edge timing into HRTIM capture.

This gives software-defined receiver selection without relays or manual
rewiring. CC capture remains independent and can continue while SBU mode is
changed by host/API command.

### Hardware Review Notes

- The direct UART taps are not AC-coupled. They are only safe if the raw SBU
  voltage range is within the STM32 input limits, or if an external level/protect
  stage is added.
- Use low-capacitance ESD parts for the final board. BAT54-style Schottky clamps
  can work for bench experiments, but their capacitance may disturb DP AUX if
  placed directly on the SBU node.
- Keep the SBU tap wiring short. On a breadboard, expect more capacitance,
  ringing, and common-mode pickup than on a controlled PCB.
- Treat the component values as bring-up starting points. If UART edges are too
  slow at 1 Mbps, lower only `R_uart`. If AUX comparator edges are rounded or
  noisy, tune `R_comp`, bias impedance, and physical layout.
