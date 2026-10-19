"""Python model of the EGA/VGA planar memory path for mode 0Dh (320x200x16).

Test-oracle code only (docs/PLAN.md §2.3): the original blitters run in unicorn with
the A000 window mapped to this model, then the planes are rasterised to indexed
pixels and compared with the port's framebuffer page.

Modelled: four 64 KB planes, the four latches, Sequencer map mask (SEQ 2), Graphics
Controller set/reset (0), enable set/reset (1), colour compare (2), data rotate +
logical function (3), read map select (4), mode (5: write mode 0..3, read mode 0/1),
colour don't care (7), bit mask (8).  CRTC registers are stored (start address =
displayed page).  Not modelled: odd/even and chain-4 addressing, attribute
controller / DAC (palette work goes through INT 10h in game86 and is logged there).
"""

from __future__ import annotations

PLANE_SIZE = 0x10000
BYTES_PER_LINE = 40          # 320 px / 8
WIDTH, HEIGHT = 320, 200
PAGE_BYTES = BYTES_PER_LINE * HEIGHT   # 8000; game86 pages at 0x0000, 0x2000, 0x4000

SEQ_INDEX, SEQ_DATA = 0x3C4, 0x3C5
GC_INDEX, GC_DATA = 0x3CE, 0x3CF
CRTC_INDEX, CRTC_DATA = 0x3D4, 0x3D5

GC_SET_RESET, GC_ENABLE_SET_RESET, GC_COLOR_COMPARE, GC_ROTATE = 0, 1, 2, 3
GC_READ_MAP, GC_MODE, GC_MISC, GC_COLOR_DONT_CARE, GC_BIT_MASK = 4, 5, 6, 7, 8
SEQ_MAP_MASK = 2


def _expand(bit: int) -> int:
    return 0xFF if bit else 0x00


def _ror8(v: int, n: int) -> int:
    n &= 7
    return ((v >> n) | (v << (8 - n))) & 0xFF


