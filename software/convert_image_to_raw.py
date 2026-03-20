#!/usr/bin/env python3
"""Convert a standard image file into packed RGB raw bytes for the JPEG pipeline."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="Source image path")
    parser.add_argument("--output", required=True, help="Destination raw RGB path")
    parser.add_argument("--width", type=int, required=True, help="Output width in pixels")
    parser.add_argument("--height", type=int, required=True, help="Output height in pixels")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if args.width <= 0 or args.height <= 0:
        raise ValueError("width and height must be positive")

    with Image.open(input_path) as image:
        rgb_image = image.convert("RGB")
        if rgb_image.size != (args.width, args.height):
            rgb_image = rgb_image.resize((args.width, args.height), Image.Resampling.LANCZOS)
        raw_bytes = rgb_image.tobytes()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(raw_bytes)

    print(f"Converted {input_path} -> {output_path}")
    print(f"Dimensions: {args.width}x{args.height}")
    print(f"Bytes     : {len(raw_bytes)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
