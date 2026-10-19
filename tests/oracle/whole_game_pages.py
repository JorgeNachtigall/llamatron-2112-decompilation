"""Whole-game oracle, pages side: after every frame of tests/oracle/whole_game.py's lockstep
run, the original's VRAM (the EGA plane model) against the port's pages.

Compared per frame (PageComparer.hook, run_lockstep's on_frame):
  - the shown picture: the original's 8000 bytes from the CRTC start address (which the frame
    loop writes before wait_vsync 1000:e5f3, where the frame stops) against
    gfx_display_pixels(); when the start address is a page's (0000 / 2000 / 4000), also that
    the port shows that page;
  - the three pages, VRAM 0000 / 2000 / 4000 against GFX_PAGE_0 / 1 / BACKGROUND, while the
    original shows one of them. While the text scroller runs, the original shows its 64 KB
    canvas at other start addresses and the port keeps the canvas apart from its pages
    (draw_scroller.c), so then only the shown picture is compared.
  - The frame stops at frame_run's wait_vsync (the call returning to 1e7b:0196) before the
    rest of frame_run: the page swap and the erase of the new draw page (1e7b:0213..0299,
    the dirty list at CS:[6735]), which the port's frame_run has already done. So the
    original's hidden page is compared with that list applied (pending_erase); after the
    other wait_vsync callers (palette_set, the scroller's own wait) no erase follows.

A difference is reported as the first one found (shown picture first, then pages 0, 1,
background): its kind, page, bounding box, pixel count and first pixel; with dump_dir set,
both pictures of it are also written there as PNGs.
"""

from __future__ import annotations

import ctypes
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

WIDTH, HEIGHT = 320, 200
BYTES_PER_LINE = WIDTH // 8
PAGE_BYTES = BYTES_PER_LINE * HEIGHT
PLANE_SIZE = 0x10000
PIXELS = WIDTH * HEIGHT
PAGES = {0x0000: 0, 0x2000: 1, 0x4000: 2}        # VRAM offset -> GfxPageId
PAGE_NAMES = {0: "page 0 (0000)", 1: "page 1 (2000)", 2: "background (4000)"}
BACKGROUND = 0x4000

CODE0 = 0x1000
FRAME_RUN_WAIT_RETURN = (0x1E7B, 0x0196)        # far return of frame_run's wait_vsync call
CS_ERASE_LIST = 0x6735                          # the list frame_run erases after the swap
ERASE_LIST_END = 0xFE0C
ERASE_LIST_MAX = 4000                           # entries; a longer list is not a list

# Per plane p: a byte of that plane -> its 8 pixels' bit p (MSB = leftmost pixel).
_EXPAND = [[bytes(((b >> (7 - i)) & 1) << p for i in range(8)) for b in range(256)]
           for p in range(4)]


def rasterise(planes, offset: int) -> bytes:
    """Indexed pixels of the 320x200 picture at VRAM `offset` (wrapping at 64 KB), as
    EgaPlanes.rasterise but fast enough for every frame."""
    acc = 0
    for p in range(4):
        plane = planes[p]
        end = offset + PAGE_BYTES
        data = plane[offset:end] if end <= PLANE_SIZE else \
            bytes(plane[offset:]) + bytes(plane[:end - PLANE_SIZE])
        acc |= int.from_bytes(b"".join(map(_EXPAND[p].__getitem__, data)), "big")
    return acc.to_bytes(PIXELS, "big")


def read_erase_list(m) -> list[tuple[int, int, int]] | None:
    """The original's pending erase, when its frame stopped in frame_run's wait: the entries
    {count (low byte 16-pixel words, high byte != 0: one word more), rows, page-relative byte
    offset} of the list at CS:[6735] (test_draw_basic.dirty_entries), else None."""
    r = m.get_regs()
    ss, sp = r["ss"], r["sp"]
    ret = (m.read_u16(ss, (sp + 2) & 0xFFFF), m.read_u16(ss, sp))
    if ret != FRAME_RUN_WAIT_RETURN:
        return None
    at = m.read_u16(CODE0, CS_ERASE_LIST)
    out = []
    while len(out) < ERASE_LIST_MAX:
        count = m.read_u16(CODE0, at)
        if count == ERASE_LIST_END:
            return out
        out.append((count, m.read_u16(CODE0, at + 2), m.read_u16(CODE0, at + 4)))
        at = (at + 6) & 0xFFFF
    raise ValueError(f"erase list at CS:{m.read_u16(CODE0, CS_ERASE_LIST):04x} has no end")


def apply_erase(planes, page: int, entries) -> list[bytearray]:
    """Copies of the planes with 1e7b:0213..0299 done on `page`: each entry's rows of
    2 * words bytes copied from the background page (an entry's bytes run on into the next
    row past column 39, as the original's linear copy does)."""
    out = [bytearray(p) for p in planes]
    for count, rows, offset in entries:
        n = 2 * ((count & 0xFF) + (1 if count >> 8 else 0))
        for r in range(rows):
            a = offset + r * BYTES_PER_LINE
            dst, src = (page + a) & 0xFFFF, (BACKGROUND + a) & 0xFFFF
            if dst + n <= PLANE_SIZE and src + n <= PLANE_SIZE:
                for p in out:
                    p[dst:dst + n] = p[src:src + n]
            else:
                for p in out:
                    for i in range(n):
                        p[(dst + i) & 0xFFFF] = p[(src + i) & 0xFFFF]
    return out


