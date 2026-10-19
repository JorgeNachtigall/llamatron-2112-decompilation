"""W2 palette: game86 palette_set 1000:1be6 against gfx_set_game_palette (src/engine/gfx.c)
through llamatron_game_testlib.

The original's INT 10h calls are captured and applied to a model of the card: on VGA, AX=1012h
loads DAC registers (two blocks of 8: 0..7 and 16..23) and mode 0Dh shows colour i through
attribute register i, which video_set_mode 1000:e4f5 loads (AX=1002h from DS:2122); on EGA,
AX=1002h loads the 16 attribute registers + overscan straight from the palette. The shown
colours must equal the port's gfx_palette() entries 0..15. Palettes, the nibble -> DAC table
(DS:2134) and its 6-bit values are random.
"""

import ctypes
import struct

from generators import case_rng, fuzz_cases

from adapters.cport import bind

CODE0, CS_SAVED_DS = 0x1000, 0x0255
G_VGA = 0x058A
NIBBLE_TO_DAC = 0x2134
VGA_ATTRIBUTES = 0x2122                  # what e4f5 loads on VGA
PALETTE_AT = 0x2144                      # where the game keeps one (any DGROUP address works)
DAC_SCRATCH = (0x4451, 16 * 3)           # palette_set's DAC staging buffer, not kept by the port
ADAPTER_EGA, ADAPTER_VGA = 0, 1


class GfxGamePalette(ctypes.Structure):
    _fields_ = [("st_words", ctypes.c_uint16 * 16), ("ega_registers", ctypes.c_uint8 * 17)]


class GfxColor(ctypes.Structure):
    _fields_ = [("r", ctypes.c_uint8), ("g", ctypes.c_uint8), ("b", ctypes.c_uint8)]


class Card:
    """What palette_set / video_set_mode tell the video BIOS."""

    def __init__(self):
        self.dac = [(0, 0, 0)] * 256
        self.attributes = list(range(16))
        self.calls = []

    def int10(self, m, _intno):
        r = m.get_regs()
        self.calls.append(r["ax"])
        if r["ax"] == 0x1012:
            data = m.read(r["es"], r["dx"], 3 * r["cx"])
            for i in range(r["cx"]):
                self.dac[r["bx"] + i] = tuple(b & 0x3F for b in data[3 * i:3 * i + 3])
        elif r["ax"] == 0x1002:
            self.attributes = list(m.read(r["es"], r["dx"], 16))

    def vga_colour(self, i):
        return tuple((v << 2 | v >> 4) for v in self.dac[self.attributes[i]])

    def ega_colour(self, i):
        v = self.attributes[i]
        return (((v >> 2) & 1) * 0xAA + ((v >> 5) & 1) * 0x55,
                ((v >> 1) & 1) * 0xAA + ((v >> 4) & 1) * 0x55,
                (v & 1) * 0xAA + ((v >> 3) & 1) * 0x55)


class C:
    def __init__(self, lib):
        self.init = bind(lib, "gfx_init", None)
        self.set_adapter = bind(lib, "gfx_set_adapter", None, ctypes.c_int, ctypes.POINTER(ctypes.c_uint8))
        self.set_game_palette = bind(lib, "gfx_set_game_palette", None, ctypes.POINTER(GfxGamePalette))
        self.palette = bind(lib, "gfx_palette", ctypes.POINTER(GfxColor))


def run_original(machine, vga, words, ega, table):
    machine.reset()
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_dgroup(G_VGA, bytes([vga]))
    machine.write_dgroup(NIBBLE_TO_DAC, bytes(table))
    machine.write_dgroup(PALETTE_AT, struct.pack("<16H", *words) + bytes(ega))
    card = Card()
    if vga:
        # mode 0Dh's attribute registers as video_set_mode leaves them on VGA
        card.attributes = list(machine.read_dgroup(VGA_ATTRIBUTES, 16))
    res = machine.call("1000:1be6", regs={"dx": PALETTE_AT}, far=True, interrupts={0x10: card.int10})
    changed = [off for off in res.dgroup_diff
               if not DAC_SCRATCH[0] <= off < DAC_SCRATCH[0] + DAC_SCRATCH[1]]
    assert changed == [], f"palette_set wrote DGROUP outside its DAC buffer: {changed}"
    return card


def test_vga_attribute_registers_are_the_ports_mapping(machine):
    """The port shows colour i as DAC i (i < 8) or DAC i + 8: the registers e4f5 loads."""
    regs = machine.read_dgroup(VGA_ATTRIBUTES, 16)
    assert list(regs) == list(range(8)) + list(range(16, 24))


def test_palette_set(machine, cgame):
    c = C(cgame)
    c.init()
    for case in fuzz_cases(60):
        rnd = case_rng("engine_palette", case)
        vga = rnd.randrange(2)
        words = [rnd.randrange(0x10000) for _ in range(16)]
        ega = [rnd.randrange(256) for _ in range(17)]
        table = [rnd.randrange(256) for _ in range(16)]
        card = run_original(machine, vga, words, ega, table)
        assert card.calls == ([0x1012, 0x1012] if vga else [0x1002]), f"case {case}"

        c.set_adapter(ADAPTER_VGA if vga else ADAPTER_EGA, (ctypes.c_uint8 * 16)(*table))
        pal = GfxGamePalette((ctypes.c_uint16 * 16)(*words), (ctypes.c_uint8 * 17)(*ega))
        c.set_game_palette(ctypes.byref(pal))
        got = c.palette()
        for i in range(16):
            want = card.vga_colour(i) if vga else card.ega_colour(i)
            assert (got[i].r, got[i].g, got[i].b) == want, f"case {case} vga={vga} colour {i}"
