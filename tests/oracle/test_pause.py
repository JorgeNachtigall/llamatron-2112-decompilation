"""W5b pause.c against game86: pause_tick_hook (1000:14d0, incl. sound_toggle 1514 and
pause_key_sample 1538), and sound_toggle on its own against sound.c game_sound_toggle. The keyboard bytes (0586 last make code, 0B72 its copy) live in the
engine's InputState on the port side; the hooks (39E0 draw, 39E4 tick) are compared as
GameState.draw_hook / tick_hook."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkCase, ThinkOracle

from adapters.cport import bind
from adapters.gamestate import SCRATCH

LAST_MAKE_CODE, LAST_MAKE_CODE_COPY = 0x0586, 0x0B72
SOUND_KEY_LATCH, SOUND_ON, SOUND_PRESENT = 0x0686, 0x0688, 0x0689
SND_DRIVER, SND_DRIVER_OTHER = 0x0400, 0x0404   # far pointers: the driver in use, the other
SOUND_OFF_STUB, DRIVER_SEG = (0x1000, 0xE785), 0x9800
PAUSE_MESSAGE, PAUSE_MESSAGES = 0x09EC, 0x09E2
DRAW_HOOK, TICK_HOOK = 0x39E0, 0x39E4
PLAY_DRAW_HOOK, PLAY_TICK_HOOK, PAUSE_TICK_HOOK = 0x1A3B, 0x1562, 0x14D0
KEY_O, KEY_S, KEY_G = 0x18, 0x1F, 0x22


class InputPrefix(ctypes.Structure):
    """The head of the engine's InputState (src/engine/input.h) up to the make codes."""
    _fields_ = [("device", ctypes.c_int), ("single_stick", ctypes.c_bool),
                ("keys_held", ctypes.c_uint8), ("last_scan_code", ctypes.c_uint8),
                ("last_make_code", ctypes.c_uint8), ("last_make_code_copy", ctypes.c_uint8)]


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


@pytest.fixture(scope="module")
def port(cgame):
    hook = bind(cgame, "pause_tick_hook", None, ctypes.c_void_p)
    keys = ctypes.cast(bind(cgame, "input_state", ctypes.c_void_p)(),
                       ctypes.POINTER(InputPrefix)).contents
    set_enabled = bind(cgame, "snd_set_enabled", None, ctypes.c_bool)
    enabled = bind(cgame, "snd_enabled", ctypes.c_bool)
    return hook, keys, set_enabled, enabled


@pytest.fixture(scope="module")
def toggle(cgame):
    return bind(cgame, "game_sound_toggle", None, ctypes.c_void_p)


def make_case(oracle, machine, rnd):
    c = oracle.make_case(machine, rnd, 0x01)
    img = bytearray(c.image)
    img[LAST_MAKE_CODE] = rnd.choice([KEY_O, KEY_S, KEY_S, KEY_G, 0x19, 0, rnd.randrange(0x80)])
    img[LAST_MAKE_CODE_COPY] = rnd.choice([0x1b, 0x1c, 0x1d, 0x3f, rnd.randrange(0x80)])
    struct.pack_into("<H", img, SOUND_KEY_LATCH, rnd.choice([0, 1]))
    img[SOUND_ON] = rnd.choice([0, 1])
    messages = struct.unpack_from("<4H", img, PAUSE_MESSAGES)
    struct.pack_into("<H", img, PAUSE_MESSAGE, rnd.choice((0,) + messages))
    struct.pack_into("<H", img, DRAW_HOOK, rnd.choice([PLAY_DRAW_HOOK, 0x0894]))
    struct.pack_into("<H", img, TICK_HOOK, PAUSE_TICK_HOOK)
    playing = rnd.choice([0, 0, rnd.randrange(1, 33)])
    return ThinkCase(oracle.dg.canonical(bytes(img)), 0, (playing, c.status[1]))


@pytest.mark.parametrize("case", fuzz_cases(192))
def test_pause_tick_hook(machine, oracle, port, case):
    hook, keys, set_enabled, enabled = port
    case_ = make_case(oracle, machine, case_rng("pause_tick_hook", case))
    want, want_sounds = oracle.run_original(machine, case_, None, "1000:14d0")

    def run(state):
        keys.last_make_code = case_.image[LAST_MAKE_CODE]
        keys.last_make_code_copy = case_.image[LAST_MAKE_CODE_COPY]
        hook(ctypes.addressof(state))
    was_enabled = enabled()     # game_sound_toggle switches the engine; the testlib runs it off
    try:
        got, got_sounds = oracle.run_port(case_, None, run)
    finally:
        set_enabled(was_enabled)
    bad = oracle.dg.mismatches(want, got, ignore=SCRATCH)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"
    assert got_sounds == want_sounds


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_sound_toggle(machine, oracle, port, toggle, case):
    """1000:1514 flips sound_on (0688) and swaps the driver in use ([0400]) with the other one
    ([0404]): the sound-off stub e785 and the card's driver. The port's game_sound_toggle flips
    sound_on and switches the engine (snd_set_enabled); sound_present (0689) stays."""
    _, _, set_enabled, enabled = port
    rnd = case_rng("sound_toggle", case)
    img = bytearray(oracle.make_case(machine, rnd, 0x01).image)
    on = rnd.randrange(2)
    img[SOUND_ON], img[SOUND_PRESENT] = on, rnd.randrange(2)
    driver = (DRIVER_SEG, rnd.randrange(0, 0x100, 2))
    in_use, other = (driver, SOUND_OFF_STUB) if on else (SOUND_OFF_STUB, driver)
    struct.pack_into("<4H", img, SND_DRIVER, in_use[1], in_use[0], other[1], other[0])
    image = oracle.dg.canonical(bytes(img))
    machine.write_dgroup(0, image)
    machine.call("1000:1514")
    want = machine.read_dgroup(0, 0x10000)
    seg_off = struct.unpack_from("<4H", want, SND_DRIVER)
    assert ((seg_off[1], seg_off[0]), (seg_off[3], seg_off[2])) == (other, in_use)

    state = oracle.dg.from_dgroup(image)
    was_enabled = enabled()
    try:
        set_enabled(bool(on))
        toggle(ctypes.addressof(state))
        port_driver_on = enabled()
    finally:
        set_enabled(was_enabled)
    assert port_driver_on == ((seg_off[1], seg_off[0]) == driver)
    got = oracle.dg.to_dgroup(state, image)
    bad = oracle.dg.mismatches(want, got, ignore=SCRATCH)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"

