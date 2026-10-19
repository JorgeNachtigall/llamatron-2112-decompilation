"""W6 text scroller: game86 text_scroller 1000:5844 (with display_page_reset 582f,
vram_clear_from_1f40 6383, vram_clear_line 638f and the end 594a) against
src/game/draw_scroller.c through llamatron_game_testlib.

The original's loop runs in lockstep with the C state machine: wait_vsync and kbd_flush are
stubbed (RETF), hiscore_table_show (5c3c) with RET, and at each input check (1000:5883) the
test sets g_scroll_abort / g_joy_buttons from a random script, compares the scroll position
and (now and then) the visible window with page 0 of the port, then runs one C frame with the
same input. At the end all three pages, the whole canvas and the page roles are compared.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind
from test_text import (CANVAS_PIXELS, CELLS, CODE0, CS_CLEAR, CS_COLOUR, CS_OPAQUE,
                       DRAW_PAGE, PRINTABLE, SCROLL_MODE, STRING_AREA, TEXT_X, DrawTarget, Font,
                       TextState, first_diff)

from llamatron_harness.ega import HEIGHT, WIDTH

LATE_TEXT = 0x1BC8                 # DS offset the original prints the late text from
CS_DIRTY_PTRS = (0x6733, 0x6735)
DIRTY_LIST = 0x6737
CS_CODE0_SEG = (0x1E7B, 0x013D)
CS_END_OFFSET = 0x582D             # smc_scroller_op
SCROLL_OFF, SCROLL_STATE, SCROLL_COUNTER, SCROLL_FLAG = 0x043D, 0x043F, 0x0440, 0x0443
SCROLL_ABORT, JOY_BUTTONS = 0x0587, 0x07FD
PAGES = 0x39D3                     # [39d3] page drawn next, [39d5] page on display
WAIT_VSYNC, KBD_FLUSH, HISCORE_SHOW = 0xE5F3, 0xE605, 0x5C3C
PAGE_CELLS = 8000
PAGE_OFFSETS = (0x0000, 0x2000, 0x4000)

RUNNING, SHOW_HISCORE, DONE = 0, 1, 2
SKIP_KEY, PAUSE_KEY = 0x39, 0x19


class ScrollerConfig(ctypes.Structure):
    _fields_ = [("text", ctypes.c_char_p), ("end_offset", ctypes.c_uint16),
                ("late_text", ctypes.c_char_p)]


class Scroller(ctypes.Structure):
    _fields_ = [("config", ScrollerConfig), ("text", ctypes.POINTER(TextState)),
                ("offset", ctypes.c_uint16), ("counter", ctypes.c_int16),
                ("mode", ctypes.c_uint8), ("skip_locked", ctypes.c_bool),
                ("stepped", ctypes.c_bool)]


class ScrollerInput(ctypes.Structure):
    _fields_ = [("skip", ctypes.c_bool), ("pause", ctypes.c_bool)]


class C:
    def __init__(self, lib):
        bind(lib, "gfx_init", None)()
        self.lib = lib
        self.page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.flip = bind(lib, "gfx_flip", None)
        self.display = bind(lib, "gfx_display_page_id", ctypes.c_int)
        self.draw_page = bind(lib, "gfx_draw_page_id", ctypes.c_int)
        self.canvas = bind(lib, "scroller_canvas", DrawTarget)
        self.start = bind(lib, "scroller_start", None, ctypes.POINTER(Scroller),
                          ctypes.POINTER(ScrollerConfig), ctypes.POINTER(TextState))
        self.frame = bind(lib, "scroller_frame", ctypes.c_int, ctypes.POINTER(Scroller),
                          ScrollerInput)
        self.finish = bind(lib, "scroller_finish", None, ctypes.POINTER(Scroller))
        self.text_print = bind(lib, "text_print", ctypes.c_void_p, ctypes.POINTER(DrawTarget),
                               ctypes.POINTER(TextState), ctypes.c_char_p)
        self.set_wrapped = bind(lib, "draw_set_canvas_wrapped", None, ctypes.c_bool)

    def load(self, pid, pixels):
        ctypes.memmove(self.page(pid), pixels, len(pixels))

    def read(self, pid):
        return ctypes.string_at(self.page(pid), WIDTH * HEIGHT)


@pytest.fixture
def c(cgame):
    return C(cgame)


_SPREAD = [sum(((b >> (7 - i)) & 1) << (8 * i) for i in range(8)) for b in range(256)]


def window(planes, start, cells=PAGE_CELLS):
    """Indexed pixels of `cells` VRAM cells from `start`, wrapping at 64 KB (the CRTC view)."""
    out = bytearray(cells * 8)
    p0, p1, p2, p3 = planes
    sp = _SPREAD
    for i in range(cells):
        a = (start + i) & 0xFFFF
        v = sp[p0[a]] | sp[p1[a]] << 1 | sp[p2[a]] << 2 | sp[p3[a]] << 3
        out[i * 8:i * 8 + 8] = v.to_bytes(8, "little")
    return bytes(out)


def scroll_text(rnd, words):
    out = bytearray()
    for _ in range(words):
        kind = rnd.random()
        if kind < 0.70:
            out += bytes(rnd.choice(PRINTABLE) for _ in range(rnd.randrange(1, 9))) + b" "
        elif kind < 0.78:
            out += b"\\c" + bytes([rnd.randrange(256)])
        elif kind < 0.88:
            out += b"\\r"
        elif kind < 0.94:
            out += b"\\q" + bytes(rnd.choice(PRINTABLE + [0x20]) for _ in range(rnd.randrange(0, 12))) + b"\0"
        else:
            out += b"\0"
    return bytes(out) + b"\\e"


def fitting_text(rnd, c, y, wrapped, words):
    """A text whose lines stay inside the original's 1638-row table (the pen only moves
    down here, so its end row bounds every line)."""
    scratch = ctypes.create_string_buffer(CANVAS_PIXELS)
    target = DrawTarget(ctypes.cast(scratch, ctypes.c_void_p), CELLS, True, -1)
    while True:
        text = scroll_text(rnd, words)
        state = TextState(0x10, y, 0, 1, 0, False)
        c.set_wrapped(wrapped)
        c.text_print(ctypes.byref(target), ctypes.byref(state), text)
        c.set_wrapped(False)
        if state.y + 16 < 1638:
            return text
        words = max(1, words * 2 // 3)


def input_script(rnd):
    """Inputs for the successive checks: (skip, pause) pairs, repeated last entry."""
    style = rnd.choice(["none", "none", "skip", "pause", "mixed"])
    script = []
    if style == "none":
        return script
    for _ in range(rnd.randrange(1, 40)):
        gap = rnd.choice([rnd.randrange(1, 30), rnd.randrange(30, 800)])
        script += [(False, False)] * gap
        if style in ("pause", "mixed") and rnd.random() < 0.7:
            script += [(False, True)] * rnd.randrange(1, 6)
        if style in ("skip", "mixed") and rnd.random() < 0.3:
            script.append((True, rnd.random() < 0.2))
    return script


def run_scroller(machine, c, rnd, story, script=None, end_offset=None):
    font = Font(rnd)
    colour = rnd.randrange(16)
    font.to_c(c.lib)                               # also used to measure the texts
    text = fitting_text(rnd, c, 0xCE, False, rnd.randrange(20, 500))
    late = fitting_text(rnd, c, 0x02, True, rnd.randrange(5, 200)) if story else None
    if end_offset is None:
        end_offset = rnd.choice([0xE800, 0x8800, rnd.randrange(0, 0x10000)])

    # Pages, display role and the original's VRAM.
    pages = [indexed_page(rnd, rnd.choice(["noise", "blocks"])) for _ in range(3)]
    for p in machine.ega.planes:
        p[:] = bytes(rnd.randrange(256) for _ in range(CELLS))
    for off, px in zip(PAGE_OFFSETS, pages):
        machine.ega.load_indexed(px, page=off)
    for pid, px in enumerate(pages):
        c.load(pid, px)
    shown = rnd.randrange(2)
    if c.display() != shown:
        c.flip()
    machine.write_dgroup(PAGES, struct.pack("<2H", 0x2000 * (1 - shown), 0x2000 * shown))

    # The original's state.
    font.load_original(machine)
    machine.write_dgroup(STRING_AREA, text)
    if story:
        machine.write_dgroup(LATE_TEXT, late)
    machine.write_dgroup(SCROLL_MODE, b"\0")
    machine.write_dgroup(SCROLL_FLAG, b"\1" if story else b"\0")
    machine.write_dgroup(SCROLL_STATE, b"\0")
    machine.write_dgroup(SCROLL_ABORT, b"\0")
    machine.write_dgroup(JOY_BUTTONS, b"\0")
    machine.write_u16(CODE0, CS_END_OFFSET, end_offset)
    for ptr in CS_DIRTY_PTRS:
        machine.write_u16(CODE0, ptr, DIRTY_LIST)
    machine.write_u16(*CS_CODE0_SEG, CODE0)
    machine.write(CODE0, CS_COLOUR, bytes([colour]))
    machine.write(CODE0, CS_CLEAR, b"\0")
    machine.write(CODE0, CS_OPAQUE, b"\0")
    machine.write(CODE0, WAIT_VSYNC, b"\xcb")      # RETF
    machine.write(CODE0, KBD_FLUSH, b"\xcb")
    machine.write(CODE0, HISCORE_SHOW, b"\xc3")    # RET

    # The port.
    state = TextState(0, 0, 0, colour, 0, False)
    text_buf = ctypes.create_string_buffer(text, len(text))
    late_buf = ctypes.create_string_buffer(late, len(late)) if story else None
    config = ScrollerConfig(ctypes.cast(text_buf, ctypes.c_char_p), end_offset,
                            ctypes.cast(late_buf, ctypes.c_char_p) if story else None)
    sc = Scroller()
    c.start(ctypes.byref(sc), ctypes.byref(config), ctypes.byref(state))
    status = c.frame(ctypes.byref(sc), ScrollerInput(False, False))

    if script is None:
        script = input_script(rnd)
    log = {"checks": 0, "windows": 0, "hiscore_counter": None}

    def compare(where):
        off = struct.unpack("<H", machine.read_dgroup(SCROLL_OFF, 2))[0]
        counter = struct.unpack("<h", machine.read_dgroup(SCROLL_COUNTER, 2))[0]
        assert (sc.offset, sc.counter) == (off, counter), where
        assert sc.mode == machine.read_dgroup(SCROLL_MODE, 1)[0], where
        assert sc.skip_locked == bool(machine.read_dgroup(SCROLL_STATE, 1)[0]), where
        if log["checks"] % 97 == 1:
            want = window(machine.ega.planes, off)
            got = c.read(0)
            assert got == want, f"{where} window at {off:#x}: " + first_diff(got, want)
            log["windows"] += 1

    def on_check(m):
        nonlocal status
        k = log["checks"]
        log["checks"] += 1
        assert status == RUNNING, f"check {k}: C already ended with {status}"
        compare(f"check {k}")
        skip, pause = script[k] if k < len(script) else (False, False)
        key = joy = 0
        if skip:
            if rnd.random() < 0.5:
                key = SKIP_KEY
            else:
                joy |= 0x10
        if pause:
            if key == 0 and rnd.random() < 0.5:
                key = PAUSE_KEY
            else:
                joy |= 0x20
        m.write_dgroup(SCROLL_ABORT, bytes([key]))
        m.write_dgroup(JOY_BUTTONS, bytes([joy]))
        status = c.frame(ctypes.byref(sc), ScrollerInput(skip, pause))

    def on_hiscore(m):
        log["hiscore_counter"] = struct.unpack("<h", m.read_dgroup(SCROLL_COUNTER, 2))[0]

    machine.add_breakpoint("1000:5883", on_check)
    machine.add_breakpoint("1000:5941", on_hiscore)
    machine.call("1000:5844", regs={"si": STRING_AREA}, max_insns=400_000_000)

    if log["hiscore_counter"] is not None:
        assert status == SHOW_HISCORE
        assert sc.counter == log["hiscore_counter"]
    else:
        assert status == DONE
    c.finish(ctypes.byref(sc))

    canvas = c.canvas()
    got = ctypes.string_at(canvas.pixels, CANVAS_PIXELS)
    want = window(machine.ega.planes, 0, CELLS)
    assert got == want, "canvas: " + first_diff(got, want)
    for pid, off in enumerate(PAGE_OFFSETS):
        got_page = c.read(pid)
        want_page = machine.ega.rasterise(off)
        assert got_page == want_page, f"page {pid}: " + first_diff(got_page, want_page)
    assert machine.read_dgroup(PAGES, 4) == struct.pack("<2H", 0x2000, 0)
    assert (c.display(), c.draw_page()) == (0, 1)
    assert (state.x, state.y) == struct.unpack("<2h", machine.read_dgroup(TEXT_X, 4))
    return log


@pytest.mark.parametrize("case", fuzz_cases(6))
def test_scroller_plain(machine, c, case):
    run_scroller(machine, c, case_rng("scroller_plain", case), story=False)


@pytest.mark.parametrize("case", fuzz_cases(6))
def test_scroller_story(machine, c, case):
    run_scroller(machine, c, case_rng("scroller_story", case), story=True)


def test_scroller_story_without_input(machine, c):
    """A whole story run: wraps, late text, mode 3, then the hand-over to the table."""
    log = run_scroller(machine, c, case_rng("scroller_full", 0), story=True, script=[])
    assert log["checks"] > 0x10000 // 40 and log["hiscore_counter"] is not None
    assert log["windows"] > 15


def test_scroller_plain_to_end(machine, c):
    log = run_scroller(machine, c, case_rng("scroller_end", 0), story=False, script=[],
                       end_offset=0xE800)
    assert log["checks"] == 0xE800 // 40
