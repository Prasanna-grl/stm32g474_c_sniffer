# STM32G474 PD Host CLI

This is the first C++ host-side tool for the STM32G474 PD sniffer.

All commands below assume you are already inside this directory:

```sh
cd host
```

It uses `libusb-1.0` to read EP1 and the same layered decode model as the Python
diagnostic tool:

```text
USB/read bytes -> 64-byte records -> edge stream -> BMC/4b5b -> PD packets
```

The current implementation supports:

- offline decode from an EP1 raw binary capture;
- live EP1 read from VID `0x227f`, PID `0x0009`, endpoint `0x81`;
- live EP2 analog read from endpoint `0x82`;
- raw live capture output for PD and analog streams;
- single combined PD/analog CSV output for plotting;
- optional analog-only CSV output for debugging;
- decoded PD message printing.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Offline Decode

Decode a capture stored in this `host/` directory:

```sh
./build/g474_pd_host --in-bin capture_ep1.bin
```

Decode a capture produced by the Python tool in `../tools/`:

```sh
./build/g474_pd_host --in-bin ../tools/capture_ep1.bin
```

Decode an existing EP2 analog capture:

```sh
./build/g474_pd_host \
  --in-ana-bin capture_ep2.bin \
  --out-csv timeline.csv
```

Create one combined CSV from existing EP1 and EP2 raw captures:

```sh
./build/g474_pd_host \
  --in-bin capture_ep1.bin \
  --in-ana-bin capture_ep2.bin \
  --out-csv timeline.csv
```

## Live Capture

Read EP1 live, decode while reading, and write the raw EP1 stream:

```sh
./build/g474_pd_host \
  --pd \
  --records 4096 \
  --read-size 512 \
  --out-raw capture_ep1.bin
```

Read EP2 analog only and write raw plus CSV:

```sh
./build/g474_pd_host \
  --ana \
  --records 4096 \
  --read-size 512 \
  --out-ana-raw capture_ep2.bin \
  --out-csv timeline.csv
```

Read PD and analog together:

```sh
./build/g474_pd_host \
  --all \
  --records 4096 \
  --read-size 512 \
  --out-raw capture_ep1.bin \
  --out-ana-raw capture_ep2.bin \
  --out-csv timeline.csv
```

`timeline.csv` is intended for charts. It contains both analog samples and PD
packet events on the same `timestamp_us` axis:

```text
timestamp_us,timestamp,event,cc,record,message,crc_ok,summary,payload_hex,analog_format,vbus_mv,...
```

For plotting, use analog rows for voltage/current traces and PD rows as event
markers/annotations at the same timestamp.

For the cleanest chart file, capture raw EP1/EP2 first, then combine them
offline. The offline combine step sorts the final CSV by timestamp:

```sh
./build/g474_pd_host \
  --in-bin capture_ep1.bin \
  --in-ana-bin capture_ep2.bin \
  --out-csv timeline.csv
```

Longer capture:

```sh
./build/g474_pd_host \
  --pd \
  --records 50000 \
  --read-size 512 \
  --out-raw capture_long_ep1.bin
```

Show packets with bad CRC too:

```sh
./build/g474_pd_host \
  --in-bin capture_ep1.bin \
  --show-crc-bad \
  --out-csv timeline_pd_only.csv
```

Override USB IDs if firmware descriptors change:

```sh
./build/g474_pd_host \
  --vid 0x227f \
  --pid 0x0009 \
  --endpoint 0x81 \
  --ana-endpoint 0x82 \
  --interface 0
```

If USB permissions block access, run once with `sudo` to confirm it is a
permission issue.

```sh
sudo ./build/g474_pd_host --pd --records 4096 --out-raw capture_ep1.bin
```

## Current Defaults

```text
VID:        0x227f
PID:        0x0009
Interface:  0
PD EP:      0x81
Analog EP:  0x82
Read size:  512 bytes
Record:     64 bytes
Samples:    28 x uint16_t per edge record
Clock:      24 MHz edge sample clock
```

## Stream Modes

```text
--pd    EP1 PD edge stream only. This is the default.
--ana   EP2 analog stream only.
--all   EP1 PD and EP2 analog together.
```

The current analog parser expects the F0-compatible 64-byte analog packet:

```text
0      marker 0x01
1      marker 0x20
2..5   timestamp at 1 MHz
6..7   VBUS mV
8..9   VBUS mA
10..11 CC1 mV
12..13 CC1 mA
14..15 CC2 mV
16..17 CC2 mA
```

The PD summary prints `active_cc=CCx source=edge_stream`. This is based on EP1
edge records/decoded bits, not analog measurements.

The current firmware dummy analog stream is rate-limited to 10 ms, so expect
about 100 analog packets per second until the INA237 DMA snapshot path replaces
the dummy source.
