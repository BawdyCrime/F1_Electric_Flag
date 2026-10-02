#!/usr/bin/env python3
"""Convert a PNG logo to a little-endian RGB565 raw image for LVGL."""

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Source image")
    parser.add_argument("output", type=Path, help="Destination .rgb565 file")
    parser.add_argument("--height", type=int, required=True, help="Output height in pixels")
    parser.add_argument("--width", type=int, help="Output canvas width in pixels")
    args = parser.parse_args()

    if args.height <= 0:
        parser.error("--height must be greater than zero")
    if args.width is not None and args.width <= 0:
        parser.error("--width must be greater than zero")
    if not args.input.is_file():
        parser.error(f"input file does not exist: {args.input}")

    convert = shutil.which("magick") or shutil.which("convert")
    identify = shutil.which("identify")
    if convert is None or identify is None:
        parser.error("ImageMagick is required (install the magick/convert and identify commands)")

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        resized_png = temp_path / "resized.png"
        rgb_path = temp_path / "pixels.rgb"

        convert_args = [
            convert,
            str(args.input),
            "-trim",
            "+repage",
            "-resize",
            f"{args.width}x{args.height}" if args.width is not None else f"x{args.height}",
        ]
        if args.width is not None:
            convert_args.extend(
                ["-background", "black", "-gravity", "center", "-extent", f"{args.width}x{args.height}"]
            )
        convert_args.extend(
            [
                "-background",
                "black",
                "-alpha",
                "remove",
                "-alpha",
                "off",
                "-depth",
                "8",
                f"PNG24:{resized_png}",
            ]
        )
        subprocess.run(convert_args, check=True)
        dimensions = subprocess.check_output(
            [identify, "-format", "%w %h", str(resized_png)], text=True
        )
        width, height = map(int, dimensions.split())

        subprocess.run(
            [convert, str(resized_png), "-depth", "8", f"RGB:{rgb_path}"],
            check=True,
        )
        rgb = rgb_path.read_bytes()
        expected_size = width * height * 3
        if len(rgb) != expected_size:
            raise RuntimeError(f"expected {expected_size} RGB bytes, got {len(rgb)}")

        rgb565 = bytearray(width * height * 2)
        for source_index in range(0, len(rgb), 3):
            red, green, blue = rgb[source_index : source_index + 3]
            pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            target_index = (source_index // 3) * 2
            rgb565[target_index] = pixel & 0xFF
            rgb565[target_index + 1] = pixel >> 8

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(rgb565)
    print(f"Wrote {args.output}: {width}x{height}, {len(rgb565)} bytes (RGB565 little-endian)")


if __name__ == "__main__":
    main()