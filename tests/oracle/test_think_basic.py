"""W5b simple think handlers (src/game/entities/think_basic.c, movement.c) against game86:
think_00 hatch (43dc, also 04/07/0a), think_01 touch-only (43f1) with the player-touch tail
43f7 and entity_hits_player 4b49, think_02 fade (43c9), think_03 swarm (4ac9) with
entity_move_bounce 4e7f, think_09 timer (4ec8), think_0b/0c/0d mother, shooter and shot
(4ed7, 4feb with entity_aim_at_target 509b, 504f), think_10 homing (388a, mover 38a2) and
the beam emitters think_11/12 (382d, 37fe), the other homing users 13/1f/28/2a (256e, 2d6e,
2d5a, 2d35), bouncers 0f/14/19/1a/1b (3cea, 25f1, 20f7, 2112, 148d) and the empty think_2b."""

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle

ACCEL_GROWTH = 0x0826       # think_1f, set by script op 3


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


def hatch_entity(rnd, rec):
    rec.set("scale", rnd.choice([0, 1, 2, 3, rnd.randrange(0x10000), 0x8000, 0x8001]))


def timer_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 1, 0xFFFF, 0x8000, rnd.randrange(0x40)]))
    rec.set("scale", rnd.choice([13, 14, 15, 0x7FFF, rnd.randrange(0x10000)]))


def swarm_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 0, 1, rnd.randrange(40)]))
    rec.set("vx", rnd.randrange(-8, 9))
    rec.set("vy", rnd.randrange(-8, 9))
    if rnd.random() < 0.3:      # at the edges, to exercise the bounce and the y pin
        rec.set("x", rnd.choice([8, 9, 0x137, 0x138]))
        rec.set("y", rnd.choice([0x10, 0x11, 0xbb, 0xbc]))


def shot_entity(rnd, rec):
    rec.set("vx", rnd.randrange(-6, 7))
    rec.set("vy", rnd.randrange(-6, 7))
    if rnd.random() < 0.4:
        rec.set("x", rnd.choice([9, 10, 0x136, 0x137]))
        rec.set("y", rnd.choice([0x11, 0x12, 0xbe, 0xbf]))


def mother_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 0, 1, rnd.randrange(60)]))
    rec.set("vx", rnd.choice([-1, 1, 2]))
    rec.set("vy", rnd.choice([-1, 1, 2]))
    rec.raw[0x16:0x18] = rnd.choice([b"\xff\xff", b"\x00\x00", b"\x05\x00", bytes([rnd.randrange(256), 0xff])])
    rec.raw[0x18] = rnd.randrange(0x40)                    # drop period
    rec.raw[0x19] = rnd.choice([0, 1, 0xff, rnd.randrange(0x40)])
    rec.raw[0x1a:0x1c] = bytes([rnd.choice([0, 1, 3, 8]), 0])
    rec.raw[0x1c:0x1e] = bytes([rnd.randrange(0x40), 0])   # sprite base
    if rnd.random() < 0.3:
        rec.set("x", rnd.choice([8, 9, 10, 0x136, 0x137, 0x138]))
        rec.set("y", rnd.choice([0x10, 0x11, 0x12, 0xbe, 0xbf, 0xc0]))


def shooter_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 0, 1, rnd.randrange(0xa0)]))
    rec.set("vx", rnd.randrange(-2, 3))
    rec.set("vy", rnd.randrange(-1, 2))
    rec.raw[0x16] = rnd.choice([0x3f, rnd.randrange(256)])
    rec.raw[0x17] = rnd.choice([0, 1, 0xff, 0x80, rnd.randrange(256)])


def homing_entity(rnd, rec):
    """16.16 velocity +14/+18, accel +1c/+20, position +26/+2a (integer part = x / y)."""
    import struct
    def fixed(v):
        return struct.pack("<i", v)
    for off in (0x14, 0x18):
        rec.raw[off:off + 4] = fixed(rnd.choice([0, rnd.randrange(-0x40000, 0x40000),
                                                 rnd.randrange(-0x400000, 0x400000)]))
    for off in (0x1c, 0x20):
        rec.raw[off:off + 4] = fixed(rnd.choice([0x800, 0x1000, 0x2000, rnd.randrange(0x10000)]))
    rec.raw[0x26:0x2a] = fixed(rec.get("x") << 16 | rnd.randrange(0x10000))
    rec.raw[0x2a:0x2e] = fixed(rec.get("y") << 16 | rnd.randrange(0x10000))
    if rnd.random() < 0.3:
        rec.raw[0x28:0x2a] = rnd.choice([9, 10, 0x12e, 0x12f]).to_bytes(2, "little")
        rec.raw[0x2c:0x2e] = rnd.choice([0x11, 0x12, 0xb5, 0xb6]).to_bytes(2, "little")
    rec.raw[0x30:0x32] = rnd.choice([b"\xff\xff", b"\xff\xff", b"\x00\x00", b"\x05\x00"])
    rec.raw[0x24] = rnd.randrange(1, 0x20)
    rec.raw[0x25] = rnd.choice([0, 0, 1, rnd.randrange(0x20)])
    rec.raw[0x2e] = rnd.randrange(1, 0x40)
    rec.raw[0x2f] = rnd.choice([1, 2, 0, rnd.randrange(0x40)])


CASES = {
    0x00: hatch_entity, 0x04: hatch_entity, 0x07: hatch_entity, 0x0a: hatch_entity,
    0x01: None,
    0x02: timer_entity,
    0x03: swarm_entity,
    0x09: timer_entity,
    0x0b: mother_entity,
    0x0c: shooter_entity,
    0x0d: shot_entity,
    0x10: homing_entity,
    0x11: homing_entity,
    0x12: homing_entity,
    0x0f: swarm_entity, 0x14: swarm_entity, 0x19: swarm_entity, 0x1a: swarm_entity,
    0x1b: swarm_entity,
    0x13: homing_entity, 0x1f: homing_entity, 0x28: homing_entity, 0x2a: homing_entity,
    0x2b: None,
}


@pytest.mark.parametrize("think", sorted(CASES), ids=lambda t: f"think_{t:02x}")
@pytest.mark.parametrize("case", fuzz_cases(48))
def test_think(machine, oracle, think, case):
    rnd = case_rng(f"think_{think:02x}", case)
    c = oracle.make_case(machine, rnd, think, CASES[think],
                         extra={ACCEL_GROWTH: rnd.choice([0, 1, 0x10, rnd.randrange(0x10000)])})
    oracle.check(machine, c, think)
