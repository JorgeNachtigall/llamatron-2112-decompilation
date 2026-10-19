"""RNG routines of game86 in unicorn vs a Python reference and vs the port's C
functions (src/game/rng.c via llamatron_game_testlib; the test_c_* cases skip when
the library is not built)."""

import pytest
from generators import case_rng, fuzz_cases, rng_state

import ctypes

from adapters.rng import (MIXER, PERM_INDEX, PERM_SIZE, PERM_TABLE, CRngApi, CRngMixer,
                          RngState, mixer_from_c, mixer_to_c, rng_from_c, rng_from_dgroup,
                          rng_to_c, rng_to_dgroup)


def ror16(v, n):
    return ((v >> n) | (v << (16 - n))) & 0xFFFF


def ref_next_byte(st: RngState):
    """game86 1000:e4de: next byte of the permutation table, index wraps at 512."""
    value = st.table[st.index & 0x1FF]
    return value, RngState(st.table, (st.index + 1) & 0x1FF, st.mixer)


def ref_mix(mixer, bios_ticks):
    """game86 1000:e616: shuffle-time mixer, perturbed by the BIOS tick low word."""
    s0, s1, s2, s3 = mixer
    ax = ror16((ror16(s0, 3) + s1 + s2) & 0xFFFF, 2)
    ticks = bios_ticks & 0xFFFF
    nxt = ticks
    if ticks == s2:
        s3 = (s3 + ticks + 0x2F) & 0xFFFF
        nxt = ticks ^ s3
    return ax, (s1, s2, nxt, s3)


def ref_init_table(mixer, bios_ticks):
    """game86 1000:007b..00ab: two copies of 0..255, then 4000 random swaps."""
    table = bytearray(i & 0xFF for i in range(PERM_SIZE))
    for _ in range(0xFA0):
        a, mixer = ref_mix(mixer, bios_ticks)
        b, mixer = ref_mix(mixer, bios_ticks)
        a &= 0x1FF
        b &= 0x1FF
        table[a], table[b] = table[b], table[a]
    return bytes(table), mixer


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_next_byte(machine, case):
    st = rng_state(case_rng("next_byte", case))
    rng_to_dgroup(machine, st)
    want_value, want_state = ref_next_byte(st)

    res = machine.call("1000:e4de")

    assert res.regs["bx"] == want_value          # BH is cleared, BL = table byte
    assert rng_from_dgroup(machine) == want_state
    # Nothing but the index word may change.
    assert set(res.dgroup_diff) <= {PERM_INDEX, PERM_INDEX + 1}
    assert all(PERM_INDEX <= d.start - machine.dgroup * 16 < PERM_INDEX + 2
               for d in res.memory_diff)
    assert not res.interrupts and not res.ports


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_mixer(machine, case):
    rnd = case_rng("mixer", case)
    mixer = rng_state(rnd).mixer
    # Half the cases hit the "BIOS tick == s2" perturbation branch.
    ticks = mixer[2] if case % 2 else rnd.randrange(0x10000)
    ticks |= rnd.randrange(0x10000) << 16          # high word is never read
    rng_to_dgroup(machine, RngState(mixer=mixer))
    machine.set_bios_ticks(ticks)
    want_ax, want_mixer = ref_mix(mixer, ticks)

    res = machine.call("1000:e616", regs={"bx": 0x1234})

    assert res.regs["ax"] == want_ax
    assert res.regs["bx"] == 0x1234                 # preserved (push/pop)
    assert res.regs["es"] == 0x0000                 # left at [0x44d] = 0 (0000:046C)
    assert rng_from_dgroup(machine).mixer == want_mixer
    assert set(res.dgroup_diff) <= set(range(MIXER, MIXER + 8))


@pytest.mark.parametrize("case", fuzz_cases(4))
def test_init_shuffle(machine, case):
    """Whole start-up table build from a given BIOS tick seed (as 1000:5f6b sets it)."""
    rnd = case_rng("init_shuffle", case)
    ticks = rnd.randrange(0x1800B0)                 # a valid time of day
    lo, hi = ticks & 0xFFFF, ticks >> 16
    mixer = (lo, hi, lo, hi)                        # 1000:5f6b: [453]=[457]=lo, [455]=[459]=hi
    rng_to_dgroup(machine, RngState(mixer=mixer))
    machine.set_bios_ticks(ticks)
    want_table, want_mixer = ref_init_table(mixer, ticks)

    machine.run_until("1000:007b", "1000:00ab")

    st = rng_from_dgroup(machine)
    assert st.table == want_table
    assert st.mixer == want_mixer
    assert sorted(st.table) == sorted(i & 0xFF for i in range(PERM_SIZE))


