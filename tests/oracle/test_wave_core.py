"""W4 wave core against game86 in unicorn: level_select_variant (1000:5350, script.c),
wave_spawn_enemies (1000:1ced, the roster rebuild before a retry, tick.c), the think loop
entities_think_all (1000:432b, entity_loop.c: the target ship per slot, then the handler) and
the whole play tick hook play_tick_hook (1000:1562, tick.c: timers, players, fire, shots, the
think loop, the swarm re-aim and the wave's spawning).

Each case is a random but valid DGROUP image from ThinkOracle.make_case (entity table, ships,
wave state, RNG table, sound status); both sides run from it and every byte the adapter maps
is compared, plus the sound-driver requests. The tick hook cases run with a free entity table
(the think handlers have their own tests) and random keys / controls; about half of them spawn,
fire or move the trail.
"""

import ctypes
import struct

import pytest
from generators import case_rng, entity_table, fuzz_cases
from think_oracle import THINK_COUNT, ThinkCase, ThinkOracle

from adapters.cport import bind
from adapters.entities import ENTITY_COUNT, ENTITY_SIZE, ENTITY_TABLE
from adapters.gamestate import SCRATCH

LEVEL = 0x21AA
ROSTER, ROSTER_SIZE = 0x220A, 50
ENTITY_KIND = 0x3A          # word: kind (low byte signed in 1d15's test), -1 = none
CS_SAVED_DS = (0x1000, 0x0255)  # DGROUP, stored by start-up (1000:0040); 1cf1 loads ES from it


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


@pytest.fixture(scope="module")
def port(cgame):
    class Port:
        select_variant = bind(cgame, "level_select_variant", None, ctypes.c_void_p,
                              ctypes.c_void_p)
        spawn_enemies = bind(cgame, "wave_spawn_enemies", None, ctypes.c_void_p)
        play_tick = bind(cgame, "play_tick_hook", None, ctypes.c_void_p)
        think_all = bind(cgame, "entities_think_all", None, ctypes.c_void_p)
        levels = bind(cgame, "asset_levels", ctypes.c_void_p)()
    if not Port.levels:
        pytest.skip("extracted data not available")
    return Port


def edited(oracle, case, words=None, raw=None):
    img = bytearray(case.image)
    for off, v in (words or {}).items():
        struct.pack_into("<H", img, off, v & 0xFFFF)
    for off, data in (raw or {}).items():
        img[off:off + len(data)] = data
    return ThinkCase(oracle.dg.canonical(bytes(img)), case.slot, case.status)


def check(oracle, machine, case, entry, port_call, ignore=()):
    machine.write_u16(*CS_SAVED_DS, machine.dgroup)
    want, want_sounds = oracle.run_original(machine, case, None, entry)
    got, got_sounds = oracle.run_port(case, None, port_call)
    bad = oracle.dg.mismatches(want, got, ignore=ignore)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"
    assert got_sounds == want_sounds


# ---- level_select_variant (5350) -------------------------------------------------------

@pytest.mark.parametrize("case", fuzz_cases(32))
def test_level_select_variant(machine, oracle, port, case):
    rnd = case_rng("wave_core.select_variant", case)
    c = oracle.make_case(machine, rnd, rnd.randrange(THINK_COUNT))
    level = rnd.choice([rnd.randrange(100), rnd.randrange(0x10000)])
    c = edited(oracle, c, {LEVEL: level})
    check(oracle, machine, c, "1000:5350",
          lambda s: port.select_variant(ctypes.addressof(s), port.levels))


# ---- wave_spawn_enemies (1ced) ---------------------------------------------------------

def kinds_table(rnd, distinct):
    """Give the live records kinds from a pool of `distinct` values (sometimes more than the
    31 roster entries 1d1c scans, and sometimes with a negative low byte)."""
    pool = [rnd.randrange(0x2A) for _ in range(distinct)]
    raw = {}
    for i in range(ENTITY_COUNT):
        off = ENTITY_TABLE + i * ENTITY_SIZE + ENTITY_KIND
        kind = rnd.choice(pool)
        if rnd.random() < 0.05:
            kind = rnd.choice([0xFFFF, 0x0080 | rnd.randrange(0x80), rnd.randrange(0x10000)])
        raw[off] = struct.pack("<H", kind)
    return raw


