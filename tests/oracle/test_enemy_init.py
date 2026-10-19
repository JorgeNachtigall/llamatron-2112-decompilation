"""W5a: the 41 wave-spawn init handlers (tbl_enemy_init, DS:2CA4; src/game/entities/
enemy_init.c with spawn.c's enemy_random_spawn_pos) against game86.

The original is entered as wave_spawn_next (1000:1dcb) jumps there: SI = the slot the spawn
loop just took with entity_alloc (still free, overlay cleared, kind set). The port runs
enemy_init_handlers[i] on the same record."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.entities import ENTITY_COUNT
from think_oracle import FREE_SLOTS, w16
from w5a_oracle import (INIT_COUNT, INIT_FN, INIT_TABLE, WAVE, W5aOracle, slot_address, u16)

SINE8 = 0x4141              # g_sine8: filled by data_load at start-up, not in the EXE image
SPRITE_REF_LLAMA = 0x1B1    # 218C:0362 / 2
# Inits that leave sprite < 0 with a frame in +2c (Entity.sprite_ref).
SPRITE_REF_INITS = {0x01}
MIN_FREE = 12               # enemy_init_10 needs nine slots


@pytest.fixture(scope="module")
def oracle(cgame):
    o = W5aOracle(cgame)
    sine = bind(cgame, "asset_sine", ctypes.c_void_p)()
    assert sine, "extracted data not loaded"
    o.sine8 = ctypes.string_at(sine, 256)
    return o


def init_address(machine, index):
    return struct.unpack("<H", machine.read_dgroup(INIT_TABLE + 2 * index, 2))[0]


def spawn_case(machine, oracle, rnd, index):
    case = oracle.random_image(machine, rnd)
    image = bytearray(case.image)
    image[SINE8:SINE8 + 256] = oracle.sine8
    types = [struct.unpack_from("<h", image, slot_address(s))[0] for s in range(ENTITY_COUNT)]
    free = [s for s in range(ENTITY_COUNT) if types[s] < 0]
    busy = [s for s in range(ENTITY_COUNT) if types[s] >= 0]
    rnd.shuffle(busy)
    while len(free) < MIN_FREE:
        s = busy.pop()
        image[slot_address(s):slot_address(s) + 2] = w16(0xFFFF)
        free.append(s)
    slot = min(free)
    # What entity_alloc (3d43) and the spawn loop (1db8..1dc7) did to the slot.
    base = slot_address(slot)
    image[base + 0x32:base + 0x34] = w16(0)
    image[base + 0x06:base + 0x08] = w16(0x12)
    image[base + 0x0E:base + 0x10] = w16(0)
    image[base + 0x3A:base + 0x3C] = w16(index)
    image[FREE_SLOTS:FREE_SLOTS + 2] = w16(len(free) - 1)
    case.image = oracle.dg.canonical(bytes(image))
    case.slot = slot
    case.regs = {"si": base, "di": rnd.choice([0, base, slot_address(rnd.randrange(ENTITY_COUNT))])}
    return case


def check_init(machine, oracle, case, index):
    want, want_sounds = oracle.run_original(machine, case, f"1000:{init_address(machine, index):04x}")
    state = oracle.dg.from_dgroup(case.image)
    oracle.sound_clear()
    fn = oracle.init_table[index]
    assert fn, f"enemy_init_{index:02x} missing"
    INIT_FN(fn)(ctypes.addressof(state), oracle.dg.entity(state, case.slot))
    got = oracle.dg.to_dgroup(state, case.image)
    if index in SPRITE_REF_INITS:
        want = oracle.with_sprite_frames(want, [case.slot])
    oracle.compare(case, want, got, want_sounds, oracle.port_sounds())


@pytest.mark.parametrize("index", range(INIT_COUNT), ids=lambda i: f"enemy_init_{i:02x}")
@pytest.mark.parametrize("case", fuzz_cases(24))
def test_enemy_init(machine, oracle, index, case):
    rnd = case_rng(f"enemy_init_{index:02x}", case)
    check_init(machine, oracle, spawn_case(machine, oracle, rnd, index), index)


@pytest.mark.parametrize("wave", range(3))
@pytest.mark.parametrize("toggle", range(2))
def test_llama_sprite_refs_are_frames(machine, oracle, wave, toggle):
    """enemy_init_01 copies 218C:0362 + 4*wave + 2*toggle into +2c; for every wave the levels
    use, that entry must be a frame (not a link into the ref table) for the port's
    frame-index sprite_ref to be exact."""
    refs = bind(oracle.lib, "asset_sprite_refs", ctypes.c_void_p)()

    class SpriteRefTable(ctypes.Structure):
        _fields_ = [("count", ctypes.c_uint16), ("refs", ctypes.POINTER(ctypes.c_uint16))]

    table = SpriteRefTable.from_address(refs)
    ref = table.refs[SPRITE_REF_LLAMA + 2 * wave + toggle]
    assert ref < 0x8000
    raw = machine.read(0x218C, 0x362 + 4 * wave + 2 * toggle, 2)
    assert oracle.frame_of_offset(struct.unpack("<H", raw)[0]) == ref
