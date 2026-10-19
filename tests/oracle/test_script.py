"""Level scripts (src/game/script.c) vs game86 in unicorn.

script_start_level: the inline sequence 1000:020b..0461 (ops loop, level wrap, wave type,
spawn cursor [0x39EB] at 045d) on a random image with level 0..100 and loop 0..3.
script_next_spawn_pair: 1000:3d09 with the cursor in a level's list or in the current
player's roster, up to and including the (x, 0xFF) end marker (0802 = 1, 08F8 = 091B).

Both sides start from the same canonical DGROUP image; every byte the adapter maps is
compared, plus the sound requests (op 2 requests sample 0x16). The LevelSet comes from the
extracted data the testlib loads (asset_levels).
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DGROUP_SIZE, DgroupMap

STUB_DRIVER = (0x1000, 0xE785)
SND_DRIVER = 0x0400
LEVEL = 0x21AA
LOOP_COUNT = 0x233C
PLAYER = 0x21A6
ROSTER = 0x220A
ROSTER_SIZE = 50
LEVEL_TABLE = 0x2BDA
LEVEL_COUNT = 100
SPAWN_CURSOR = 0x39EB
SPAWNING_DONE = 0x0802
UNHATCHED = 0x091B
UNHATCHED_LEFT = 0x08F8
# words the level ops set (LevelTuning) and the per-player parameters 21AC..21CB
TUNING = [0x812, 0x816, 0x818, 0x820, 0x826, 0x828, 0x82A, 0x82C, 0x82E, 0x830, 0x86E]
OP_OPERANDS = [2, 4, 4, 2, 2, 2, 3, 1]
MAX_INSNS = 1_000_000


class SoundRequest(ctypes.Structure):
    _fields_ = [("sample", ctypes.c_uint8), ("rate_index", ctypes.c_uint8),
                ("priority", ctypes.c_uint8)]


@pytest.fixture(scope="module")
def port(cgame):
    class Port:
        dg = DgroupMap(cgame)
        start_level = bind(cgame, "script_start_level", None, ctypes.c_void_p, ctypes.c_void_p)
        next_pair = bind(cgame, "script_next_spawn_pair", ctypes.c_bool,
                         ctypes.c_void_p, ctypes.c_void_p)
        levels = bind(cgame, "asset_levels", ctypes.c_void_p)()
        log_count = bind(cgame, "game_sound_log_count", ctypes.c_uint)
        log_at = bind(cgame, "game_sound_log_at", SoundRequest, ctypes.c_uint)
        log_clear = bind(cgame, "game_sound_log_clear", None)
    if not Port.dg.has_data or not Port.levels:
        pytest.skip("extracted data not available")
    return Port


def w16(img: bytearray, off: int, v: int) -> None:
    struct.pack_into("<H", img, off, v & 0xFFFF)


def base_image(machine, dg, rnd):
    img = bytearray(dg.canonical(machine.read_dgroup(0, DGROUP_SIZE)))
    for off in TUNING:
        w16(img, off, rnd.randrange(0x10000))
    img[PLAYER + 6:PLAYER + 0x26] = rnd.randbytes(0x20)     # 21AC..21CB
    w16(img, SND_DRIVER, STUB_DRIVER[1])
    w16(img, SND_DRIVER + 2, STUB_DRIVER[0])
    return img


def run_original(machine, img, start, stop=None):
    machine.write_dgroup(0, bytes(img))
    sounds = []

    def driver(m):
        r = m.get_regs()
        if r["ax"] >> 8 == 3:
            sounds.append((r["ax"] & 0xFF, r["cx"] & 0xFF, r["cx"] >> 8))

    handle = machine.add_breakpoint(STUB_DRIVER, driver)
    try:
        if stop:
            machine.run_until(start, stop, max_insns=MAX_INSNS)
        else:
            machine.call(start, max_insns=MAX_INSNS)
    finally:
        machine.remove_breakpoint(handle)
    return machine.read_dgroup(0, DGROUP_SIZE), sounds


def run_port(port, img, fn):
    state = port.dg.from_dgroup(bytes(img))
    port.log_clear()
    result = fn(state, port.levels)
    sounds = [port.log_at(i) for i in range(port.log_count())]
    return (port.dg.to_dgroup(state, bytes(img)),
            [(s.sample, s.rate_index, s.priority) for s in sounds], result)


def compare(port, orig, mine):
    bad = port.dg.mismatches(orig, mine)
    assert bad == [], {hex(o): (orig[o], mine[o]) for o in bad[:16]}


# ---- script_start_level --------------------------------------------------------------

@pytest.mark.parametrize("level", range(LEVEL_COUNT + 1))     # 100: the 0 entry, wraps
def test_start_level_each(machine, port, level):
    rnd = case_rng("script.start_each", level)
    img = base_image(machine, port.dg, rnd)
    w16(img, LEVEL, level)
    w16(img, LOOP_COUNT, rnd.randrange(3))
    orig, orig_sounds = run_original(machine, img, "1000:020b", "1000:0461")
    mine, my_sounds, _ = run_port(port, img, port.start_level)
    compare(port, orig, mine)
    assert my_sounds == orig_sounds


@pytest.mark.parametrize("case", fuzz_cases(40))
def test_start_level_random(machine, port, case):
    rnd = case_rng("script.start_random", case)
    img = base_image(machine, port.dg, rnd)
    w16(img, LEVEL, rnd.choice([rnd.randrange(LEVEL_COUNT), LEVEL_COUNT]))
    w16(img, LOOP_COUNT, rnd.choice([0, 1, 2, 3, rnd.randrange(16)]))
    orig, orig_sounds = run_original(machine, img, "1000:020b", "1000:0461")
    mine, my_sounds, _ = run_port(port, img, port.start_level)
    compare(port, orig, mine)
    assert my_sounds == orig_sounds


# ---- script_next_spawn_pair ----------------------------------------------------------

def level_spawn_list(img, level):
    off = struct.unpack_from("<H", img, LEVEL_TABLE + 2 * level)[0]
    while img[off] >= 0xF8:
        off += 1 + OP_OPERANDS[0xFF - img[off]]
    return off + 1


def spawn_state(rnd, img):
    w16(img, SPAWNING_DONE, 0)
    w16(img, UNHATCHED, rnd.randrange(0x10000))
    w16(img, UNHATCHED_LEFT, rnd.randrange(0x10000))
    w16(img, 0x08FA, rnd.randrange(0x10000))
    w16(img, 0x0900, rnd.randrange(0x10000))


def check_pair(machine, port, img):
    orig, _ = run_original(machine, img, "1000:3d09")
    mine, _, more = run_port(port, img, port.next_pair)
    compare(port, orig, mine)
    assert more == (orig[SPAWNING_DONE] == 0)


@pytest.mark.parametrize("level", [0, 1, 2, 17, 42, 63, 98, 99])
def test_next_pair_level_list(machine, port, level):
    rnd = case_rng("script.pair_level", level)
    img = base_image(machine, port.dg, rnd)
    start = level_spawn_list(img, level)
    end = 0
    while img[start + 2 * end + 1] != 0xFF:
        end += 1
    for index in range(end + 1):                  # every pair and the end marker
        spawn_state(rnd, img)
        w16(img, SPAWN_CURSOR, start + 2 * index)
        check_pair(machine, port, img)


@pytest.mark.parametrize("case", fuzz_cases(40))
def test_next_pair_roster(machine, port, case):
    rnd = case_rng("script.pair_roster", case)
    img = base_image(machine, port.dg, rnd)
    end = rnd.randrange(ROSTER_SIZE)              # the end marker's slot
    for i in range(ROSTER_SIZE):
        kind = rnd.randrange(41) if rnd.random() < 0.9 else rnd.randrange(256)
        count = rnd.randrange(0xFF) if i != end else 0xFF
        img[ROSTER + 2 * i] = kind
        img[ROSTER + 2 * i + 1] = count
    spawn_state(rnd, img)
    w16(img, SPAWN_CURSOR, ROSTER + 2 * rnd.randrange(end + 1))
    img = bytearray(port.dg.canonical(bytes(img)))
    check_pair(machine, port, img)
