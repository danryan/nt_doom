#!/usr/bin/env python3
"""Capture the disting NT's screen via firmware SysEx.

Sends F0 00 21 27 6D <sysex_id> 0x01 F7 and decodes the 0x33 reply.
Returns 16384 bytes, one byte per pixel, 4-bit grayscale (0..15), row-major,
256 px wide x 64 px tall.

Usage:
    python3 harness/scripts/nt_screenshot.py [--out PATH] [--pgm PATH] [--timeout 3.0]
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import mido


SCREEN_W = 256
SCREEN_H = 64
PAYLOAD_BYTES = SCREEN_W * SCREEN_H  # 16384

EXPERT_SLEEPERS_MANUF = bytes([0x00, 0x21, 0x27])
DISTING_NT_PREFIX = 0x6D
REQ_TAKE_SCREENSHOT = 0x01
RESP_SCREENSHOT = 0x33


def build_request(sysex_id: int = 0) -> bytes:
    return bytes(
        [0xF0] + list(EXPERT_SLEEPERS_MANUF) + [DISTING_NT_PREFIX, sysex_id & 0x7F, REQ_TAKE_SCREENSHOT, 0xF7]
    )


def parse_response(raw: bytes) -> bytes:
    if raw[:1] != b"\xF0" or raw[-1:] != b"\xF7":
        raise ValueError(f"missing F0/F7 framing: {raw[:1].hex()}..{raw[-1:].hex()}")
    if raw[1:4] != EXPERT_SLEEPERS_MANUF:
        raise ValueError(f"manufacturer mismatch: {raw[1:4].hex()}")
    if raw[4] != DISTING_NT_PREFIX:
        raise ValueError(f"prefix mismatch: {raw[4]:#x}")
    if raw[6] != RESP_SCREENSHOT:
        raise ValueError(f"unexpected opcode {raw[6]:#x}")
    payload = raw[7:-1]
    # Firmware response is 16385 bytes between header and F7. nt_helper treats
    # the first 16384 as pixel data and ignores the trailing byte. Match that.
    if len(payload) < PAYLOAD_BYTES:
        raise ValueError(f"payload len {len(payload)} < {PAYLOAD_BYTES}")
    return payload[:PAYLOAD_BYTES]


def find_port(direction: str, substr: str) -> str:
    func = mido.get_input_names if direction == "input" else mido.get_output_names
    ports = func()
    matches = [p for p in ports if substr.lower() in p.lower()]
    if not matches:
        raise SystemExit(f"no {direction} port matching {substr!r}; available: {ports}")
    return matches[0]


def capture(port_substr: str = "disting NT", timeout: float = 3.0, sysex_id: int = 0) -> bytes:
    in_name = find_port("input", port_substr)
    out_name = find_port("output", port_substr)
    with mido.open_input(in_name) as in_port, mido.open_output(out_name) as out_port:
        for _ in in_port.iter_pending():
            pass
        out_port.send(mido.Message.from_bytes(list(build_request(sysex_id))))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for m in in_port.iter_pending():
                if m.type == "sysex":
                    raw = bytes([0xF0] + list(m.data) + [0xF7])
                    if len(raw) >= 8 and raw[6] == RESP_SCREENSHOT:
                        return parse_response(raw)
            time.sleep(0.01)
    raise TimeoutError(f"no screenshot reply within {timeout}s")


def write_pgm(payload: bytes, path: Path) -> None:
    """Write a PGM (P5) image, mapping 0..15 grayscale to 0..255."""
    header = f"P5\n{SCREEN_W} {SCREEN_H}\n255\n".encode()
    body = bytes((b * 17) & 0xFF for b in payload)  # 15 * 17 = 255
    path.write_bytes(header + body)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, help="raw 16384-byte capture path")
    p.add_argument("--pgm", type=Path, help="P5 PGM rendering for human eyeballing")
    p.add_argument("--timeout", type=float, default=3.0)
    p.add_argument("--port-substr", default="disting NT")
    args = p.parse_args()

    payload = capture(args.port_substr, args.timeout)
    print(f"captured {len(payload)} bytes", file=sys.stderr)

    if args.out:
        args.out.write_bytes(payload)
        print(f"wrote raw: {args.out}", file=sys.stderr)
    if args.pgm:
        write_pgm(payload, args.pgm)
        print(f"wrote pgm: {args.pgm}", file=sys.stderr)
    if not args.out and not args.pgm:
        nz = sum(1 for b in payload if b)
        uniq = sorted(set(payload))
        print(f"nonzero pixels: {nz}/{len(payload)}, unique grey levels: {uniq}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
