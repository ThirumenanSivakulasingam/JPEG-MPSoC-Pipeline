#!/usr/bin/env python3
import argparse
import re
import sys
from pathlib import Path


BEGIN_RE = re.compile(r"JPEG_DUMP_BEGIN size=(\d+)")
DATA_RE = re.compile(r"JPEG_DUMP ([0-9A-Fa-f]+)$")
END_RE = re.compile(r"JPEG_DUMP_END size=(\d+)")


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract a JPEG binary from a cpu_5 UART dump log.")
    parser.add_argument("--log", required=True, help="Path to cpu_5 terminal log")
    parser.add_argument("--output", required=True, help="Output JPEG path")
    parser.add_argument("--expected-size", type=int, default=0, help="Optional expected JPEG size")
    args = parser.parse_args()

    log_path = Path(args.log)
    output_path = Path(args.output)

    dump_started = False
    dump_finished = False
    announced_size = None
    hex_chunks = []

    with log_path.open("r", encoding="utf-8", errors="ignore") as infile:
        for raw_line in infile:
            line = raw_line.strip()

            match = BEGIN_RE.search(line)
            if match:
                dump_started = True
                dump_finished = False
                announced_size = int(match.group(1))
                hex_chunks = []
                continue

            if not dump_started:
                continue

            match = DATA_RE.search(line)
            if match:
                hex_chunks.append(match.group(1))
                continue

            match = END_RE.search(line)
            if match:
                dump_finished = True
                if announced_size is None:
                    announced_size = int(match.group(1))
                break

    if not dump_started:
        print("No JPEG_DUMP_BEGIN marker found in terminal log.", file=sys.stderr)
        return 1

    if not dump_finished:
        print("JPEG_DUMP_END marker not found in terminal log.", file=sys.stderr)
        return 1

    hex_data = "".join(hex_chunks)
    if len(hex_data) % 2 != 0:
        print("Collected UART dump has an odd number of hex digits.", file=sys.stderr)
        return 1

    try:
        jpeg_data = bytes.fromhex(hex_data)
    except ValueError as exc:
        print(f"Failed to decode UART dump hex: {exc}", file=sys.stderr)
        return 1

    if announced_size is not None and len(jpeg_data) != announced_size:
        print(
            f"Decoded JPEG length mismatch: expected {announced_size}, got {len(jpeg_data)}.",
            file=sys.stderr,
        )
        return 1

    if args.expected_size and len(jpeg_data) != args.expected_size:
        print(
            f"Runner expected {args.expected_size} bytes, got {len(jpeg_data)} from UART dump.",
            file=sys.stderr,
        )
        return 1

    output_path.write_bytes(jpeg_data)
    print(f"Extracted {len(jpeg_data)} bytes to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