@pytest.mark.parametrize("case", fuzz_cases(48))
def test_wave_spawn_enemies(machine, oracle, port, case):
    rnd = case_rng("wave_core.spawn_enemies", case)
    c = oracle.make_case(machine, rnd, rnd.randrange(THINK_COUNT))
    distinct = rnd.choice([1, 3, 8, 20, 31, 32, 40])
    raw = kinds_table(rnd, distinct)
    raw[ROSTER] = rnd.randbytes(2 * ROSTER_SIZE)        # whatever the last try left there
    c = edited(oracle, c, raw=raw)
    check(oracle, machine, c, "1000:1ced",
          lambda s: port.spawn_enemies(ctypes.addressof(s)))


# ---- entities_think_all (432b) --------------------------------------------------------

LOOP_THINKS = 4                 # hatch, touch, fade, swarm (aims at the slot's target ship)
OPTIONS, OPT_DOUBLE = 0x2CF6, 0x02


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_entities_think_all(machine, oracle, port, case):
    rnd = case_rng("wave_core.think_all", case)
    c = oracle.make_case(machine, rnd, rnd.randrange(LOOP_THINKS))
    records = entity_table(rnd, n_think=LOOP_THINKS)
    raw = {ENTITY_TABLE: b"".join(bytes(r.raw) for r in records),
           OPTIONS: bytes([c.image[OPTIONS] | (OPT_DOUBLE if rnd.random() < 0.5 else 0)])}
    c = edited(oracle, c, raw=raw)
    check(oracle, machine, c, "1000:432b",
          lambda s: port.think_all(ctypes.addressof(s)), ignore=SCRATCH)


# ---- play_tick_hook (1562) -------------------------------------------------------------

FRAME_DRAWN = 0x0429                        # byte: the tick hook waits for the first frame
LAST_MAKE_CODE = 0x0586
CONTROL_A, CONTROL_B = 0x07FC, 0x07FB       # controller 1 / 2 control bytes
KEY_P, KEY_X = 0x19, 0x2D


class InputPrefix(ctypes.Structure):
    """The head of the engine's InputState (src/engine/input.h) up to controls[]."""
    _fields_ = [("device", ctypes.c_int), ("single_stick", ctypes.c_bool),
                ("keys_held", ctypes.c_uint8), ("last_scan_code", ctypes.c_uint8),
                ("last_make_code", ctypes.c_uint8), ("last_make_code_copy", ctypes.c_uint8),
                ("unsampled", ctypes.c_uint8), ("release_pending", ctypes.c_uint8),
                ("controls", ctypes.c_uint8 * 2)]


@pytest.fixture(scope="module")
def keys(cgame):
    return ctypes.cast(bind(cgame, "input_state", ctypes.c_void_p)(),
                       ctypes.POINTER(InputPrefix)).contents


def control_byte(rnd):
    return rnd.choice([0, 0x80]) | rnd.choice([0, rnd.randrange(16)])


def free_entities():
    """Every record free (type -1, rest 0): the tick's own work without the think handlers,
    which tests/oracle/test_think_*.py check one by one."""
    free = b"\xff\xff" + bytes(ENTITY_SIZE - 2)
    return {ENTITY_TABLE: free * ENTITY_COUNT}


@pytest.mark.parametrize("case", fuzz_cases(96))
def test_play_tick_hook(machine, oracle, port, keys, case):
    rnd = case_rng("wave_core.play_tick", case)
    c = oracle.make_case(machine, rnd, rnd.randrange(THINK_COUNT))
    make = rnd.choice([0] * 6 + [KEY_P, KEY_X, rnd.randrange(0x80)])
    controls = (control_byte(rnd), control_byte(rnd))
    raw = free_entities()
    raw.update({FRAME_DRAWN: bytes([rnd.random() < 0.9]), LAST_MAKE_CODE: bytes([make]),
                CONTROL_A: bytes([controls[0]]), CONTROL_B: bytes([controls[1]])})
    c = edited(oracle, c, raw=raw)

    def run(state):
        keys.last_make_code = make
        keys.controls[0], keys.controls[1] = controls
        try:
            port.play_tick(ctypes.addressof(state))
        finally:                            # the testlib's InputState is shared by all tests
            keys.last_make_code = 0
            keys.controls[0] = keys.controls[1] = 0
    check(oracle, machine, c, "1000:1562", run, ignore=SCRATCH)
