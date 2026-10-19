"""W5b llamas and the snake (src/game/entities/think_llamas.c, think_snake.c) against game86:
think_05 llama (4c46, rescue 4ca3..4df2), think_06 follower (4bfb), think_08 hunter (495f with
the search 498e), think_1d snake head (36a0 with the spawn 366f and the mover 38a2), think_1e
snake segment (36f6)."""

import struct

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle
from test_think_basic import homing_entity

from adapters.entities import ENTITY_COUNT, ENTITY_SIZE, ENTITY_TABLE

THINK_LLAMA, THINK_FOLLOWER = 0x05, 0x06
FOLLOWER_LAG = 0x06BA       # 2 words, trail bytes
SNAKE_HEAD = 0x0870
UNHATCHED_LEFT = 0x08F8
UNHATCHED = 0x091B
MARKER_PENDING = 0x091F
MARKER_Y = 0x0921           # 2 words
POWERUP = 0x21C4
PICKUP_PENDING = 0x21C6
LOOP_COUNT = 0x233C
LEVEL_FLAGS = 0x233E
TRAIL_HEAD = 0x2CF7         # 2 words, trail bytes
TRAIL_P1, TRAIL_P2 = 0x6CC9, 0x70C9
TRAIL_BYTES = 0x400
SNAKE_TRAIL = 0x4481
INTRO_TIMER = 0x08FC
TICKS = 0x39E6


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


def follower_fixup(image: bytes) -> bytes:
    """Followers keep the trail they walk as a player index (kill_action 0/1); the original
    keeps the trail's DS address."""
    out = bytearray(image)
    for slot in range(ENTITY_COUNT):
        base = ENTITY_TABLE + ENTITY_SIZE * slot
        if out[base] != THINK_FOLLOWER:
            continue
        trail = struct.unpack_from("<H", out, base + 0x1E)[0]
        if trail in (TRAIL_P1, TRAIL_P2):
            struct.pack_into("<H", out, base + 0x1E, 0 if trail == TRAIL_P1 else 1)
    return bytes(out)


def llama_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 1, rnd.randrange(0x20), 0xFFFF]))
    rec.set("vx", rnd.randrange(-1, 2))
    rec.set("vy", rnd.randrange(-1, 2))
    rec.raw[0x16:0x18] = rnd.choice([0, 2, 4, 6]).to_bytes(2, "little")   # sprite offset
    rec.raw[0x30:0x32] = rnd.randrange(0x60, 0x80).to_bytes(2, "little")


def llama_words(rnd, records, slot):
    return {
        UNHATCHED_LEFT: rnd.choice([1, 1, 2, rnd.randrange(0x10)]),
        UNHATCHED: rnd.randrange(0x10),
        MARKER_PENDING: rnd.choice([0, 1]),
        MARKER_Y: rnd.randrange(0x100), MARKER_Y + 2: rnd.randrange(0x100),
        POWERUP: rnd.choice([0, 1, 0xFFFF, rnd.randrange(0x10000)]),
        PICKUP_PENDING: rnd.choice([0, rnd.randrange(11)]),
        LOOP_COUNT: rnd.choice([0, 0, 1, 2, 3, rnd.randrange(0x40)]),
        LEVEL_FLAGS: rnd.randrange(0x100),
        FOLLOWER_LAG: 8 * rnd.randrange(0x40), FOLLOWER_LAG + 2: 8 * rnd.randrange(0x40),
    }


def follower_entity(rnd, rec):
    rec.set("timer", rnd.choice([0x28, 0x50, 8 * rnd.randrange(0x80), rnd.randrange(0x10000)]))
    rec.set("kill_action", rnd.choice([TRAIL_P1, TRAIL_P2]))
    rec.raw[0x30:0x32] = rnd.randrange(0x60, 0x80).to_bytes(2, "little")


def trail_words(rnd, records, slot):
    words = {TRAIL_HEAD: 8 * rnd.randrange(0x80), TRAIL_HEAD + 2: 8 * rnd.randrange(0x80)}
    for base in (TRAIL_P1, TRAIL_P2):
        for off in range(0, TRAIL_BYTES, 8):
            words[base + off] = rnd.randrange(0x10, 0x130)
            words[base + off + 2] = rnd.randrange(0x10, 0xb0)
            words[base + off + 4] = rnd.choice([0, 2, 4, 6, rnd.randrange(9)])
    return words


