"""The 10b0 spawn-list clobber (script_clobber_spawns_10b0, src/game/script.c) vs game86.

In the original a think_13 entity that drops a shot (1000:2594 -> 10b0 -> enemy_shot_init)
clears [SI+8] and [SI+0A] with SI = think_13's code offset, which zeroes two spawn pairs of
the level script stored at that DS offset (levels.dat clobber_10b0). The test runs a real
drop through the think loop (1000:432b) in unicorn, checks which pairs it zeroed against
clobber_10b0, then walks that level's spawn list with 1000:3d09 and compares each pair with
script_next_spawn_pair after script_clobber_spawns_10b0, and without it on a fresh image.
"""

import ctypes
import struct

import pytest

from adapters.cport import bind
from adapters.gamestate import DGROUP_SIZE, DgroupMap

STUB_DRIVER = (0x1000, 0xE785)
SND_DRIVER = 0x0400
ENTITY_TABLE, ENTITY_SIZE, ENTITY_COUNT = 0x48C1, 0x40, 70
THINK_TABLE = 0x1FD2
THINK_13 = 0x13
LEVEL_TABLE = 0x2BDA
SPAWN_CURSOR = 0x39EB
SPAWNING_DONE = 0x0802
SPAWN_REMAINING = 0x08FA
SPAWN_TYPE = 0x0900
SHIPS = (0x2CFB, 0x2CFF)
OP_OPERANDS = [2, 4, 4, 2, 2, 2, 3, 1]
LEVEL_NONE = 0xFFFF
MAX_INSNS = 1_000_000


class LevelSpawnClobber(ctypes.Structure):
    _fields_ = [("level", ctypes.c_uint16), ("first_spawn", ctypes.c_uint16),
                ("spawn_count", ctypes.c_uint16)]


@pytest.fixture(scope="module")
def port(cgame):
    class Port:
        dg = DgroupMap(cgame)
        next_pair = bind(cgame, "script_next_spawn_pair", ctypes.c_bool,
                         ctypes.c_void_p, ctypes.c_void_p)
        clobber = bind(cgame, "script_clobber_spawns_10b0", None, ctypes.c_void_p)
        levels = bind(cgame, "asset_levels", ctypes.c_void_p)()

        @staticmethod
        def target(levels):
            # LevelSet.clobber_10b0 is the struct's first member (formats.h)
            return LevelSpawnClobber.from_address(levels)
    if not Port.dg.has_data or not Port.levels:
        pytest.skip("extracted data not available")
    return Port


def w16(img, off, v):
    struct.pack_into("<H", img, off, v & 0xFFFF)


def r16(img, off):
    return struct.unpack_from("<H", img, off)[0]


def spawn_list(img, level):
    off = r16(img, LEVEL_TABLE + 2 * level)
    while img[off] >= 0xF8:
        off += 1 + OP_OPERANDS[0xFF - img[off]]
    return off + 1                                 # past the wave-type byte


def clean_image(machine, dg):
    img = bytearray(dg.canonical(machine.read_dgroup(0, DGROUP_SIZE)))
    w16(img, SND_DRIVER, STUB_DRIVER[1])
    w16(img, SND_DRIVER + 2, STUB_DRIVER[0])
    return img


def drop_shot(machine, img):
    """One think_13 entity about to drop (2582..258c all fall through), every other slot
    free; runs the think loop and returns the resulting image."""
    table = bytearray(b"\xff" * (ENTITY_COUNT * ENTITY_SIZE))
    rec = bytearray(ENTITY_SIZE)
    rec[0], rec[1] = THINK_13, 0
    struct.pack_into("<hh", rec, 0x02, 160, 100)
    struct.pack_into("<H", rec, 0x06, 0x12)
    rec[0x24], rec[0x25] = 5, 0                    # reload, countdown: DEC -> -1
    struct.pack_into("<H", rec, 0x2E, 3)           # drops left: DEC -> 2
    struct.pack_into("<h", rec, 0x30, -1)
    table[0:ENTITY_SIZE] = rec
    img[ENTITY_TABLE:ENTITY_TABLE + len(table)] = table
    for at in SHIPS:                               # keep the ships away from the entity
        struct.pack_into("<hh", img, at, 20, 20)
    machine.write_dgroup(0, bytes(img))
    machine.call("1000:432b", max_insns=MAX_INSNS)
    out = machine.read_dgroup(0, DGROUP_SIZE)
    assert r16(out, ENTITY_TABLE + ENTITY_SIZE) == 0x1010, "no shot was dropped"
    return out


def walk_list(machine, port, img, level, clobbered):
    start = spawn_list(img, level)
    index = 0
    while True:
        w16(img, SPAWNING_DONE, 0)
        w16(img, SPAWN_REMAINING, 0x1234)
        w16(img, SPAWN_TYPE, 0x5678)
        w16(img, SPAWN_CURSOR, start + 2 * index)
        machine.write_dgroup(0, bytes(img))
        machine.call("1000:3d09", max_insns=MAX_INSNS)
        orig = machine.read_dgroup(0, DGROUP_SIZE)

        state = port.dg.from_dgroup(bytes(img))
        if clobbered:
            port.clobber(state)
        more = port.next_pair(state, port.levels)
        mine = port.dg.to_dgroup(state, bytes(img))

        bad = port.dg.mismatches(orig, mine)
        assert bad == [], (index, {hex(o): (orig[o], mine[o]) for o in bad[:16]})
        assert more == (orig[SPAWNING_DONE] == 0)
        if not more:
            return index
        index += 1


def test_clobber_target_matches_original(machine, port):
    target = port.target(port.levels)
    assert target.level != LEVEL_NONE
    img = clean_image(machine, port.dg)
    assert r16(img, THINK_TABLE + 2 * THINK_13) + 8 == spawn_list(img, target.level) + 2 * target.first_spawn
    after = drop_shot(machine, bytearray(img))
    changed = [o for o in range(0x244C, 0x2AFE) if img[o] != after[o]]
    first = spawn_list(img, target.level) + 2 * target.first_spawn
    assert set(changed) <= set(range(first, first + 2 * target.spawn_count))
    assert all(after[o] == 0 for o in range(first, first + 2 * target.spawn_count))


@pytest.mark.parametrize("clobbered", [False, True])
def test_spawn_list_after_drop(machine, port, clobbered):
    target = port.target(port.levels)
    img = clean_image(machine, port.dg)
    if clobbered:
        img = bytearray(drop_shot(machine, img))
        img[ENTITY_TABLE:ENTITY_TABLE + ENTITY_COUNT * ENTITY_SIZE] = \
            clean_image(machine, port.dg)[ENTITY_TABLE:ENTITY_TABLE + ENTITY_COUNT * ENTITY_SIZE]
        img = bytearray(port.dg.canonical(bytes(img)))
    walk_list(machine, port, img, target.level, clobbered)
