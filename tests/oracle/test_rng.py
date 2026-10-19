"""First oracle proof: the RNG routines of game86, run in unicorn.

The reference functions below state the routines' behaviour in plain terms; they
stand in for the C port (src/game) until it exists, then the same cases will be
fed to the C functions through tests/adapters.
"""

import random

import pytest

from adapters.rng import (MIXER, PERM_INDEX, PERM_SIZE, PERM_TABLE, RngState,
                          rng_from_dgroup, rng_to_dgroup)


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


@pytest.mark.parametrize("seed", range(64))
def test_next_byte(machine, seed):
    rnd = random.Random(seed)
    st = RngState(table=bytes(rnd.randrange(256) for _ in range(PERM_SIZE)),
                  index=rnd.choice([0, 0x1FF, rnd.randrange(0x200)]))
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


@pytest.mark.parametrize("seed", range(64))
def test_mixer(machine, seed):
    rnd = random.Random(1000 + seed)
    mixer = tuple(rnd.randrange(0x10000) for _ in range(4))
    # Half the cases hit the "BIOS tick == s2" perturbation branch.
    ticks = mixer[2] if seed % 2 else rnd.randrange(0x10000)
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


@pytest.mark.parametrize("seed", range(4))
def test_init_shuffle(machine, seed):
    """Whole start-up table build from a given BIOS tick seed (as 1000:5f6b sets it)."""
    rnd = random.Random(2000 + seed)
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
