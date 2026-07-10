#!/usr/bin/env python3
"""Decode DisplayPort AUX edge timing captures from STM32G474 EP2 records.

This is a bring-up diagnostic. It validates the physical/timing layer first:

  raw 64-byte records -> uint16 HRTIM edge samples -> bursts -> Manchester bits

The script accepts both the planned EP2 DP AUX packet marker (`01 40`) and the
existing EP1-style edge marker (`record[6] == 0x13`) so it can be used while the
firmware packet format is still moving.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


RECORD_SIZE = 64
EDGE_PAYLOAD_OFFSET = 8
EDGE_SAMPLE_COUNT = 28
EDGE_SAMPLE_MODULO = 1 << 16

ANALOG_CODE_WORD = 0x2001
EP1_EDGE_MARKER = 0x13
EP2_DPAUX_MARKER = 0x4001

DPAUX_REQUEST_COMMANDS = {
    0x0: "I2C_WRITE",
    0x1: "I2C_READ",
    0x2: "I2C_STATUS",
    0x4: "I2C_WRITE_MOT",
    0x5: "I2C_READ_MOT",
    0x6: "I2C_STATUS_MOT",
    0x8: "NATIVE_WRITE",
    0x9: "NATIVE_READ",
}

DPAUX_REPLY_CODES = {
    0x0: "ACK",
    0x1: "NACK",
    0x2: "DEFER",
}


@dataclass
class EdgeRecord:
    index: int
    kind: str
    timestamp_us: int
    sequence: int
    overflow: bool
    channel: int
    samples: list[int]


@dataclass
class EdgePoint:
    record_index: int
    raw: int
    time: int


@dataclass
class Burst:
    index: int
    start_record: int
    edges: list[EdgePoint]
    intervals: list[int]


@dataclass
class AuxCandidate:
    score: int
    packet_kind: str
    command: int | None
    address: int | None
    length: int | None
    polarity: int
    bit_order: str
    bit_offset: int
    preamble_byte: int | None
    preamble_count: int
    raw: bytes
    payload: bytes
    description: str


@dataclass
class DecodedBurst:
    burst: Burst
    bits: list[int]
    errors: int
    candidates: list[AuxCandidate]


def parse_int(text: str) -> int:
    return int(text, 0)


def read_le16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def read_le32(data: bytes, offset: int) -> int:
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{byte:02x}" for byte in data)


def load_records(path: Path) -> list[bytes]:
    data = path.read_bytes()
    full_len = len(data) - (len(data) % RECORD_SIZE)
    return [
        data[offset : offset + RECORD_SIZE]
        for offset in range(0, full_len, RECORD_SIZE)
    ]


def parse_edge_record(index: int, record: bytes) -> EdgeRecord | None:
    if len(record) != RECORD_SIZE:
        return None

    code = read_le16(record, 0)
    if code == ANALOG_CODE_WORD:
        return None

    kind: str
    timestamp_us: int
    sequence: int
    overflow = False
    channel = 0

    if code == EP2_DPAUX_MARKER:
        kind = "ep2_dpaux"
        timestamp_us = read_le32(record, 2)
        sequence = read_le16(record, 6)
        # EP2 DP AUX uses this word as a packet sequence/status value. Do not
        # apply the EP1 CC edge-stream overflow/channel bit interpretation here.
        overflow = False
        channel = 0
    elif record[6] == EP1_EDGE_MARKER:
        kind = "ep1_edge"
        sequence = code
        timestamp_us = read_le32(record, 2)
        overflow = bool(sequence & 0x8000)
        channel = 1 if (sequence & 0x4000) else 0
    else:
        return None

    samples = [
        read_le16(record, EDGE_PAYLOAD_OFFSET + (sample_index * 2))
        for sample_index in range(EDGE_SAMPLE_COUNT)
    ]
    return EdgeRecord(
        index=index,
        kind=kind,
        timestamp_us=timestamp_us,
        sequence=sequence,
        overflow=overflow,
        channel=channel,
        samples=samples,
    )


def build_edge_points(records: list[EdgeRecord]) -> list[EdgePoint]:
    points: list[EdgePoint] = []
    unwrapped_base = 0
    previous_raw: int | None = None
    previous_time: int | None = None

    for record in records:
        for sample in record.samples:
            if previous_raw is not None:
                if sample == previous_raw:
                    continue
                if sample < previous_raw:
                    unwrapped_base += EDGE_SAMPLE_MODULO

            time = unwrapped_base + sample
            if previous_time is not None and time == previous_time:
                previous_raw = sample
                continue

            points.append(EdgePoint(record.index, sample, time))
            previous_raw = sample
            previous_time = time

    return points


def split_bursts(points: list[EdgePoint], max_gap: int) -> list[Burst]:
    if len(points) < 2:
        return []

    bursts: list[Burst] = []
    current = [points[0]]

    for point in points[1:]:
        delta = point.time - current[-1].time
        if delta > max_gap or delta <= 0:
            if len(current) >= 2:
                bursts.append(make_burst(len(bursts), current))
            current = [point]
        else:
            current.append(point)

    if len(current) >= 2:
        bursts.append(make_burst(len(bursts), current))

    return bursts


def make_burst(index: int, edges: list[EdgePoint]) -> Burst:
    intervals = [
        edges[i + 1].time - edges[i].time for i in range(len(edges) - 1)
    ]
    return Burst(
        index=index,
        start_record=edges[0].record_index,
        edges=edges,
        intervals=intervals,
    )


def classify_interval(delta: int, args: argparse.Namespace) -> str:
    if args.half_min <= delta <= args.half_max:
        return "H"
    if args.full_min <= delta <= args.full_max:
        return "F"
    return "?"


def decode_manchester_relative(
    intervals: list[int], args: argparse.Namespace
) -> tuple[list[int], int]:
    """Decode relative Manchester bits from edge intervals.

    The first center transition is assigned bit 0. A full-bit interval means the
    next center transition belongs to the opposite bit value. A half+half pair
    means a boundary transition occurred between two equal bits.

    Return `(bits, errors)`. Invert all bits to get the opposite comparator
    polarity candidate.
    """

    bits = [0]
    errors = 0
    pos = 0

    while pos < len(intervals):
        delta = intervals[pos]

        if args.full_min <= delta <= args.full_max:
            bits.append(bits[-1] ^ 1)
            pos += 1
            continue

        if args.half_min <= delta <= args.half_max:
            if (
                pos + 1 < len(intervals)
                and args.half_min <= intervals[pos + 1] <= args.half_max
            ):
                bits.append(bits[-1])
                pos += 2
            else:
                errors += 1
                pos += 1
            continue

        errors += 1
        pos += 1

    return bits, errors


def bits_to_bytes(bits: list[int], msb_first: bool) -> bytes:
    out = bytearray()
    for offset in range(0, len(bits) - 7, 8):
        value = 0
        chunk = bits[offset : offset + 8]
        for bit_index, bit in enumerate(chunk):
            if msb_first:
                value |= bit << (7 - bit_index)
            else:
                value |= bit << bit_index
        out.append(value)
    return bytes(out)


def strip_aux_preamble(data: bytes) -> tuple[int | None, int, bytes]:
    """Strip the leading idle/preamble run from a decoded AUX byte candidate."""

    if not data:
        return None, 0, data

    preamble = data[0]
    if preamble not in (0x00, 0xFF):
        return None, 0, data

    count = 0
    for byte in data:
        if byte != preamble:
            break
        count += 1

    return preamble, count, data[count:]


def describe_aux_payload(payload: bytes) -> tuple[int, str, str] | None:
    if not payload:
        return None

    command = payload[0] >> 4
    if command in DPAUX_REQUEST_COMMANDS and len(payload) >= 4:
        address = ((payload[0] & 0x0F) << 16) | (payload[1] << 8) | payload[2]
        length = payload[3] + 1
        if length > 32:
            return None
        write_command = command in (0x0, 0x4, 0x8)
        data = payload[4 : 4 + length] if write_command else payload[4:]
        score = 100
        if payload[3] <= 0x0F:
            score += 10
        elif command in (0x0, 0x1, 0x2, 0x4, 0x5, 0x6):
            # I2C-over-AUX captures can be truncated while probing; keep these
            # visible, but rank spec-sized transfers higher.
            score -= 10
        else:
            score -= 25
        if write_command:
            missing = length - len(data)
            if missing <= 0:
                score += 15
            else:
                score -= min(50, missing * 10)

        desc = (
            f"REQUEST {DPAUX_REQUEST_COMMANDS[command]} "
            f"addr=0x{address:05x} len={length}"
        )
        if command in (0x0, 0x1, 0x2, 0x4, 0x5, 0x6):
            desc += f" i2c_addr=0x{address & 0x7F:02x}"
        if data:
            desc += f" data={hex_bytes(data)}"
        if write_command and len(data) < length:
            desc += f" truncated={length - len(data)}"
        elif not write_command and data:
            desc += " trailing_bytes"
        return score, "request", desc

    if len(payload) <= 18:
        reply_byte = payload[0]
        aux_reply = reply_byte & 0x03
        i2c_reply = (reply_byte >> 2) & 0x03
        if aux_reply in DPAUX_REPLY_CODES and i2c_reply in DPAUX_REPLY_CODES:
            data = payload[1:]
            if aux_reply != 0x00:
                desc = f"REPLY AUX_{DPAUX_REPLY_CODES[aux_reply]}"
            elif i2c_reply != 0x00:
                desc = (
                    "REPLY AUX_ACK "
                    f"I2C_{DPAUX_REPLY_CODES[i2c_reply]}"
                )
            else:
                desc = "REPLY AUX_ACK"
            if data:
                desc += f" data={hex_bytes(data)}"
            return 80, "reply", desc

    if payload[0] in DPAUX_REPLY_CODES and len(payload) <= 18:
        data = payload[1:]
        desc = f"REPLY {DPAUX_REPLY_CODES[payload[0]]}"
        if data:
            desc += f" data={hex_bytes(data)}"
        return 70, "reply", desc

    return None


def extract_aux_fields(payload: bytes, packet_kind: str) -> tuple[int | None, int | None, int | None]:
    if packet_kind != "request" or len(payload) < 4:
        return None, None, None
    command = payload[0] >> 4
    address = ((payload[0] & 0x0F) << 16) | (payload[1] << 8) | payload[2]
    length = payload[3] + 1
    return command, address, length


def find_aux_candidates(
    bits0: list[int], args: argparse.Namespace
) -> list[AuxCandidate]:
    candidates: list[AuxCandidate] = []

    for polarity, bits in ((0, bits0), (1, [bit ^ 1 for bit in bits0])):
        for msb_first, bit_order in ((True, "msb"), (False, "lsb")):
            for bit_offset in range(8):
                raw = bits_to_bytes(bits[bit_offset:], msb_first)[
                    : args.bytes_limit
                ]
                preamble_byte, preamble_count, payload = strip_aux_preamble(raw)
                described = describe_aux_payload(payload)
                if described is None:
                    continue

                score, packet_kind, description = described
                command, address, length = extract_aux_fields(payload, packet_kind)
                score += min(preamble_count, 8)
                if preamble_count < 3:
                    score -= 35
                if packet_kind == "reply" and preamble_count >= 3:
                    # Reply frames can be very short, and in current captures
                    # the responder direction can decode with the opposite
                    # idle/preamble polarity from the request direction.
                    score += 40
                elif args.aux_preamble is not None:
                    if preamble_byte == args.aux_preamble:
                        score += 50
                    elif preamble_byte is not None:
                        score -= 40
                candidates.append(
                    AuxCandidate(
                        score=score,
                        packet_kind=packet_kind,
                        command=command,
                        address=address,
                        length=length,
                        polarity=polarity,
                        bit_order=bit_order,
                        bit_offset=bit_offset,
                        preamble_byte=preamble_byte,
                        preamble_count=preamble_count,
                        raw=raw,
                        payload=payload,
                        description=description,
                    )
                )

    candidates.sort(
        key=lambda item: (
            item.score,
            item.preamble_count,
            item.polarity == 1,
            item.bit_order == "msb",
        ),
        reverse=True,
    )
    return candidates


def decode_burst(burst: Burst, args: argparse.Namespace) -> DecodedBurst:
    bits, errors = decode_manchester_relative(burst.intervals, args)
    return DecodedBurst(
        burst=burst,
        bits=bits,
        errors=errors,
        candidates=find_aux_candidates(bits, args),
    )


def format_preamble(candidate: AuxCandidate) -> str:
    if candidate.preamble_byte is None:
        return "none"
    return f"0x{candidate.preamble_byte:02x}*{candidate.preamble_count}"


def format_candidate(candidate: AuxCandidate) -> str:
    return (
        f"{candidate.description}; polarity={candidate.polarity} "
        f"order={candidate.bit_order} bit_offset={candidate.bit_offset} "
        f"preamble={format_preamble(candidate)} raw={hex_bytes(candidate.raw)}"
    )


def print_transaction_summary(
    decoded_bursts: list[DecodedBurst], args: argparse.Namespace
) -> None:
    recognized = [
        decoded
        for decoded in decoded_bursts
        if decoded.candidates and decoded.candidates[0].score >= args.min_packet_score
    ]

    print(
        f"\ndpaux_packets recognized={len(recognized)} "
        f"min_score={args.min_packet_score}"
    )
    if not recognized:
        return

    for decoded in recognized[: args.max_packets]:
        candidate = decoded.candidates[0]
        print(
            f"  burst={decoded.burst.index:04d} "
            f"record={decoded.burst.start_record:04d} "
            f"edges={len(decoded.burst.edges):03d} "
            f"errors={decoded.errors:02d} "
            f"score={candidate.score:03d} "
            f"{format_candidate(candidate)}"
        )

    print("\ndpaux_transactions:")
    pending_request: DecodedBurst | None = None
    printed = 0

    for decoded in recognized:
        candidate = decoded.candidates[0]
        if candidate.packet_kind == "request":
            if pending_request is not None and printed < args.max_transactions:
                request_candidate = pending_request.candidates[0]
                print(
                    f"  request burst={pending_request.burst.index:04d} "
                    f"unmatched: {request_candidate.description}"
                )
                printed += 1
            pending_request = decoded
            continue

        if candidate.packet_kind == "reply":
            if printed >= args.max_transactions:
                continue
            if pending_request is None:
                print(
                    f"  reply burst={decoded.burst.index:04d} "
                    f"without visible request: {candidate.description}"
                )
            else:
                request_candidate = pending_request.candidates[0]
                gap_ticks = (
                    decoded.burst.edges[0].time - pending_request.burst.edges[-1].time
                )
                gap_us = gap_ticks / 24.0
                print(
                    f"  request burst={pending_request.burst.index:04d} -> "
                    f"reply burst={decoded.burst.index:04d} "
                    f"gap={gap_us:.2f}us"
                )
                print(f"    {request_candidate.description}")
                print(f"    {candidate.description}")
                pending_request = None
            printed += 1

    if pending_request is not None and printed < args.max_transactions:
        request_candidate = pending_request.candidates[0]
        print(
            f"  request burst={pending_request.burst.index:04d} "
            f"unmatched: {request_candidate.description}"
        )


def bit_string(bits: list[int], limit: int) -> str:
    text = "".join(str(bit) for bit in bits[:limit])
    if len(bits) > limit:
        text += "..."
    return text


def print_histogram(intervals: list[int], limit: int) -> None:
    counts = Counter(intervals)
    print("interval_histogram:")
    for delta, count in counts.most_common(limit):
        print(f"  {delta:5d} ticks: {count}")


def write_interval_csv(path: Path, bursts: list[Burst], args: argparse.Namespace) -> None:
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            ["burst", "start_record", "edge_index", "delta_ticks", "class"]
        )
        for burst in bursts:
            for edge_index, delta in enumerate(burst.intervals):
                writer.writerow(
                    [
                        burst.index,
                        burst.start_record,
                        edge_index,
                        delta,
                        classify_interval(delta, args),
                    ]
                )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode DP AUX edge timing from STM32G474 64-byte records."
    )
    parser.add_argument("--in-bin", type=Path, required=True)
    parser.add_argument("--include-overflow", action="store_true")
    parser.add_argument("--channel", type=parse_int, choices=(0, 1), default=None)
    parser.add_argument("--half-min", type=parse_int, default=8)
    parser.add_argument("--half-max", type=parse_int, default=16)
    parser.add_argument("--full-min", type=parse_int, default=18)
    parser.add_argument("--full-max", type=parse_int, default=30)
    parser.add_argument("--max-gap", type=parse_int, default=80)
    parser.add_argument("--min-edges", type=parse_int, default=8)
    parser.add_argument("--hist-limit", type=parse_int, default=32)
    parser.add_argument("--max-bursts", type=parse_int, default=20)
    parser.add_argument("--bits-limit", type=parse_int, default=160)
    parser.add_argument("--bytes-limit", type=parse_int, default=32)
    parser.add_argument("--packet-candidates", type=parse_int, default=3)
    parser.add_argument("--max-packets", type=parse_int, default=80)
    parser.add_argument("--max-transactions", type=parse_int, default=40)
    parser.add_argument("--min-packet-score", type=parse_int, default=120)
    parser.add_argument("--no-transactions", action="store_true")
    parser.add_argument(
        "--aux-preamble",
        type=parse_int,
        default=0xFF,
        choices=(0x00, 0xFF),
        help="Preferred decoded AUX preamble byte for ranking candidates.",
    )
    parser.add_argument("--out-interval-csv", type=Path)
    args = parser.parse_args()

    raw_records = load_records(args.in_bin)
    edge_records = [
        parsed
        for index, record in enumerate(raw_records)
        if (parsed := parse_edge_record(index, record)) is not None
    ]
    if not args.include_overflow:
        edge_records = [record for record in edge_records if not record.overflow]
    if args.channel is not None:
        edge_records = [record for record in edge_records if record.channel == args.channel]

    points = build_edge_points(edge_records)
    bursts = [
        burst
        for burst in split_bursts(points, args.max_gap)
        if len(burst.edges) >= args.min_edges
    ]
    intervals = [delta for burst in bursts for delta in burst.intervals]

    print(
        f"records_total={len(raw_records)} edge_records={len(edge_records)} "
        f"edges={len(points)} bursts={len(bursts)}"
    )
    print(
        f"thresholds: half={args.half_min}..{args.half_max} "
        f"full={args.full_min}..{args.full_max} max_gap={args.max_gap}"
    )

    if intervals:
        half_count = sum(args.half_min <= d <= args.half_max for d in intervals)
        full_count = sum(args.full_min <= d <= args.full_max for d in intervals)
        unknown_count = len(intervals) - half_count - full_count
        print(
            f"intervals={len(intervals)} half={half_count} "
            f"full={full_count} unknown={unknown_count}"
        )
        print_histogram(intervals, args.hist_limit)

    if args.out_interval_csv:
        write_interval_csv(args.out_interval_csv, bursts, args)
        print(f"wrote {args.out_interval_csv}")

    decoded_bursts = [decode_burst(burst, args) for burst in bursts]
    if not args.no_transactions:
        print_transaction_summary(decoded_bursts, args)

    for decoded in decoded_bursts[: args.max_bursts]:
        burst = decoded.burst
        classes = "".join(classify_interval(delta, args) for delta in burst.intervals)
        bits0 = decoded.bits
        errors0 = decoded.errors
        bits1 = [bit ^ 1 for bit in bits0]
        lsb0 = bits_to_bytes(bits0, msb_first=False)[: args.bytes_limit]
        msb0 = bits_to_bytes(bits0, msb_first=True)[: args.bytes_limit]
        lsb1 = bits_to_bytes(bits1, msb_first=False)[: args.bytes_limit]
        msb1 = bits_to_bytes(bits1, msb_first=True)[: args.bytes_limit]

        print(
            f"\nburst={burst.index} start_record={burst.start_record} "
            f"edges={len(burst.edges)} intervals={len(burst.intervals)} "
            f"decoded_bits={len(bits0)} errors={errors0}"
        )
        print(f"  interval_classes={classes[:args.bits_limit]}")
        print(f"  bits polarity0={bit_string(bits0, args.bits_limit)}")
        print(f"  bits polarity1={bit_string(bits1, args.bits_limit)}")
        print(f"  bytes lsb polarity0={hex_bytes(lsb0)}")
        print(f"  bytes msb polarity0={hex_bytes(msb0)}")
        print(f"  bytes lsb polarity1={hex_bytes(lsb1)}")
        print(f"  bytes msb polarity1={hex_bytes(msb1)}")

        for candidate in decoded.candidates[: args.packet_candidates]:
            print(f"  dpaux {format_candidate(candidate)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
