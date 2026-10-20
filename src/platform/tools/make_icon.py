#!/usr/bin/env python3
"""App icon for the personal-use builds, made at build time from the extracted title picture:
the llama in the middle of fs.exe's VGA title (build/generated/data/title_vga.idx8 + .pal,
tools/extract/FORMATS.md) on a rounded blue square. No game pixels are stored in the repository;
the image only exists in the build tree.

    make_icon.py --data DIR [--iconset OUT.iconset] [--favicon OUT.png]

--iconset writes the PNG sizes of a macOS .iconset (then `iconutil -c icns`), --favicon one
64 x 64 PNG for the web page. Without the title files, or if the llama is not where it should
be, a generic placeholder (a gradient square with a frame) is drawn instead.
"""
import argparse
import os
import struct
import sys
import zlib

# (file name, pixel size) as iconutil expects them.
SIZES = [(f"icon_{s}x{s}{suffix}.png", s * scale)
         for s in (16, 32, 128, 256, 512) for suffix, scale in (("", 1), ("@2x", 2))]
MASTER = 1024                       # every size is a box-filtered reduction of this
FAVICON = 64

TITLE_WIDTH, TITLE_HEIGHT = 320, 400    # newtit.lbm over newtit2.lbm (FORMATS.md)
# Where the llama stands in the 320 x 400 picture: its pixels are the brown ones (red above
# blue, green below red) in this box; the blue gradient and the yellow text around it are not.
LLAMA_BOX = (105, 160, 215, 245)    # x0, y0, x1, y1 (exclusive)
LLAMA_MIN_PIXELS = 300              # the llama has 756
# The icon draws the picture's pixels square. A 4:3 screen shows them 5:3 wide, which at icon
# size makes the llama read as a dachshund.

MARGIN, RADIUS = 0.08, 0.18         # the rounded square, in units of the icon size
LLAMA_WIDTH = 0.70


def write_png(path, size, rgba):
    rows = bytearray()
    for y in range(size):
        rows.append(0)                       # filter: none
        rows.extend(rgba[y * size * 4:(y + 1) * size * 4])

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def inside_square(u, v):
    """Whether (u, v) in [0, 1) lies in the rounded square."""
    dx = max(abs(u - 0.5) - (0.5 - MARGIN - RADIUS), 0.0)
    dy = max(abs(v - 0.5) - (0.5 - MARGIN - RADIUS), 0.0)
    return (dx * dx + dy * dy) ** 0.5 <= RADIUS


def background(u, v):
    """RGBA of the rounded square: deep blue to brighter blue, top to bottom."""
    if not inside_square(u, v):
        return (0, 0, 0, 0)
    return (int(10 + 30 * v), int(20 + 50 * v), int(90 + 130 * v), 255)


def placeholder(u, v):
    r, g, b, a = background(u, v)
    edge = max(abs(u - 0.5), abs(v - 0.5))
    if a and 0.22 < edge < 0.27:
        return (235, 235, 245, 255)
    return (r, g, b, a)


def find_llama(data_dir):
    """The llama's pixels as {(x, y): (r, g, b)}, or None."""
    try:
        with open(os.path.join(data_dir, "title_vga.idx8"), "rb") as f:
            idx = f.read()
        with open(os.path.join(data_dir, "title_vga.pal"), "rb") as f:
            pal = f.read()
    except OSError:
        return None
    if len(idx) != TITLE_WIDTH * TITLE_HEIGHT or len(pal) != 256 * 3:
        return None
    x0, y0, x1, y1 = LLAMA_BOX
    pixels = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = idx[y * TITLE_WIDTH + x]
            r, g, b = (level * 255 // 63 for level in pal[c * 3:c * 3 + 3])
            if r > b and g < r:
                pixels[(x, y)] = (r, g, b)
    return pixels if len(pixels) >= LLAMA_MIN_PIXELS else None


def render_master(llama):
    """MASTER x MASTER RGBA: the rounded square with the llama (or the placeholder)."""
    size = MASTER
    paint = background if llama else placeholder
    img = bytearray()
    for y in range(size):
        for x in range(size):
            img += bytes(paint((x + 0.5) / size, (y + 0.5) / size))
    if not llama:
        return img
    x_min, y_min = min(p[0] for p in llama), min(p[1] for p in llama)
    w = max(p[0] for p in llama) - x_min + 1
    h = max(p[1] for p in llama) - y_min + 1
    sx = sy = LLAMA_WIDTH * size / w            # icon pixels per llama pixel
    left, top = (size - w * sx) / 2, (size - h * sy) / 2
    for (px, py), rgb in llama.items():
        ax, ay = int(left + (px - x_min) * sx), int(top + (py - y_min) * sy)
        bx, by = int(left + (px - x_min + 1) * sx), int(top + (py - y_min + 1) * sy)
        row = (bytes(rgb) + b"\xff") * (bx - ax)
        for y in range(ay, by):
            img[(y * size + ax) * 4:(y * size + bx) * 4] = row
    return img


def reduce(img, size):
    """Box-filter the MASTER image to size x size (MASTER is a multiple of every size); colour
    is averaged by alpha so the transparent corners do not darken the edge."""
    k = MASTER // size
    if k == 1:
        return bytes(img)
    out = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            r = g = b = a = 0
            for yy in range(y * k, y * k + k):
                base = (yy * MASTER + x * k) * 4
                for i in range(base, base + k * 4, 4):
                    alpha = img[i + 3]
                    r += img[i] * alpha
                    g += img[i + 1] * alpha
                    b += img[i + 2] * alpha
                    a += alpha
            if a:
                o = (y * size + x) * 4
                out[o:o + 4] = bytes((r // a, g // a, b // a, a // (k * k)))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", required=True, help="the extracted data folder (generated/data)")
    ap.add_argument("--iconset", help="write a macOS .iconset folder")
    ap.add_argument("--favicon", help=f"write a {FAVICON} x {FAVICON} PNG")
    args = ap.parse_args()
    llama = find_llama(args.data)
    if llama is None:
        print("make_icon.py: no title llama in the extracted data, placeholder icon", file=sys.stderr)
    master = render_master(llama)
    if args.iconset:
        os.makedirs(args.iconset, exist_ok=True)
        for name, size in SIZES:
            write_png(os.path.join(args.iconset, name), size, reduce(master, size))
    if args.favicon:
        write_png(args.favicon, FAVICON, reduce(master, FAVICON))
    return 0


if __name__ == "__main__":
    sys.exit(main())
