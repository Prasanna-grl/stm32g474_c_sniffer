# STM32G474 PD Sniffer Tools

These tools are adapted from the STM32F072B sniffer utilities.

The G474 firmware keeps the F072B-style 8-byte EP1 record header, but the edge
payload is different:

```text
bytes 0..7:   F072B-compatible header
bytes 8..63:  28 little-endian uint16_t HRTIM samples
sample clock: 24 MHz
packet timestamp: 1 MHz, bytes 2..5
```

## Capture EP1 Records

The capture tool defaults to this firmware's active USB descriptor:

```text
VID: 0x227f
PID: 0x0009
Product: Thunder
Endpoint: 0x81
Interface: 0
```

Read from the sniffer endpoint and save raw 64-byte records:

```sh
python3 tools/ep1_chunk_plot.py \
  --max-records 4096 \
  --read-size 512 \
  --out-bin capture_ep1.bin \
  --out-records-txt capture_ep1.txt \
  --out-headers-txt capture_headers.txt \
  --analyze
```

Analyze an existing binary capture:

```sh
python3 tools/ep1_chunk_plot.py --in-bin capture_ep1.bin --analyze
```

## Decode PD Packets

Decode from a binary capture:

```sh
python3 tools/pd_edge_decode.py --in-bin capture_ep1.bin
```

Decode from a text dump:

```sh
python3 tools/pd_edge_decode.py --in-text capture_ep1.txt
```

The default decoder thresholds are scaled for the G474 24 MHz HRTIM capture
clock:

```text
half bit: 30..50 ticks
full bit: 70..100 ticks
max gap:  400 ticks
```

Use `--half-min`, `--half-max`, `--full-min`, `--full-max`, and `--max-gap` to
tune those values for a specific board or capture.