@dataclass
class PageDiff:
    tick: int
    frame: int
    kind: str                       # "shown page", "shown pixels" or "page pixels"
    page: str
    bbox: tuple[int, int, int, int] = (0, 0, 0, 0)     # x0, y0, x1, y1 (inclusive)
    count: int = 0
    first: tuple[int, int, int, int] = (0, 0, 0, 0)    # x, y, original, port
    detail: str = ""

    def describe(self) -> str:
        if self.kind == "shown page":
            return f"frame {self.frame} (tick {self.tick}): {self.detail}"
        x0, y0, x1, y1 = self.bbox
        x, y, a, b = self.first
        return (f"frame {self.frame} (tick {self.tick}): {self.kind} differ on {self.page}: "
                f"{self.count} px in x {x0}..{x1}, y {y0}..{y1}; first at ({x}, {y}): "
                f"original {a}, port {b}")


def pixel_diff(orig: bytes, port: bytes):
    """(bbox, count, first) of the pixels that differ, or None."""
    if orig == port:
        return None
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    count = 0
    first = None
    for y in range(HEIGHT):
        row = slice(y * WIDTH, (y + 1) * WIDTH)
        a, b = orig[row], port[row]
        if a == b:
            continue
        for x in range(WIDTH):
            if a[x] != b[x]:
                count += 1
                x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
                if first is None:
                    first = (x, y, a[x], b[x])
    return (x0, y0, x1, y1), count, first


def write_png(path: Path, pixels: bytes, palette: list[tuple[int, int, int]]) -> None:
    """An 8-bit paletted PNG of a 320x200 indexed picture."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    raw = b"".join(b"\0" + pixels[y * WIDTH:(y + 1) * WIDTH] for y in range(HEIGHT))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 3, 0, 0, 0))
           + chunk(b"PLTE", b"".join(bytes(c) for c in palette))
           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    path.write_bytes(png)


# Fixed 16 colours for the dumps (the game's own palette is not needed to see a difference).
DUMP_PALETTE = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170),
                (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85),
                (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]


class PageComparer:
    def __init__(self, lib: ctypes.CDLL, dump_dir: Path | None = None):
        self._page = lib.gfx_page
        self._page.restype, self._page.argtypes = ctypes.c_void_p, [ctypes.c_int]
        self._display = lib.gfx_display_pixels
        self._display.restype, self._display.argtypes = ctypes.c_void_p, []
        self._display_id = lib.gfx_display_page_id
        self._display_id.restype, self._display_id.argtypes = ctypes.c_int, []
        self.dump_dir = dump_dir
        self.frames = 0

    def port_page(self, page_id: int) -> bytes:
        return ctypes.string_at(self._page(page_id), PIXELS)

    def port_shown(self) -> bytes:
        return ctypes.string_at(self._display(), PIXELS)

    def compare(self, ega, tick: int, frame: int, pending_erase=None) -> PageDiff | None:
        self.frames += 1
        start = ega.start_address
        orig = rasterise(ega.planes, start)
        port = self.port_shown()
        d = pixel_diff(orig, port)
        if d:
            return self._found(tick, frame, "shown pixels", f"shown picture (start {start:04x})",
                               orig, port, d)
        if start not in PAGES:
            return None                 # the scroller's canvas
        shown = self._display_id()
        if shown != PAGES[start]:
            return PageDiff(tick, frame, "shown page", PAGE_NAMES[PAGES[start]],
                            detail=f"the original shows {PAGE_NAMES[PAGES[start]]}, the port "
                                   f"{PAGE_NAMES.get(shown, shown)} (same pixels)")
        hidden = 0x2000 - start if start in (0x0000, 0x2000) else None
        for offset, page_id in PAGES.items():
            if page_id == shown:
                continue                # compared above
            planes = ega.planes
            if pending_erase and offset == hidden:
                planes = apply_erase(planes, offset, pending_erase)
            orig = rasterise(planes, offset)
            port = self.port_page(page_id)
            d = pixel_diff(orig, port)
            if d:
                return self._found(tick, frame, "page pixels", PAGE_NAMES[page_id], orig, port, d)
        return None

    def _found(self, tick, frame, kind, page, orig, port, d) -> PageDiff:
        bbox, count, first = d
        if self.dump_dir:
            self.dump_dir.mkdir(parents=True, exist_ok=True)
            stem = f"frame{frame:05d}_{kind.replace(' ', '_')}"
            write_png(self.dump_dir / f"{stem}_original.png", orig, DUMP_PALETTE)
            write_png(self.dump_dir / f"{stem}_port.png", port, DUMP_PALETTE)
        return PageDiff(tick, frame, kind, page, bbox, count, first)

    def hook(self, orig, port, tick: int, frame: int):
        """run_lockstep's on_frame: a whole_game.Result for the first difference."""
        diff = self.compare(orig.ega, tick, frame, read_erase_list(orig.m))
        if diff is None:
            return None
        from whole_game import Result
        return Result(False, tick, frame, "pages", detail=diff.describe())
