#!/usr/bin/env python3
"""
Read STM32G474 PD sniffer EP1 IN data and split it into 64-byte records.

The host USB driver may complete reads as 64, 512, 576, 16384, or other byte
counts. This script ignores the URB grouping and analyzes the logical 64-byte
records that the firmware sends.

The G474 sniffer keeps the STM32F072B-style 8-byte header, while the edge
payload is 28 little-endian uint16_t HRTIM samples.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from pathlib import Path


DEFAULT_VID = 0x227F
DEFAULT_PID = 0x0009
DEFAULT_INTERFACE = 0
DEFAULT_ENDPOINT = 0x81
RECORD_SIZE = 64
ANALOG_CODE_WORD = 0x2001
ANALOG_PAYLOAD_OFF = 6
ANALOG_PAYLOAD_SIZE = 12
EDGE_PAYLOAD_OFF = 8
EDGE_SAMPLE_SIZE = 2


def parse_int(text: str) -> int:
    return int(text, 0)


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def decode_analog(record: bytes) -> dict[str, int] | None:
    if len(record) < ANALOG_PAYLOAD_OFF + ANALOG_PAYLOAD_SIZE:
        return None

    payload = record[
        ANALOG_PAYLOAD_OFF : ANALOG_PAYLOAD_OFF + ANALOG_PAYLOAD_SIZE
    ]
    return {
        "vbus_mv": int.from_bytes(payload[0:2], "little", signed=False),
        "vbus_ma": int.from_bytes(payload[2:4], "little", signed=True),
        "cc1_mv": int.from_bytes(payload[4:6], "little", signed=False),
        "cc1_ma": int.from_bytes(payload[6:8], "little", signed=True),
        "cc2_mv": int.from_bytes(payload[8:10], "little", signed=False),
        "cc2_ma": int.from_bytes(payload[10:12], "little", signed=True),
    }


def format_analog(values: dict[str, int] | None) -> str:
    if values is None:
        return ""

    return (
        f"vbus={values['vbus_mv']}mV/{values['vbus_ma']}mA "
        f"cc1={values['cc1_mv']}mV/{values['cc1_ma']}mA "
        f"cc2={values['cc2_mv']}mV/{values['cc2_ma']}mA"
    )


def classify_record(record: bytes) -> str:
    if len(record) != RECORD_SIZE:
        return "short"
    if int.from_bytes(record[0:2], "little") == ANALOG_CODE_WORD:
        return "tail"
    if record[6] == 0x13:
        return "edge"
    return "unknown"


def edge_seq(record: bytes) -> int:
    return int.from_bytes(record[0:2], "little")


def edge_group_key(record: bytes) -> tuple[int, int, int]:
    seq = edge_seq(record)
    channel = 1 if (seq & 0x4000) else 0
    seq_page = (seq & 0x0FF0) >> 4
    timestamp = int.from_bytes(record[2:6], "little")
    return channel, seq_page, timestamp


def edge_sample_preview(record: bytes, count: int = 8) -> str:
    values: list[str] = []
    payload = record[EDGE_PAYLOAD_OFF:RECORD_SIZE]
    for offset in range(0, min(len(payload), count * EDGE_SAMPLE_SIZE), EDGE_SAMPLE_SIZE):
        if offset + EDGE_SAMPLE_SIZE > len(payload):
            break
        sample = int.from_bytes(payload[offset : offset + EDGE_SAMPLE_SIZE], "little")
        values.append(f"{sample:04x}")
    return " ".join(values)


def describe_record(index: int, record: bytes, elapsed_s: float) -> dict[str, object]:
    kind = classify_record(record)
    timestamp = int.from_bytes(record[2:6], "little") if len(record) >= 6 else 0

    if kind == "edge":
        seq = edge_seq(record)
        chunk = seq & 0x07
        channel = 1 if (seq & 0x4000) else 0
        marker = f"{record[6]:02x} {record[7]:02x}"
        payload_preview = edge_sample_preview(record)
        detail = (
            f"seq=0x{seq:04x} ch=CC{channel + 1} chunk={chunk} "
            f"ts=0x{timestamp:08x} marker={marker} samples16={payload_preview}"
        )
    elif kind == "tail":
        analog_values = decode_analog(record)
        analog = hex_bytes(
            record[ANALOG_PAYLOAD_OFF : ANALOG_PAYLOAD_OFF + ANALOG_PAYLOAD_SIZE]
        )
        payload_preview = hex_bytes(
            record[
                ANALOG_PAYLOAD_OFF
                + ANALOG_PAYLOAD_SIZE : ANALOG_PAYLOAD_OFF
                + ANALOG_PAYLOAD_SIZE
                + 16
            ]
        )
        detail = (
            f"code=0x{ANALOG_CODE_WORD:04x} ts=0x{timestamp:08x} "
            f"analog_raw={analog} {format_analog(analog_values)}"
        )
    else:
        payload_preview = hex_bytes(record[:16])
        detail = f"header={hex_bytes(record[:8])}"

    analog_values = decode_analog(record) if kind == "tail" else None
    row = {
        "index": index,
        "elapsed_ms": elapsed_s * 1000.0,
        "kind": kind,
        "timestamp": timestamp,
        "header": hex_bytes(record[:8]),
        "detail": detail,
        "preview": payload_preview,
        "analog_raw": "",
        "vbus_mv": "",
        "vbus_ma": "",
        "cc1_mv": "",
        "cc1_ma": "",
        "cc2_mv": "",
        "cc2_ma": "",
    }
    if analog_values is not None:
        row["analog_raw"] = hex_bytes(
            record[ANALOG_PAYLOAD_OFF : ANALOG_PAYLOAD_OFF + ANALOG_PAYLOAD_SIZE]
        )
        row.update(analog_values)

    return row


def print_record(row: dict[str, object]) -> None:
    print(
        f"{row['index']:06d} {row['elapsed_ms']:10.3f} ms "
        f"{row['kind']:<7} {row['header']}  {row['detail']}"
    )


def open_device(args: argparse.Namespace) -> usb.core.Device:
    import usb.core
    import usb.util

    dev = usb.core.find(idVendor=args.vid, idProduct=args.pid)
    if dev is None:
        raise SystemExit(
            f"Device not found: vid=0x{args.vid:04x} pid=0x{args.pid:04x}"
        )

    dev.set_configuration()

    try:
        if dev.is_kernel_driver_active(args.interface):
            dev.detach_kernel_driver(args.interface)
    except (NotImplementedError, usb.core.USBError):
        pass

    usb.util.claim_interface(dev, args.interface)
    return dev


def close_device(dev: object, interface: int) -> None:
    import usb.util

    usb.util.release_interface(dev, interface)


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "index",
                "elapsed_ms",
                "kind",
                "timestamp",
                "header",
                "detail",
                "preview",
                "analog_raw",
                "vbus_mv",
                "vbus_ma",
                "cc1_mv",
                "cc1_ma",
                "cc2_mv",
                "cc2_ma",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)


def write_records_txt(path: Path, records: list[bytes]) -> None:
    with path.open("w") as f:
        for index, record in enumerate(records):
            f.write(f"Chunk {index:06d}: {hex_bytes(record)}\n")


def write_headers_txt(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w") as f:
        for row in rows:
            f.write(
                f"Header {row['index']:06d}: {row['header']}  "
                f"{row['kind']}  {row['detail']}\n"
            )


def write_analog_txt(path: Path, rows: list[dict[str, object]]) -> None:
    fields = ["vbus_mv", "vbus_ma", "cc1_mv", "cc1_ma", "cc2_mv", "cc2_ma"]

    with path.open("w") as f:
        f.write(
            "index timestamp analog_raw "
            "vbus_mv vbus_ma cc1_mv cc1_ma cc2_mv cc2_ma\n"
        )
        for row in rows:
            if row["kind"] != "tail":
                continue

            values = " ".join(str(row[field]) for field in fields)
            f.write(
                f"{row['index']} 0x{row['timestamp']:08x} "
                f"{row['analog_raw']} {values}\n"
            )


def records_from_bytes(data: bytes) -> tuple[list[bytes], bytes]:
    full_len = len(data) - (len(data) % RECORD_SIZE)
    records = [
        data[offset : offset + RECORD_SIZE]
        for offset in range(0, full_len, RECORD_SIZE)
    ]
    return records, data[full_len:]


def load_bin(path: Path, max_records: int) -> tuple[list[bytes], bytes]:
    records, partial = records_from_bytes(path.read_bytes())
    if max_records:
        kept = records[:max_records]
        if len(records) > max_records:
            partial = b""
        return kept, partial
    return records, partial


def load_text_records(path: Path, max_records: int) -> list[bytes]:
    records: list[bytes] = []
    chunk_re = re.compile(r"^Chunk\s+\d+:\s+(.+)$")

    for line in path.read_text().splitlines():
        match = chunk_re.match(line.strip())
        if not match:
            continue

        values = bytes(int(byte, 16) for byte in match.group(1).split())
        if len(values) != RECORD_SIZE:
            continue

        records.append(values)
        if max_records and len(records) >= max_records:
            break

    return records


def analyze_records(records: list[bytes]) -> list[str]:
    lines: list[str] = []
    counts: dict[str, int] = {}
    for record in records:
        kind = classify_record(record)
        counts[kind] = counts.get(kind, 0) + 1

    lines.append(f"records={len(records)} counts={counts}")

    unknown = [
        (idx, record[:8])
        for idx, record in enumerate(records)
        if classify_record(record) == "unknown"
    ]
    if unknown:
        lines.append(f"unknown_records={len(unknown)}")
        for idx, header in unknown[:10]:
            lines.append(f"  unknown[{idx}] header={hex_bytes(header)}")
    else:
        lines.append("unknown_records=0")

    group_errors: list[str] = []
    analog_after_edge_groups = 0
    analog_other = 0
    edge_group_count = 0
    active_key: tuple[int, int, int] | None = None
    active_indices: list[int] = []
    active_chunks: list[int] = []
    last_edge_ts: int | None = None
    ts_regressions: list[str] = []

    def finish_group(next_idx: int) -> None:
        nonlocal active_key, active_indices, active_chunks, edge_group_count
        if active_key is None:
            return

        edge_group_count += 1
        expected = list(range(8))
        if active_chunks != expected:
            group_errors.append(
                "edge_group_error "
                f"records={active_indices[0]}..{active_indices[-1]} "
                f"key={active_key} chunks={active_chunks}"
            )

        active_key = None
        active_indices = []
        active_chunks = []

    for idx, record in enumerate(records):
        kind = classify_record(record)

        if kind == "edge":
            ts = int.from_bytes(record[2:6], "little")
            if last_edge_ts is not None and ts < last_edge_ts:
                ts_regressions.append(
                    f"timestamp_regression record={idx} "
                    f"prev=0x{last_edge_ts:08x} now=0x{ts:08x}"
                )
            last_edge_ts = ts

            key = edge_group_key(record)
            chunk = edge_seq(record) & 0x07

            if active_key is None:
                active_key = key
                active_indices = [idx]
                active_chunks = [chunk]
            elif key == active_key:
                active_indices.append(idx)
                active_chunks.append(chunk)
            else:
                finish_group(idx)
                active_key = key
                active_indices = [idx]
                active_chunks = [chunk]

        elif kind == "tail":
            if active_key is not None:
                finish_group(idx)
                analog_after_edge_groups += 1
            else:
                analog_other += 1

    finish_group(len(records))

    lines.append(f"edge_groups={edge_group_count}")
    lines.append(f"edge_group_errors={len(group_errors)}")
    for error in group_errors[:20]:
        lines.append(f"  {error}")

    lines.append(f"analog_after_edge_group={analog_after_edge_groups}")
    lines.append(f"analog_without_open_edge_group={analog_other}")

    lines.append(f"timestamp_regressions={len(ts_regressions)}")
    for regression in ts_regressions[:10]:
        lines.append(f"  {regression}")

    return lines


def plot_records(path: Path | None, records: list[bytes], show: bool) -> None:
    try:
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError as exc:
        raise SystemExit(
            "Plotting needs matplotlib and numpy. Install them or omit --plot."
        ) from exc

    if not records:
        print("No records to plot.")
        return

    matrix = np.array([list(r) for r in records], dtype=np.uint8)
    height = max(4, min(14, len(records) * 0.18))

    fig, ax = plt.subplots(figsize=(12, height))
    image = ax.imshow(matrix, aspect="auto", interpolation="nearest", cmap="viridis")
    ax.set_title("EP1 IN 64-byte records")
    ax.set_xlabel("Byte offset within 64-byte record")
    ax.set_ylabel("Record index")
    ax.set_xticks(range(0, RECORD_SIZE, 4))
    fig.colorbar(image, ax=ax, label="byte value")
    fig.tight_layout()

    if path is not None:
        fig.savefig(path, dpi=150)
        print(f"Wrote plot: {path}")
    if show:
        plt.show()
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read EP1 IN and analyze logical 64-byte sniffer records."
    )
    parser.add_argument("--vid", type=parse_int, default=DEFAULT_VID)
    parser.add_argument("--pid", type=parse_int, default=DEFAULT_PID)
    parser.add_argument("--interface", type=parse_int, default=DEFAULT_INTERFACE)
    parser.add_argument("--endpoint", type=parse_int, default=DEFAULT_ENDPOINT)
    parser.add_argument(
        "--read-size",
        type=parse_int,
        default=512,
        help="Host read request size. Use 64, 512, 576, or 16384 for comparisons.",
    )
    parser.add_argument("--timeout-ms", type=int, default=1000)
    parser.add_argument("--max-records", type=int, default=256)
    parser.add_argument("--duration", type=float, default=0.0)
    parser.add_argument("--out-bin", type=Path)
    parser.add_argument("--out-csv", type=Path)
    parser.add_argument(
        "--out-records-txt",
        type=Path,
        help="Write each logical 64-byte EP1 record as a hex text line.",
    )
    parser.add_argument(
        "--out-headers-txt",
        type=Path,
        help="Write each 8-byte record header plus decoded summary text.",
    )
    parser.add_argument(
        "--out-analog-txt",
        type=Path,
        help="Write decoded analog records as plain text.",
    )
    parser.add_argument("--plot", type=Path)
    parser.add_argument("--show", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument(
        "--in-bin",
        type=Path,
        help="Analyze an existing raw EP1 binary dump instead of reading USB.",
    )
    parser.add_argument(
        "--in-text",
        type=Path,
        help="Analyze a text dump with 'Chunk NNN: xx xx ...' lines.",
    )
    parser.add_argument(
        "--analyze",
        action="store_true",
        help="Print stream continuity checks for 64-byte records.",
    )
    args = parser.parse_args()

    if args.read_size < RECORD_SIZE:
        raise SystemExit("--read-size must be at least 64")

    rows: list[dict[str, object]] = []
    records: list[bytes] = []
    started = time.monotonic()
    partial = bytearray()

    if args.in_bin and args.in_text:
        raise SystemExit("Use only one of --in-bin or --in-text")

    if args.in_bin:
        loaded, trailing = load_bin(args.in_bin, args.max_records)
        records.extend(loaded)
        partial.extend(trailing)
        for index, record in enumerate(records):
            row = describe_record(index, record, 0.0)
            rows.append(row)
            if not args.quiet:
                print_record(row)
    elif args.in_text:
        records.extend(load_text_records(args.in_text, args.max_records))
        for index, record in enumerate(records):
            row = describe_record(index, record, 0.0)
            rows.append(row)
            if not args.quiet:
                print_record(row)
    else:
        dev = open_device(args)
        print(
            f"Reading EP 0x{args.endpoint:02x}, interface {args.interface}, "
            f"read_size={args.read_size}, record_size={RECORD_SIZE}"
        )

        raw_out = args.out_bin.open("wb") if args.out_bin else None

        try:
            while len(records) < args.max_records:
                if args.duration and (time.monotonic() - started) >= args.duration:
                    break

                try:
                    data = bytes(dev.read(args.endpoint, args.read_size, args.timeout_ms))
                except Exception as exc:
                    if exc.__class__.__name__ == "USBTimeoutError":
                        print("timeout")
                        continue
                    raise

                if raw_out:
                    raw_out.write(data)

                partial.extend(data)
                while len(partial) >= RECORD_SIZE and len(records) < args.max_records:
                    record = bytes(partial[:RECORD_SIZE])
                    del partial[:RECORD_SIZE]

                    elapsed = time.monotonic() - started
                    row = describe_record(len(records), record, elapsed)
                    rows.append(row)
                    records.append(record)

                    if not args.quiet:
                        print_record(row)

        except KeyboardInterrupt:
            print("\nInterrupted.")
        finally:
            if raw_out:
                raw_out.close()
            close_device(dev, args.interface)

    counts: dict[str, int] = {}
    for row in rows:
        counts[row["kind"]] = counts.get(row["kind"], 0) + 1

    print(f"Records: {len(records)}  Counts: {counts}")
    if partial:
        print(f"Trailing partial bytes not analyzed: {len(partial)}")

    if args.analyze:
        print("Analysis:")
        for line in analyze_records(records):
            print(line)

    if args.out_csv:
        write_csv(args.out_csv, rows)
        print(f"Wrote CSV: {args.out_csv}")

    if args.out_records_txt:
        write_records_txt(args.out_records_txt, records)
        print(f"Wrote 64-byte records TXT: {args.out_records_txt}")

    if args.out_headers_txt:
        write_headers_txt(args.out_headers_txt, rows)
        print(f"Wrote 8-byte headers TXT: {args.out_headers_txt}")

    if args.out_analog_txt:
        write_analog_txt(args.out_analog_txt, rows)
        print(f"Wrote analog TXT: {args.out_analog_txt}")

    if args.plot or args.show:
        plot_records(args.plot, records, args.show)

    return 0


if __name__ == "__main__":
    sys.exit(main())
