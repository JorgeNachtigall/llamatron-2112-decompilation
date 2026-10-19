"""W4 entity loops (src/game/entity_loop.c) against game86: entities_clear (4307) and
entity_alloc (3d43)."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DgroupMap

ENTITY_TABLE, ENTITY_SIZE, ENTITY_COUNT = 0x48C1, 0x40, 70


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


def random_table(rnd, free_share):
    """70 records of random bytes; a share of them free (type word 0xFFFF), and some with only
    one negative byte, the two encodings the loops tell apart."""
    out = bytearray()
    for _ in range(ENTITY_COUNT):
        rec = bytearray(rnd.randbytes(ENTITY_SIZE))
        r = rnd.random()
        if r < free_share:
            rec[0:2] = b"\xff\xff"
        elif r < free_share + 0.1:
            rec[0:2] = bytes([rnd.randrange(0x80), 0x80 | rnd.randrange(0x80)])   # word < 0
        else:
            rec[0:2] = bytes([rnd.randrange(0x100), rnd.randrange(0x80)])         # word >= 0
        out += rec
    return bytes(out)


def image_with(machine, dg, table):
    img = bytearray(machine.read_dgroup(0, 0x10000))
    img[ENTITY_TABLE:ENTITY_TABLE + len(table)] = table
    return dg.canonical(bytes(img))


@pytest.mark.parametrize("case", fuzz_cases(16))
def test_entities_clear(machine, dg, cgame, case):
    rnd = case_rng("entities_clear", case)
    img = image_with(machine, dg, random_table(rnd, 0.3))
    machine.write_dgroup(0, img)
    state = dg.from_dgroup(img)

    machine.call("1000:4307")
    bind(cgame, "entities_clear", None, ctypes.c_void_p)(state)

    assert dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, img)) == []


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_entity_alloc(machine, dg, cgame, case):
    rnd = case_rng("entity_alloc", case)
    # Some tables completely full, to cover the 'no free slot' exit.
    img = image_with(machine, dg, random_table(rnd, 0.0 if case % 8 == 0 else rnd.random() * 0.5))
    machine.write_dgroup(0, img)
    state = dg.from_dgroup(img)

    res = machine.call("1000:3d43")
    found = res.regs["bp"] == 0xFFFF
    got = bind(cgame, "entity_alloc", ctypes.c_void_p, ctypes.c_void_p)(state)

    if found:
        slot = (res.regs["si"] - ENTITY_TABLE) // ENTITY_SIZE
        assert got == dg.entity(state, slot)
    else:
        assert res.regs["bp"] == 0
        assert got is None
    assert dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, img)) == []