# ---- the port's C functions against the original ------------------------------------

SHUFFLE_CALLS = 2 * 4000


@pytest.fixture(scope="module")
def crng(cgame):
    return CRngApi(cgame)


def oracle_seed(machine, bios_ticks):
    """game86 1000:5f6b..5f89: seed words from the BIOS tick count."""
    machine.set_bios_ticks(bios_ticks)
    machine.run_until("1000:5f6b", "1000:5f89")
    return rng_from_dgroup(machine).mixer


def oracle_shuffle(machine, mixer, schedule):
    """game86 1000:007b..00ab with the BIOS tick word set to schedule(n) right
    before mixer call n reads it (1000:e640: MOV BX,ES:[046C])."""
    rng_to_dgroup(machine, RngState(mixer=mixer))
    calls = [0]

    def before_tick_read(m):
        m.set_bios_ticks(schedule(calls[0]))
        calls[0] += 1

    bp = machine.add_breakpoint("1000:e640", before_tick_read)
    try:
        machine.run_until("1000:007b", "1000:00ab")
    finally:
        machine.remove_breakpoint(bp)
    assert calls[0] == SHUFFLE_CALLS
    return rng_from_dgroup(machine)


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_c_next_byte(machine, crng, case):
    st = rng_state(case_rng("c_next_byte", case))     # index stays 0..511 (game invariant)
    rng_to_dgroup(machine, st)
    res = machine.call("1000:e4de")
    c = rng_to_c(st)
    assert crng.next_byte(ctypes.byref(c)) == res.regs["bx"]
    assert rng_from_c(c) == rng_from_dgroup(machine)


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_c_mix(machine, crng, case):
    rnd = case_rng("c_mix", case)
    mixer = rng_state(rnd).mixer
    tick = mixer[2] if case % 2 else rnd.randrange(0x10000)     # both branches
    rng_to_dgroup(machine, RngState(mixer=mixer))
    machine.set_bios_ticks(tick)
    res = machine.call("1000:e616")
    c = mixer_to_c(mixer)
    assert crng.mix(ctypes.byref(c), tick) == res.regs["ax"]
    assert mixer_from_c(c) == rng_from_dgroup(machine).mixer


@pytest.mark.parametrize("case", fuzz_cases(16))
def test_c_seed(machine, crng, case):
    ticks = case_rng("c_seed", case).randrange(0x1800B0)
    c = CRngMixer()
    crng.seed(ctypes.byref(c), ticks)
    assert mixer_from_c(c) == oracle_seed(machine, ticks)


@pytest.mark.parametrize("case", fuzz_cases(4))
def test_c_init_constant(machine, crng, case):
    ticks = case_rng("c_init", case).randrange(0x1800B0)
    want = oracle_shuffle(machine, oracle_seed(machine, ticks), lambda n: ticks)
    c = rng_to_c(RngState())
    crng.init_constant(ctypes.byref(c), ticks)
    got = rng_from_c(c)
    assert (got.table, got.mixer) == (want.table, want.mixer)


SCHEDULES = {
    # The tick advances every `step` mixer calls, as on a machine slower than the shuffle.
    "every_call": lambda t0: (lambda n: t0 + n),
    "every_37": lambda t0: (lambda n: t0 + n // 37),
    "every_500": lambda t0: (lambda n: t0 + n // 500),
    "once_midway": lambda t0: (lambda n: t0 + (n >= 3999)),
    "wraps_16bit": lambda t0: (lambda n: 0xFFFE + n // 1000),
}


@pytest.mark.parametrize("name", sorted(SCHEDULES))
def test_c_shuffle_schedule(machine, crng, name):
    t0 = case_rng(f"c_schedule:{name}", 0).randrange(0x1800B0)
    schedule = SCHEDULES[name](t0)
    mixer = oracle_seed(machine, t0)
    want = oracle_shuffle(machine, mixer, schedule)
    c = rng_to_c(RngState(mixer=mixer))
    crng.shuffle_with(c, schedule)
    got = rng_from_c(c)
    assert got.table == want.table
    assert got.mixer == want.mixer


def test_schedule_matters(machine):
    """Guard for the schedule tests: a tick that moves during the shuffle really
    gives a different table than a constant one (so the breakpoint takes effect)."""
    t0 = 0x0012ABCD
    mixer = oracle_seed(machine, t0)
    constant = oracle_shuffle(machine, mixer, lambda n: t0)
    moving = oracle_shuffle(machine, mixer, SCHEDULES["every_37"](t0))
    assert constant.table != moving.table
    assert sorted(moving.table) == sorted(i & 0xFF for i in range(PERM_SIZE))
