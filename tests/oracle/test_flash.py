"""W6 screen flashes: game86 frame_run's flash step 1e7b:0196..0212 against draw_flash_step
(src/game/draw_page.c) through llamatron_game_testlib.

Both sides start from the game palette (palette_set DS:2144 vs gfx_set_game_palette) on VGA
or EGA (or from a random palette), with a random flash mode, frame counter (044C) and flash list position (06C7 at the
start of one of the real lists, inside it or at its terminator), and run a few frames' steps.
The original's INT 10h calls go to the card model of test_engine_palette (plus AX=1010h, one
DAC register, and AX=1000h, one attribute register); after each step the colours it shows must
be the port's gfx_palette() entries 0..15, and the DGROUP state the port's.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DgroupMap
from test_engine_palette import (ADAPTER_EGA, ADAPTER_VGA, CS_SAVED_DS, G_VGA, NIBBLE_TO_DAC,
                                 VGA_ATTRIBUTES, Card, GfxColor, GfxGamePalette)

CODE0 = 0x1000
GAME_PALETTE = 0x2144
START_PALETTE = 0x8000         # scratch DGROUP space for a random starting palette
FLASH_MODE, FRAME_TIMER, FLASH_LIST = 0x0589, 0x044C, 0x06C7
FLASH_LISTS = (0x06C9, 0x06CD, 0x06FD, 0x0707, 0x0713, 0x0721, 0x0729, 0x072D)


class FlashCard(Card):
    def int10(self, m, intno):
        r = m.get_regs()
        if r["ax"] == 0x1010:
            self.calls.append(r["ax"])
            dh, ch, cl = r["dx"] >> 8, r["cx"] >> 8, r["cx"] & 0xFF
            self.dac[r["bx"]] = (dh & 0x3F, ch & 0x3F, cl & 0x3F)
        elif r["ax"] == 0x1000:
            self.calls.append(r["ax"])
            self.attributes[r["bx"] & 0xFF] = r["bx"] >> 8
        else:
            super().int10(m, intno)


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


@pytest.fixture(scope="module")
def c(cgame):
    bind(cgame, "game_data_load", ctypes.c_bool)()
    return {
        "init": bind(cgame, "gfx_init", None),
        "set_adapter": bind(cgame, "gfx_set_adapter", None, ctypes.c_int,
                            ctypes.POINTER(ctypes.c_uint8)),
        "set_game_palette": bind(cgame, "gfx_set_game_palette", None, ctypes.c_void_p),
        "palette_at": bind(cgame, "asset_palette_at", ctypes.c_void_p, ctypes.c_uint16),
        "palette": bind(cgame, "gfx_palette", ctypes.POINTER(GfxColor)),
        "step": bind(cgame, "draw_flash_step", None, ctypes.c_void_p),
    }


def list_words(machine, start):
    """Offsets of a list's words, up to and including its 0 terminator."""
    out, off = [], start
    while True:
        out.append(off)
        if machine.read_u16(machine.dgroup, off) == 0:
            return out
        off += 2


def shown(card, vga):
    return [card.vga_colour(i) if vga else card.ega_colour(i) for i in range(16)]


@pytest.mark.parametrize("case", fuzz_cases(32))
def test_flash_step(machine, dg, c, case):
    rnd = case_rng("flash_step", case)
    vga = rnd.randrange(2)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_dgroup(G_VGA, bytes([vga]))
    card = FlashCard()
    if vga:
        card.attributes = list(machine.read_dgroup(VGA_ATTRIBUTES, 16))
    # Start from the game palette or from a random one, which the 30-frame reload replaces.
    start = GAME_PALETTE
    if rnd.random() < 0.5:
        start = START_PALETTE
        machine.write_dgroup(START_PALETTE, bytes(rnd.randrange(256) for _ in range(0x31)))
    machine.call("1000:1be6", regs={"dx": start}, far=True, interrupts={0x10: card.int10})
    start_palette = machine.read_dgroup(start, 0x31)

    img = bytearray(machine.read_dgroup(0, 0x10000))
    img[FLASH_MODE] = rnd.choice([2, 2, 2, 0, 1])
    img[FRAME_TIMER] = rnd.choice([1, 2, 0, rnd.randrange(256)])
    words = list_words(machine, rnd.choice(FLASH_LISTS))
    struct.pack_into("<H", img, FLASH_LIST,
                     rnd.choice([0, words[0], words[-1], rnd.choice(words)]))
    image = dg.canonical(bytes(img))
    machine.write_dgroup(0, image)

    c["init"]()
    table = (ctypes.c_uint8 * 16)(*machine.read_dgroup(NIBBLE_TO_DAC, 16))
    c["set_adapter"](ADAPTER_VGA if vga else ADAPTER_EGA, table)
    if start == GAME_PALETTE:
        c["set_game_palette"](c["palette_at"](GAME_PALETTE))
    else:
        c["set_game_palette"](ctypes.byref(GfxGamePalette.from_buffer_copy(
            start_palette + b"\0")))   # + the struct's padding
    state = dg.from_dgroup(image)

    for frame in range(rnd.randrange(1, 6)):
        machine.run_until("1e7b:0196", "1e7b:0213", interrupts={0x10: card.int10},
                          max_insns=1_000_000)
        c["step"](state)
        got = c["palette"]()
        want = shown(card, vga)
        for i in range(16):
            assert (got[i].r, got[i].g, got[i].b) == want[i], \
                f"frame {frame}: colour {i} (vga={vga})"
        bad = dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, image))
        assert not bad, f"frame {frame}: DGROUP differs at {[hex(o) for o in bad[:16]]}"
