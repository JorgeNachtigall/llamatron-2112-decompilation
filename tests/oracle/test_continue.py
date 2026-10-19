"""W5b continue.c against game86: continue_begin (1000:07d6..07dc), continue_tick_hook (0816)
and continue_draw_hook (0894: pages and the dirty-list restore, as in test_attract)."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases
from test_attract import (CODE0, CS_G3_SEG, CS_SPRITE_SEG, G3_SEG, SCRATCH, SPRITE_SEG, C,
                          check_pages, page_offset, restore_frame, setup_machine, setup_pages)
from test_sprites import Sprite
from think_oracle import ThinkCase, ThinkOracle

from adapters.cport import bind

CONTROL_A = 0x07FC
CONTINUES, SECONDS, TICKS = 0x061F, 0x0621, 0x0623
TICK_HOOK = 0x39E4
CONTINUE_TICK_HOOK = 0x0816
SHAPE_CONTINUE = 2
DRAW_STATE = {0x39D7, 0x39D8, 0x39D9}
IGNORE = set(SCRATCH) | DRAW_STATE

LOOKUP = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint)


class ContinueArt(ctypes.Structure):
    _fields_ = [("sprite_ref", ctypes.c_void_p), ("shape", ctypes.c_void_p)]


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


@pytest.fixture(scope="module")
def c(cgame):
    c = C(cgame)
    c.begin = bind(cgame, "continue_begin", None, ctypes.c_void_p)
    c.tick_hook = bind(cgame, "continue_tick_hook", None, ctypes.c_void_p)
    c.draw_hook = bind(cgame, "continue_draw_hook", None, ctypes.c_void_p,
                       ctypes.POINTER(c.DrawTarget), ctypes.POINTER(ContinueArt))
    return c


def make_case(oracle, machine, rnd):
    img = bytearray(oracle.make_case(machine, rnd, 0x01).image)
    img[CONTROL_A] = rnd.choice([0, 0x80]) | rnd.randrange(16)
    struct.pack_into("<hhh", img, CONTINUES, rnd.choice([1, 2, 3, rnd.randrange(10)]),
                     rnd.choice([0, 1, 9, -1, rnd.randrange(10)]),
                     rnd.choice([1, 1, 2, 0x32, -1, 0, rnd.randrange(0x33)]))
    struct.pack_into("<H", img, TICK_HOOK, CONTINUE_TICK_HOOK)
    return ThinkCase(oracle.dg.canonical(bytes(img)), 0, (0, 0))


def compare(oracle, want, got):
    bad = oracle.dg.mismatches(want, got, ignore=IGNORE)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"


@pytest.mark.parametrize("case", fuzz_cases(8))
def test_continue_begin(machine, oracle, c, case):
    case_ = make_case(oracle, machine, case_rng("continue_begin", case))
    want, _ = oracle.run_original(machine, case_, None, "1000:07d6", "1000:07e2")
    got, _ = oracle.run_port(case_, None, lambda state: c.begin(ctypes.addressof(state)))
    compare(oracle, want, got)


@pytest.mark.parametrize("case", fuzz_cases(128))
def test_continue_tick_hook(machine, oracle, c, case):
    case_ = make_case(oracle, machine, case_rng("continue_tick_hook", case))
    want, _ = oracle.run_original(machine, case_, None, "1000:0816")

    def run(state):
        c.controls[0] = case_.image[CONTROL_A]
        c.tick_hook(ctypes.addressof(state))
    got, _ = oracle.run_port(case_, None, run)
    compare(oracle, want, got)        # incl. the tick hook (3fde hook_nop after fire)


class Art:
    """Random digit sprites (refs 4..13 at SPRITE_SEG:[2n]) and the label shape."""

    def __init__(self, machine, rnd):
        self.refs, blob, start = {}, bytearray(), 0x40
        for n in range(4, 14):
            s = Sprite(rnd, columns=1)
            machine.write_u16(SPRITE_SEG, 2 * n, start + len(blob))
            blob += s.original_bytes()
            self.refs[n] = s
        machine.write(SPRITE_SEG, start, bytes(blob))
        machine.write_u16(CODE0, CS_SPRITE_SEG, SPRITE_SEG)
        label = Sprite(rnd, columns=rnd.choice([1, 2]))
        machine.write_u16(G3_SEG, 2 * SHAPE_CONTINUE, 0x40)
        machine.write(G3_SEG, 0x40, label.original_bytes())
        machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)

        self.c_refs = {n: s.to_c() for n, s in self.refs.items()}
        self.c_label = label.to_c()

        @LOOKUP
        def sprite_ref(n):
            return ctypes.addressof(self.c_refs[n]) if n in self.c_refs else None

        @LOOKUP
        def shape(i):
            return ctypes.addressof(self.c_label) if i == SHAPE_CONTINUE else None

        self._keep = (sprite_ref, shape)
        self.art = ContinueArt(ctypes.cast(sprite_ref, ctypes.c_void_p),
                               ctypes.cast(shape, ctypes.c_void_p))


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_continue_draw_hook(machine, oracle, c, case):
    rnd = case_rng("continue_draw_hook", case)
    art = Art(machine, rnd)
    img = bytearray(make_case(oracle, machine, rnd).image)
    setup_pages(machine, c, rnd)
    dirty = rnd.random() < 0.5
    setup_machine(machine, img, c, dirty)
    case_ = ThinkCase(oracle.dg.canonical(bytes(img)), 0, (0, 0))

    machine.write_dgroup(0, case_.image)
    machine.call("1000:0894", far=True, max_insns=5_000_000)
    want = machine.read_dgroup(0, 0x10000)

    state = oracle.dg.from_dgroup(case_.image)
    target = c.target(c.draw_page_id())
    c.set_dirty(dirty)
    c.draw_hook(ctypes.addressof(state), ctypes.byref(target), ctypes.byref(art.art))
    c.set_dirty(False)
    compare(oracle, want, oracle.dg.to_dgroup(state, case_.image))
    check_pages(machine, c)
    if dirty:
        restore_frame(machine, c)
