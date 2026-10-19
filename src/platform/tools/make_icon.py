#!/usr/bin/env python3
"""Generic placeholder app icon for the personal-use macOS bundle: writes the PNG sizes of a
macOS .iconset (then `iconutil -c icns`). A plain rounded square with a gradient and a frame;
nothing taken from the game.

    make_icon.py OUT.iconset
"""
import os
import struct
import sys
import zlib

# (file name, pixel size) as iconutil expects them.
SIZES = [(f"icon_{s}x{s}{suffix}.png", s * scale)
         for s in (16, 32, 128, 256, 512) for suffix, scale in (("", 1), ("@2x", 2))]


def write_png(path, size, pixel):
    rows = bytearray()
    for y in range(size):
        rows.append(0)                       # filter: none
        for x in range(size):
            rows.extend(pixel(x / size, y / size))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def placeholder(u, v):
    """RGBA at (u, v) in [0, 1): gradient rounded square with a light frame."""
    margin, radius = 0.08, 0.18
    # Distance outside the rounded square (<= 0 inside).
    dx = max(abs(u - 0.5) - (0.5 - margin - radius), 0.0)
    dy = max(abs(v - 0.5) - (0.5 - margin - radius), 0.0)
    if (dx * dx + dy * dy) ** 0.5 > radius:
        return (0, 0, 0, 0)
    r, g, b = int(40 + 60 * v), int(30 + 40 * v), int(110 + 100 * v)
    edge = max(abs(u - 0.5), abs(v - 0.5))
    if 0.22 < edge < 0.27:
        r, g, b = 235, 235, 245
    return (r, g, b, 255)


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, size in SIZES:
        write_png(os.path.join(out, name), size, placeholder)
    return 0


if __name__ == "__main__":
    sys.exit(main())
