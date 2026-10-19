"""W5b palette_pulse.c against game86: the timer tick's colour-1 pulse 1000:3e4e..3eb5
(VGA: INT 10h AX=1010h register 1 from the 16-step table DS:0658; EGA: AX=1000h register 1
from DS:0678, one step of g_pulse_index 0680 per 4 ticks) against palette_pulse_tick.

Both sides start from the game palette on VGA or EGA, with a random flash mode, pulse enable,
tick count and pulse step, and run a few ticks (tick count +1 between them, as 3e08 does). The
original's INT 10h calls go to test_flash's card model; after each tick the colours it shows
must be the port's gfx_palette() entries 0..15, and the DGROUP state the port's.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases
from test_engine_palette import (ADAPTER_EGA, ADAPTER_VGA, CS_SAVED_DS, G_VGA, NIBBLE_TO_DAC,
                                 VGA_ATTRIBUTES, GfxColor)
from test_flash import FlashCard, shown

from adapters.cport import bind
from adapters.gamestate import DgroupMap

CODE0 = 0x1000
GAME_PALETTE = 0x2144
FLASH_MODE, PULSE_ENABLE, PULSE_INDEX, TICKS = 0x0589, 0x0902, 0x0680, 0x39E6


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
        "tick": bind(cgame, "palette_pulse_tick", None, ctypes.c_void_p),
    }


def tick_count_offset(dg):
    """Byte offset of GameState.tick_count, found through the adapter."""
    image = bytes(0x10000)
    zero = dg.from_dgroup(image)
    marked = bytearray(image)
    struct.pack_into("<H", marked, TICKS, 0xA55A)
    state = dg.from_dgroup(bytes(marked))
    for off in range(len(zero.raw) - 1):
        if state.raw[off:off + 2] == b"\x5a\xa5" and zero.raw[off:off + 2] == b"\0\0":
            return off
    raise AssertionError("tick_count not found")


@pytest.mark.parametrize("case", fuzz_cases(48))
def test_palette_pulse(machine, dg, c, case):
    rnd = case_rng("palette_pulse", case)
    vga = rnd.randrange(2)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_dgroup(G_VGA, bytes([vga]))
    card = FlashCard()
    if vga:
        card.attributes = list(machine.read_dgroup(VGA_ATTRIBUTES, 16))
    machine.call("1000:1be6", regs={"dx": GAME_PALETTE}, far=True, interrupts={0x10: card.int10})

    img = bytearray(machine.read_dgroup(0, 0x10000))
    img[FLASH_MODE] = rnd.choice([2, 2, 1, 0])
    struct.pack_into("<H", img, PULSE_ENABLE, rnd.choice([1, 1, 1, 0]))
    struct.pack_into("<H", img, PULSE_INDEX, rnd.choice([0, 7, 0xFFFF, 0x7FFF, rnd.randrange(0x10000)]))
    struct.pack_into("<H", img, TICKS, rnd.choice([0, 3, 0xFFFF, rnd.randrange(0x10000)]))
    image = dg.canonical(bytes(img))
    machine.write_dgroup(0, image)

    c["init"]()
    table = (ctypes.c_uint8 * 16)(*machine.read_dgroup(NIBBLE_TO_DAC, 16))
    c["set_adapter"](ADAPTER_VGA if vga else ADAPTER_EGA, table)
    c["set_game_palette"](c["palette_at"](GAME_PALETTE))
    state = dg.from_dgroup(image)
    ticks_at = tick_count_offset(dg)

    for tick in range(rnd.randrange(1, 10)):
        if tick:                                    # 3e08: the next tick counts first
            t = (machine.read_u16(machine.dgroup, TICKS) + 1) & 0xFFFF
            machine.write_dgroup(TICKS, struct.pack("<H", t))
            struct.pack_into("<H", state, ticks_at, t)
        machine.run_until("1000:3e4e", "1000:3eb7", interrupts={0x10: card.int10},
                          max_insns=100_000)
        c["tick"](state)
        got = c["palette"]()
        want = shown(card, vga)
        for i in range(16):
            assert (got[i].r, got[i].g, got[i].b) == want[i], f"tick {tick}: colour {i} (vga={vga})"
        bad = dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, image))
        assert not bad, f"tick {tick}: DGROUP differs at {[hex(o) for o in bad[:16]]}"