def llamas(rnd, records, slot, near=None):
    """Some llamas (hp 0 = unclaimed / 1 = claimed), some near `near`, no other think-5s."""
    picks = [i for i in rnd.sample(range(ENTITY_COUNT), rnd.randrange(0, 8)) if i != slot]
    if rnd.random() < 0.5 and slot != 0:
        picks.append(0)
    for i in picks:
        r = records[i]
        if r.free:
            r.raw[:] = bytes(rnd.randrange(256) for _ in range(ENTITY_SIZE))
        r.set("type", THINK_LLAMA | rnd.randrange(46) << 8)
        r.set("hp", rnd.choice([0, 0, 1]))
        if near is not None and rnd.random() < 0.5:
            r.set("x", near.get("x") + rnd.randrange(-9, 10))
            r.set("y", near.get("y") + rnd.randrange(-9, 10))
    for i, r in enumerate(records):
        if i not in picks and i != slot and not r.free and r.get("type") & 0xFF == THINK_LLAMA:
            r.set("type", r.get("type") ^ 1)
    return picks


def hunter_entity(rnd, rec):
    rec.set("timer", rnd.choice([0xFFFF, 0xFFFE, 0xFFFE, 0, 0, rnd.randrange(ENTITY_COUNT),
                                 0xFFFD]))
    rec.set("vx", rnd.randrange(-3, 4))
    rec.set("vy", rnd.randrange(-3, 4))
    rec.raw[0x16:0x18] = rnd.choice([0, 0, 1, 5]).to_bytes(2, "little")   # flash frames


def hunter_words(rnd, records, slot):
    hunter = records[slot]
    picks = llamas(rnd, records, slot, near=hunter)
    target = hunter.get("timer")
    if 0 < target < 0x8000:
        # The original reads the byte at table + target (see hunter_quarry); keep it off 5,
        # the stray-record case the port does not reproduce.
        rec, off = records[target // ENTITY_SIZE], target % ENTITY_SIZE
        if rec.raw[off] == THINK_LLAMA:
            rec.raw[off] ^= 1
    near = rnd.random() < 0.4
    cx = hunter.get("x") + rnd.randrange(-9, 10) if near else rnd.randrange(0x10, 0x130)
    cy = hunter.get("y") + rnd.randrange(-9, 10) if near else rnd.randrange(0x10, 0xb0)
    return {UNHATCHED: rnd.randrange(len(picks) + 2),
            INTRO_TIMER: rnd.choice([0, 0, 0, 0, 1]),
            TICKS: rnd.choice([0, 8 * rnd.randrange(0x2000), rnd.randrange(0x10000)]),
            0x4883: cx, 0x4885: cy}


def head_entity(rnd, rec):
    homing_entity(rnd, rec)
    rec.set("vx", rnd.randrange(0x40))
    rec.raw[0x0a] = rnd.randrange(1, 0x20)                  # spawn period
    rec.raw[0x0b] = rnd.choice([0, 0, 1, rnd.randrange(0x20)])


def segment_entity(rnd, rec):
    rec.set("timer", rnd.choice([1, 4, 8, rnd.randrange(0x10000)]))
    rec.set("hp", rnd.choice([-1, 0, 3]))


def snake_words(rnd, records, slot):
    words = {SNAKE_HEAD: rnd.randrange(0x40)}
    for i in range(0x40):
        base = SNAKE_TRAIL + 8 * i
        words[base] = rnd.randrange(0x10, 0x130)
        words[base + 2] = rnd.randrange(0x10, 0xb0)
        words[base + 4] = rnd.randrange(0xc0, 0xd0)
    return words


CASES = {
    0x05: (llama_entity, llama_words, 0.6),
    0x06: (follower_entity, trail_words, 0.3),
    0x08: (hunter_entity, hunter_words, 0.3),
    0x1d: (head_entity, snake_words, 0.3),
    0x1e: (segment_entity, snake_words, 0.3),
}


@pytest.mark.parametrize("think", sorted(CASES), ids=lambda t: f"think_{t:02x}")
@pytest.mark.parametrize("case", fuzz_cases(96))
def test_think(machine, oracle, think, case):
    rnd = case_rng(f"think_{think:02x}", case)
    entity, table, near = CASES[think]
    c = oracle.make_case(machine, rnd, think, entity, near_ship=near, table=table)
    oracle.check(machine, c, think, fixup=follower_fixup)
