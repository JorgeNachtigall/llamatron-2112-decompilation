"""W5a: the 10 pickup effects (tbl_pickup_effects, CS:0F7E; src/game/entities/pickups.c)
against game86.

The original is entered where think_1c dispatches them (1000:0f64): the pickup has just been
freed, DI = its record (+14 = the kind), AX = the TOUCH_* bits from entity_hits_player. The port
runs pickup_collect on the same record; deadly_touch (kind 9) is also called directly."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from think_oracle import w16
from w5a_oracle import PICKUP_COUNT, W5aOracle, slot_address

TOUCH_BITS = (1, 2, 3)          # TOUCH_P1, TOUCH_P2, both
DISPATCH = "1000:0f64"
DEADLY_TOUCH = "1000:37c2"


@pytest.fixture(scope="module")
def oracle(cgame):
    o = W5aOracle(cgame)
    o.deadly_touch = bind(cgame, "deadly_touch", None, ctypes.c_void_p, ctypes.c_uint)
    return o


def pickup_case(machine, oracle, rnd, kind):
    def tighten(rnd, rec):
        rec.raw[0x14:0x16] = w16(kind)
    case = oracle.random_image(machine, rnd, 0x1C, tighten)
    image = bytearray(case.image)
    base = slot_address(case.slot)
    image[base:base + 2] = w16(0xFFFF)          # think_1c freed it (1000:0f60)
    case.image = oracle.dg.canonical(bytes(image))
    case.touched = rnd.choice(TOUCH_BITS)
    case.regs = {"di": base, "ax": case.touched, "si": rnd.randrange(0x10000),
                 "cx": rnd.randrange(0x10000), "dx": rnd.randrange(0x10000)}
    return case


def check(machine, oracle, case, entry, run_port):
    want, want_sounds = oracle.run_original(machine, case, entry)
    state = oracle.dg.from_dgroup(case.image)
    oracle.sound_clear()
    run_port(state)
    got = oracle.dg.to_dgroup(state, case.image)
    oracle.compare(case, want, got, want_sounds, oracle.port_sounds())


@pytest.mark.parametrize("kind", range(PICKUP_COUNT), ids=lambda k: f"pickup_effect_{k:02x}")
@pytest.mark.parametrize("case", fuzz_cases(32))
def test_pickup_effect(machine, oracle, kind, case):
    rnd = case_rng(f"pickup_effect_{kind:02x}", case)
    c = pickup_case(machine, oracle, rnd, kind)
    check(machine, oracle, c, DISPATCH, lambda state: oracle.pickup_collect(
        ctypes.addressof(state), oracle.dg.entity(state, c.slot), c.touched))


@pytest.mark.parametrize("case", fuzz_cases(32))
def test_deadly_touch_any_value(machine, oracle, case):
    """think_11/12 enter 37c2 with a distance (< 10) in AX, not touch bits."""
    rnd = case_rng("deadly_touch", case)
    c = pickup_case(machine, oracle, rnd, 9)
    c.touched = rnd.randrange(10)
    c.regs["ax"] = c.touched
    check(machine, oracle, c, DEADLY_TOUCH,
          lambda state: oracle.deadly_touch(ctypes.addressof(state), c.touched))
