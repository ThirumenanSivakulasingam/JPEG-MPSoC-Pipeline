#!/usr/bin/env python3
"""Build an SREC image for the SDRAM parameter block plus raw RGB payload."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


SDRAM_BASE = 0x02400000
DEFAULT_DATA_OFFSET = 0x100
BYTES_PER_PIXEL = 3

COMMON_DIMS = [
    (160, 120),
    (176, 144),
    (320, 240),
    (352, 288),
    (640, 480),
    (800, 600),
    (1024, 768),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", required=True, help="Path to the raw RGB input file")
    parser.add_argument("--output", required=True, help="Destination SREC path")
    parser.add_argument("--width", type=int, help="Image width in pixels")
    parser.add_argument("--height", type=int, help="Image height in pixels")
    parser.add_argument("--quality", type=int, default=1024, help="JPEG quality factor")
    parser.add_argument("--image-format", type=int, default=4, help="RGB=4 for this firmware")
    parser.add_argument("--base-addr", type=lambda s: int(s, 0), default=SDRAM_BASE)
    parser.add_argument(
        "--data-offset",
        type=lambda s: int(s, 0),
        default=DEFAULT_DATA_OFFSET,
        help="Byte offset from SDRAM base where raw pixel data begins",
    )
    return parser.parse_args()


def infer_dimensions(raw_size: int) -> tuple[int, int]:
    candidates = [
        (width, height)
        for width, height in COMMON_DIMS
        if width * height * BYTES_PER_PIXEL == raw_size
    ]
    if len(candidates) == 1:
        return candidates[0]
    raise ValueError(
        "could not infer width/height from raw size; pass --width and --height explicitly"
    )


def s3_record(address: int, data: bytes) -> str:
    count = 4 + len(data) + 1
    body = struct.pack(">B", count) + struct.pack(">I", address) + data
    checksum = (~sum(body)) & 0xFF
    return "S3" + body.hex().upper() + f"{checksum:02X}"


def s7_record(start_address: int) -> str:
    count = 4 + 1
    body = struct.pack(">B", count) + struct.pack(">I", start_address)
    checksum = (~sum(body)) & 0xFF
    return "S7" + body.hex().upper() + f"{checksum:02X}"


def build_payload(raw_bytes: bytes, width: int, height: int, quality: int, image_format: int, data_offset: int) -> bytes:
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    expected = width * height * BYTES_PER_PIXEL
    if len(raw_bytes) != expected:
        raise ValueError(
            f"raw file size mismatch: expected {expected} bytes for {width}x{height} RGB, got {len(raw_bytes)}"
        )
    if data_offset < 16:
        raise ValueError("data offset must leave room for the 16-byte parameter block")

    header = struct.pack("<IIII", quality, image_format, width, height)
    padding = b"\x00" * (data_offset - len(header))
    return header + padding + raw_bytes


def write_srec(output_path: Path, base_address: int, payload: bytes) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["S0030000FC"]
    chunk_size = 32
    for offset in range(0, len(payload), chunk_size):
        chunk = payload[offset : offset + chunk_size]
        lines.append(s3_record(base_address + offset, chunk))
    lines.append(s7_record(base_address))
    output_path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    args = parse_args()

    raw_path = Path(args.raw)
    output_path = Path(args.output)

    if not raw_path.is_file():
        raise FileNotFoundError(f"raw input not found: {raw_path}")

    raw_bytes = raw_path.read_bytes()

    width = args.width
    height = args.height
    if width is None or height is None:
        width, height = infer_dimensions(len(raw_bytes))

    payload = build_payload(
        raw_bytes=raw_bytes,
        width=width,
        height=height,
        quality=args.quality,
        image_format=args.image_format,
        data_offset=args.data_offset,
    )
    write_srec(output_path, args.base_addr, payload)

    print(f"Built {output_path}")
    print(f"Raw input : {raw_path}")
    print(f"Dimensions: {width}x{height}")
    print(f"Quality   : {args.quality}")
    print(f"Bytes     : {len(payload)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
