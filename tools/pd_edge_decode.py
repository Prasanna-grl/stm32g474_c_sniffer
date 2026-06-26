#!/usr/bin/env python3
"""
Decode USB-PD packets from STM32G474 EP1 edge sample records.

This is a diagnostic decoder. It intentionally keeps the capture transport
separate from PD decoding so firmware overflow markers can be compared against
what the edge samples actually contain.

The STM32G474 sniffer keeps the STM32F072B 8-byte record header, but bytes
8..63 carry 28 little-endian uint16_t HRTIM capture samples at 24 MHz.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


RECORD_SIZE = 64
ANALOG_CODE_WORD = 0x2001
EDGE_PAYLOAD_OFFSET = 8
EDGE_SAMPLE_SIZE = 2
EDGE_SAMPLE_MODULO = 1 << 16
DEFAULT_SAMPLE_CLOCK_HZ = 24_000_000

SYNC1 = 0x18
SYNC2 = 0x11
SYNC3 = 0x06
RST1 = 0x07
RST2 = 0x19
EOP = 0x0D
SOP = (SYNC1, SYNC1, SYNC1, SYNC2)
SOP_PRIME = (SYNC1, SYNC1, SYNC3, SYNC3)
SOP_DPRIME = (SYNC1, SYNC3, SYNC1, SYNC3)

FIVEB_TO_NIBBLE = {
    0x1E: 0x0,
    0x09: 0x1,
    0x14: 0x2,
    0x15: 0x3,
    0x0A: 0x4,
    0x0B: 0x5,
    0x0E: 0x6,
    0x0F: 0x7,
    0x12: 0x8,
    0x13: 0x9,
    0x16: 0xA,
    0x17: 0xB,
    0x1A: 0xC,
    0x1B: 0xD,
    0x1C: 0xE,
    0x1D: 0xF,
}

CONTROL_MESSAGE_TYPES = {
    0x01: "GoodCRC",
    0x02: "GotoMin",
    0x03: "Accept",
    0x04: "Reject",
    0x05: "Ping",
    0x06: "PS_RDY",
    0x07: "Get_Source_Cap",
    0x08: "Get_Sink_Cap",
    0x09: "DR_Swap",
    0x0A: "PR_Swap",
    0x0B: "VCONN_Swap",
    0x0C: "Wait",
    0x0D: "Soft_Reset",
    0x10: "Not_Supported",
    0x11: "Get_Source_Cap_Extended",
    0x12: "Get_Status",
    0x13: "FR_Swap",
    0x14: "Get_PPS_Status",
    0x15: "Get_Country_Codes",
    0x16: "Get_Sink_Cap_Extended",
}

DATA_MESSAGE_TYPES = {
    0x01: "Source_Capabilities",
    0x02: "Request",
    0x03: "BIST",
    0x04: "Sink_Capabilities",
    0x05: "Battery_Status",
    0x06: "Alert",
    0x07: "Get_Country_Info",
    0x0F: "Vendor_Defined",
}


@dataclass
class EdgeRecord:
    index: int
    channel: int
    seq_page: int
    overflow: bool
    timestamp: int
    chunk: int
    samples: bytes


@dataclass
class Packet:
    channel: int
    bit_offset: int
    symbol_index: int
    record_index: int | None
    ordered_set: str
    payload: bytes
    symbols: list[int]
    crc_ok: bool


def parse_int(text: str) -> int:
    return int(text, 0)


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def classify_record(record: bytes) -> str:
    if len(record) != RECORD_SIZE:
        return "short"
    if int.from_bytes(record[0:2], "little") == ANALOG_CODE_WORD:
        return "tail"
    if record[6] == 0x13:
        return "edge"
    return "unknown"


def edge_record(index: int, record: bytes) -> EdgeRecord:
    seq = int.from_bytes(record[0:2], "little")
    return EdgeRecord(
        index=index,
        channel=1 if (seq & 0x4000) else 0,
        seq_page=(seq & 0x0FF0) >> 4,
        overflow=bool(seq & 0x8000),
        timestamp=int.from_bytes(record[2:6], "little"),
        chunk=seq & 0x07,
        samples=record[EDGE_PAYLOAD_OFFSET:64],
    )


def records_from_bin(path: Path, start: int | None, end: int | None) -> list[bytes]:
    data = path.read_bytes()
    full_len = len(data) - (len(data) % RECORD_SIZE)
    records = [
        data[offset : offset + RECORD_SIZE]
        for offset in range(0, full_len, RECORD_SIZE)
    ]
    return records[slice(start, None if end is None else end + 1)]


def records_from_text(path: Path, start: int | None, end: int | None) -> list[tuple[int, bytes]]:
    chunk_re = re.compile(r"^Chunk\s+(\d+):\s+(.+)$")
    records: list[tuple[int, bytes]] = []
    for line in path.read_text().splitlines():
        match = chunk_re.match(line.strip())
        if not match:
            continue
        index = int(match.group(1))
        if start is not None and index < start:
            continue
        if end is not None and index > end:
            continue
        data = bytes(int(byte, 16) for byte in match.group(2).split())
        if len(data) == RECORD_SIZE:
            records.append((index, data))
    return records


def load_records(args: argparse.Namespace) -> list[tuple[int, bytes]]:
    if args.in_bin and args.in_text:
        raise SystemExit("Use only one of --in-bin or --in-text")
    if not args.in_bin and not args.in_text:
        raise SystemExit("Provide --in-bin or --in-text")

    if args.in_text:
        return records_from_text(args.in_text, args.start, args.end)

    loaded = records_from_bin(args.in_bin, args.start, args.end)
    base = args.start or 0
    return [(base + index, record) for index, record in enumerate(loaded)]


def build_edge_streams(
    indexed_records: list[tuple[int, bytes]], include_overflow: bool
) -> dict[int, list[EdgeRecord]]:
    streams: dict[int, list[EdgeRecord]] = {0: [], 1: []}
    for index, record in indexed_records:
        if classify_record(record) != "edge":
            continue

        edge = edge_record(index, record)
        if edge.overflow and not include_overflow:
            continue
        streams[edge.channel].append(edge)
    return streams


def samples_for_stream(records: list[EdgeRecord]) -> tuple[list[int], list[int]]:
    samples: list[int] = []
    owners: list[int] = []
    for record in records:
        for offset in range(0, len(record.samples) - 1, EDGE_SAMPLE_SIZE):
            sample = int.from_bytes(record.samples[offset : offset + 2], "little")
            samples.append(sample)
            owners.append(record.index)
    return samples, owners


def drop_consecutive_duplicates(
    samples: list[int], owners: list[int]
) -> tuple[list[int], list[int]]:
    if not samples:
        return [], []

    filtered_samples = [samples[0]]
    filtered_owners = [owners[0]]
    for sample, owner in zip(samples[1:], owners[1:]):
        if sample == filtered_samples[-1]:
            continue
        filtered_samples.append(sample)
        filtered_owners.append(owner)
    return filtered_samples, filtered_owners


def decode_edges_to_bits(
    samples: list[int],
    owners: list[int],
    half_min: int,
    half_max: int,
    full_min: int,
    full_max: int,
    max_gap: int,
) -> tuple[list[int], list[int]]:
    bits: list[int] = []
    bit_owners: list[int] = []
    pending_half_owner: int | None = None

    for prev, cur, owner in zip(samples, samples[1:], owners[1:]):
        delta = (cur - prev) & (EDGE_SAMPLE_MODULO - 1)

        if delta > max_gap:
            pending_half_owner = None
            continue

        if half_min <= delta <= half_max:
            if pending_half_owner is None:
                pending_half_owner = owner
            else:
                bits.append(1)
                bit_owners.append(pending_half_owner)
                pending_half_owner = None
            continue

        if full_min <= delta <= full_max:
            pending_half_owner = None
            bits.append(0)
            bit_owners.append(owner)
            continue

        pending_half_owner = None

    return bits, bit_owners


def bits_to_symbols(bits: list[int], owners: list[int], offset: int) -> tuple[list[int], list[int]]:
    symbols: list[int] = []
    symbol_owners: list[int] = []
    for pos in range(offset, len(bits) - 4, 5):
        value = 0
        for bit_index in range(5):
            value |= bits[pos + bit_index] << bit_index
        symbols.append(value)
        symbol_owners.append(owners[pos])
    return symbols, symbol_owners


def ordered_set_name(symbols: tuple[int, int, int, int]) -> str | None:
    if symbols == SOP:
        return "SOP"
    if symbols == SOP_PRIME:
        return "SOP'"
    if symbols == SOP_DPRIME:
        return 'SOP"'
    if symbols == (RST1, RST1, RST1, RST2):
        return "Hard_Reset"
    if symbols == (RST1, RST1, RST1, RST1):
        return "Cable_Reset"
    return None


def decode_payload_symbols(data_symbols: list[int]) -> bytes | None:
    nibbles: list[int] = []
    for symbol in data_symbols:
        nibble = FIVEB_TO_NIBBLE.get(symbol)
        if nibble is None:
            return None
        nibbles.append(nibble)

    if len(nibbles) % 2:
        return None

    payload = bytearray()
    for index in range(0, len(nibbles), 2):
        payload.append(nibbles[index] | (nibbles[index + 1] << 4))
    return bytes(payload)


def crc_ok(payload: bytes) -> bool:
    if len(payload) < 6:
        return False
    expected = struct.unpack("<I", payload[-4:])[0]
    actual = zlib.crc32(payload[:-4]) & 0xFFFFFFFF
    return actual == expected


def find_packets(
    channel: int, bits: list[int], owners: list[int], include_crc_bad: bool
) -> list[Packet]:
    packets: list[Packet] = []

    for offset in range(5):
        symbols, symbol_owners = bits_to_symbols(bits, owners, offset)
        index = 0
        while index <= len(symbols) - 5:
            name = ordered_set_name(tuple(symbols[index : index + 4]))
            if name is None:
                index += 1
                continue

            if name in {"Hard_Reset", "Cable_Reset"}:
                packets.append(
                    Packet(
                        channel=channel,
                        bit_offset=offset,
                        symbol_index=index,
                        record_index=symbol_owners[index],
                        ordered_set=name,
                        payload=b"",
                        symbols=symbols[index : index + 4],
                        crc_ok=True,
                    )
                )
                index += 4
                continue

            eop_index: int | None = None
            for scan in range(index + 4, min(index + 4 + 100, len(symbols))):
                if symbols[scan] == EOP:
                    eop_index = scan
                    break

            if eop_index is None:
                index += 1
                continue

            payload = decode_payload_symbols(symbols[index + 4 : eop_index])
            if payload is not None:
                ok = crc_ok(payload)
                if ok or include_crc_bad:
                    packets.append(
                        Packet(
                            channel=channel,
                            bit_offset=offset,
                            symbol_index=index,
                            record_index=symbol_owners[index],
                            ordered_set=name,
                            payload=payload,
                            symbols=symbols[index : eop_index + 1],
                            crc_ok=ok,
                        )
                    )

            index = eop_index + 1

    return packets


def message_name(payload: bytes) -> str:
    if len(payload) < 2:
        return "Reset"

    header = int.from_bytes(payload[:2], "little")
    message_type = header & 0x1F
    object_count = (header >> 12) & 0x07
    if object_count:
        return DATA_MESSAGE_TYPES.get(message_type, f"Data_0x{message_type:02x}")
    return CONTROL_MESSAGE_TYPES.get(message_type, f"Control_0x{message_type:02x}")


def header_summary(payload: bytes) -> str:
    if len(payload) < 2:
        return ""

    header = int.from_bytes(payload[:2], "little")
    msg_type = header & 0x1F
    port_data_role = (header >> 5) & 0x01
    spec_rev = (header >> 6) & 0x03
    port_power_role = (header >> 8) & 0x01
    msg_id = (header >> 9) & 0x07
    object_count = (header >> 12) & 0x07
    ext = (header >> 15) & 0x01
    return (
        f"hdr=0x{header:04x} type=0x{msg_type:02x} rev={spec_rev} "
        f"id={msg_id} objs={object_count} pr={port_power_role} "
        f"dr={port_data_role} ext={ext}"
    )


def print_packet(packet: Packet) -> None:
    status = "CRC_OK" if packet.crc_ok else "CRC_BAD"
    payload_hex = hex_bytes(packet.payload)
    print(
        f"CC{packet.channel + 1} rec={packet.record_index} "
        f"offset={packet.bit_offset} sym={packet.symbol_index} "
        f"{packet.ordered_set} {status} {message_name(packet.payload)} "
        f"{header_summary(packet.payload)} bytes={payload_hex}"
    )


def print_stream_summary(channel: int, records: list[EdgeRecord], bits: list[int]) -> None:
    overflow = sum(1 for record in records if record.overflow)
    bad_order = 0
    previous: EdgeRecord | None = None
    for record in records:
        if previous is not None:
            expected = (previous.chunk + 1) & 0x07
            if record.chunk != expected and record.chunk != 0:
                bad_order += 1
        previous = record

    print(
        f"CC{channel + 1}: edge_records={len(records)} "
        f"overflow_records={overflow} decoded_bits={len(bits)} "
        f"chunk_order_warnings={bad_order}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode USB-PD packets from EP1 edge sample records."
    )
    parser.add_argument("--in-bin", type=Path)
    parser.add_argument("--in-text", type=Path)
    parser.add_argument("--start", type=int)
    parser.add_argument("--end", type=int)
    parser.add_argument(
        "--include-overflow",
        action="store_true",
        help="Use edge groups whose firmware sequence word has the overflow bit set.",
    )
    parser.add_argument(
        "--show-crc-bad",
        action="store_true",
        help="Print packets with valid framing but failing CRC.",
    )
    parser.add_argument(
        "--sample-clock",
        type=parse_int,
        default=DEFAULT_SAMPLE_CLOCK_HZ,
        help="Capture sample clock in Hz. Default: 24 MHz for STM32G474 HRTIM.",
    )
    parser.add_argument("--half-min", type=parse_int, default=30)
    parser.add_argument("--half-max", type=parse_int, default=50)
    parser.add_argument("--full-min", type=parse_int, default=70)
    parser.add_argument("--full-max", type=parse_int, default=100)
    parser.add_argument("--max-gap", type=parse_int, default=400)
    args = parser.parse_args()

    indexed_records = load_records(args)
    print(
        f"sample_clock={args.sample_clock}Hz sample_width={EDGE_SAMPLE_SIZE * 8} "
        f"half={args.half_min}..{args.half_max} "
        f"full={args.full_min}..{args.full_max} max_gap={args.max_gap}"
    )
    streams = build_edge_streams(indexed_records, args.include_overflow)

    all_packets: list[Packet] = []
    for channel, records in streams.items():
        samples, owners = samples_for_stream(records)
        samples, owners = drop_consecutive_duplicates(samples, owners)
        bits, bit_owners = decode_edges_to_bits(
            samples,
            owners,
            args.half_min,
            args.half_max,
            args.full_min,
            args.full_max,
            args.max_gap,
        )
        print_stream_summary(channel, records, bits)
        all_packets.extend(find_packets(channel, bits, bit_owners, args.show_crc_bad))

    all_packets.sort(key=lambda packet: (packet.record_index or -1, packet.channel))
    print(f"packets={len(all_packets)} crc_ok={sum(1 for p in all_packets if p.crc_ok)}")

    for packet in all_packets:
        print_packet(packet)

    return 0


if __name__ == "__main__":
    sys.exit(main())