class EgaPlanes:
    def __init__(self):
        self.planes = [bytearray(PLANE_SIZE) for _ in range(4)]
        self.latch = [0, 0, 0, 0]
        self.seq = bytearray(8)
        self.gc = bytearray(9)
        self.crtc = bytearray(0x19)
        self.seq_index = 0
        self.gc_index = 0
        self.crtc_index = 0
        self.set_mode_0d()

    def set_mode_0d(self) -> None:
        """Register state after INT 10h AX=000Dh, VRAM cleared."""
        for p in self.planes:
            p[:] = bytes(PLANE_SIZE)
        self.latch = [0, 0, 0, 0]
        self.seq[:] = bytes(8)
        self.seq[SEQ_MAP_MASK] = 0x0F
        self.gc[:] = bytes(9)
        self.gc[GC_MISC] = 0x05
        self.gc[GC_COLOR_DONT_CARE] = 0x0F
        self.gc[GC_BIT_MASK] = 0xFF
        self.crtc[:] = bytes(len(self.crtc))

    # ---- port I/O ----------------------------------------------------------

    def port_out(self, port: int, value: int, size: int) -> bool:
        """Handle an OUT; returns False if the port is not ours.  A word OUT to an
        index port writes the index (low byte) then the data (high byte)."""
        if port in (SEQ_INDEX, GC_INDEX, CRTC_INDEX) and size == 2:
            self.port_out(port, value & 0xFF, 1)
            self.port_out(port + 1, (value >> 8) & 0xFF, 1)
            return True
        value &= 0xFF
        if port == SEQ_INDEX:
            self.seq_index = value & 7
        elif port == SEQ_DATA:
            self.seq[self.seq_index] = value
        elif port == GC_INDEX:
            self.gc_index = value & 0x0F
        elif port == GC_DATA:
            if self.gc_index < len(self.gc):
                self.gc[self.gc_index] = value
        elif port == CRTC_INDEX:
            self.crtc_index = value & 0x1F
        elif port == CRTC_DATA:
            if self.crtc_index < len(self.crtc):
                self.crtc[self.crtc_index] = value
        else:
            return False
        return True

    def port_in(self, port: int) -> int | None:
        if port == SEQ_DATA:
            return self.seq[self.seq_index]
        if port == GC_DATA:
            return self.gc[self.gc_index] if self.gc_index < len(self.gc) else 0xFF
        if port == CRTC_DATA:
            return self.crtc[self.crtc_index] if self.crtc_index < len(self.crtc) else 0xFF
        if port in (SEQ_INDEX, GC_INDEX, CRTC_INDEX):
            return {SEQ_INDEX: self.seq_index, GC_INDEX: self.gc_index,
                    CRTC_INDEX: self.crtc_index}[port]
        return None

    # ---- CPU access to the A000 window --------------------------------------

    def read(self, off: int) -> int:
        off &= 0xFFFF
        self.latch = [p[off] for p in self.planes]
        if self.gc[GC_MODE] & 0x08:                 # read mode 1: colour compare
            cc, dc = self.gc[GC_COLOR_COMPARE], self.gc[GC_COLOR_DONT_CARE]
            diff = 0
            for p in range(4):
                if dc >> p & 1:
                    diff |= self.latch[p] ^ _expand(cc >> p & 1)
            return ~diff & 0xFF
        return self.latch[self.gc[GC_READ_MAP] & 3]

    def write(self, off: int, data: int) -> None:
        off &= 0xFFFF
        data &= 0xFF
        mode = self.gc[GC_MODE] & 3
        rot = self.gc[GC_ROTATE] & 7
        func = (self.gc[GC_ROTATE] >> 3) & 3
        mask = self.gc[GC_BIT_MASK]
        sr, esr = self.gc[GC_SET_RESET], self.gc[GC_ENABLE_SET_RESET]
        map_mask = self.seq[SEQ_MAP_MASK] & 0x0F
        for p in range(4):
            if not map_mask >> p & 1:
                continue
            latch = self.latch[p]
            if mode == 1:
                self.planes[p][off] = latch
                continue
            if mode == 0:
                v = _expand(sr >> p & 1) if esr >> p & 1 else _ror8(data, rot)
                m = mask
            elif mode == 2:
                v = _expand(data >> p & 1)
                m = mask
            else:                                   # mode 3 (VGA)
                v = _expand(sr >> p & 1)
                m = _ror8(data, rot) & mask
            if func == 1:
                v &= latch
            elif func == 2:
                v |= latch
            elif func == 3:
                v ^= latch
            self.planes[p][off] = (v & m) | (latch & ~m & 0xFF)

    # unicorn mmio callbacks (offset relative to A0000; size 1, 2 or 4)
    def mmio_read(self, _uc, offset: int, size: int, _user) -> int:
        return sum(self.read(offset + i) << (8 * i) for i in range(size))

    def mmio_write(self, _uc, offset: int, size: int, value: int, _user) -> None:
        for i in range(size):
            self.write(offset + i, value >> (8 * i))

    # ---- conversion to / from indexed pixels --------------------------------

    @property
    def start_address(self) -> int:
        return (self.crtc[0x0C] << 8) | self.crtc[0x0D]

    def rasterise(self, page: int = 0, width: int = WIDTH, height: int = HEIGHT,
                  stride: int = BYTES_PER_LINE) -> bytes:
        """Indexed pixels (0..15, one byte each, row-major) of the page at VRAM
        offset `page`; plane p supplies bit p, MSB of each byte is the leftmost pixel."""
        out = bytearray(width * height)
        p0, p1, p2, p3 = self.planes
        for y in range(height):
            row = page + y * stride
            for xb in range((width + 7) // 8):
                a = row + xb
                b0, b1, b2, b3 = p0[a], p1[a], p2[a], p3[a]
                base = y * width + xb * 8
                for bit in range(min(8, width - xb * 8)):
                    s = 7 - bit
                    out[base + bit] = ((b0 >> s & 1) | (b1 >> s & 1) << 1 |
                                       (b2 >> s & 1) << 2 | (b3 >> s & 1) << 3)
        return bytes(out)

    def load_indexed(self, pixels: bytes, page: int = 0, width: int = WIDTH,
                     height: int = HEIGHT, stride: int = BYTES_PER_LINE) -> None:
        """Inverse of rasterise: write indexed pixels (low 4 bits used) into the planes."""
        for y in range(height):
            for xb in range((width + 7) // 8):
                bits = [0, 0, 0, 0]
                for bit in range(min(8, width - xb * 8)):
                    c = pixels[y * width + xb * 8 + bit]
                    for p in range(4):
                        bits[p] |= (c >> p & 1) << (7 - bit)
                for p in range(4):
                    self.planes[p][page + y * stride + xb] = bits[p]

    def snapshot(self) -> list[bytes]:
        return [bytes(p) for p in self.planes]
