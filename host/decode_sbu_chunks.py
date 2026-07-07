#!/usr/bin/env python3
"""Decode raw SBU UART chunks exported by g474_pd_host."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


UART_RX_FRAME_CODEWORD = 0xAA
DLE = 0xFE
ETX = 0x40


@dataclass
class UartFrame:
    line: str
    index: int
    timestamp_us: int
    payload: bytes


def parse_hex_bytes(text: str) -> bytes:
    text = text.strip()
    if not text:
        return b""
    return bytes(int(part, 16) for part in text.split())


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def classify_sbu_transaction(payload: bytes) -> tuple[str, str]:
    if not payload:
        return "UNKNOWN", "empty"
    if payload[0] != DLE:
        return "UNKNOWN", "missing_dle"
    if len(payload) < 2:
        return "UNKNOWN", "short_dle"

    start = payload[1] >> 6
    if start == 0b10:
        if len(payload) != 3:
            return "LT", f"bad_len={len(payload)}"
        lse = payload[1]
        clse = payload[2]
        ok = ((lse ^ clse) & 0xFF) == 0xFF
        symbol = lse & 0x0F
        lane = (lse >> 5) & 0x01
        names = {
            0x0: "LT_Fall",
            0x2: "LT_Resume",
            0x3: "LT_LRoff",
        }
        return "LT", (
            f"basic_ok={int(ok)} lane={lane} "
            f"symbol={names.get(symbol, f'0x{symbol:x}')}"
        )

    if start in (0b00, 0b01):
        kind = "AT" if start == 0b00 else "RT"
        if len(payload) < 6:
            return kind, f"bad_len={len(payload)}"
        if payload[-2:] != bytes([DLE, ETX]):
            return kind, "missing_footer"
        if len(payload) < 8:
            return kind, f"bad_len={len(payload)}"

        received = payload[-4] | (payload[-3] << 8)
        calculated = crc16_modbus(payload[1:-4])
        summary = (
            f"crc_ok={int(received == calculated)} "
            f"crc_rx=0x{received:04x} crc_calc=0x{calculated:04x}"
        )
        if kind == "RT":
            summary += f" broadcast={int(bool(payload[1] & 0x20))}"
        return kind, summary

    return "UNKNOWN", f"start_bits=0b{start:02b}"


class UartFrameParser:
    def __init__(self, line: str):
        self.line = line
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[UartFrame]:
        self.buffer.extend(data)
        frames: list[UartFrame] = []

        while True:
            try:
                start = self.buffer.index(UART_RX_FRAME_CODEWORD)
            except ValueError:
                self.buffer.clear()
                break

            if start:
                del self.buffer[:start]

            if len(self.buffer) < 8:
                break

            size = self.buffer[6] | (self.buffer[7] << 8)
            total = 8 + size
            if len(self.buffer) < total:
                break

            index = self.buffer[1]
            timestamp_us = int.from_bytes(self.buffer[2:6], "little")
            payload = bytes(self.buffer[8:total])
            frames.append(
                UartFrame(
                    line=self.line,
                    index=index,
                    timestamp_us=timestamp_us,
                    payload=payload,
                )
            )
            del self.buffer[:total]

        return frames


def decode_csv(path: Path) -> list[UartFrame]:
    parsers: dict[str, UartFrameParser] = {}
    frames: list[UartFrame] = []

    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            line = row.get("line", "")
            raw_hex = row.get("raw_hex", "")
            if line not in parsers:
                parsers[line] = UartFrameParser(line)
            frames.extend(parsers[line].feed(parse_hex_bytes(raw_hex)))

    return frames


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode g474_pd_host --out-sbu-csv UART chunks"
    )
    parser.add_argument("csv", type=Path, help="sbu_chunks.csv path")
    args = parser.parse_args()

    frames = decode_csv(args.csv)
    for frame in frames:
        kind, summary = classify_sbu_transaction(frame.payload)
        raw = " ".join(f"{byte:02x}" for byte in frame.payload)
        print(
            f"{frame.line} idx=0x{frame.index:02x} "
            f"ts_us={frame.timestamp_us} type={kind} {summary} raw={raw}"
        )
    print(f"frames={len(frames)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
