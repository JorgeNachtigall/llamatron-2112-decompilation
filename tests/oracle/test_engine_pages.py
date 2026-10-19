"""W2 page flipping: frame_run's CRTC start write (1e7b:0185..0190) followed by page_swap
1e7b:029a, against gfx_flip (src/engine/gfx.c) through llamatron_game_testlib.

Between frames [39d3] is the page drawn next and [39d5] the page on display; frame_run shows
[39d3] (CRTC start) and page_swap exchanges the two words and the two dirty-list pointers
CS:6733/6735 (the port keeps one list per page, so that half needs no counterpart).
"""

import ctypes
import struct

from generators import case_rng, fuzz_cases

from adapters.cport import bind

CODE0, CODE1 = 0x1000, 0x1e7b
CODE1_FAR_SEG = 0x013D                   # 1e7b:013d: segment of code 1000, for ES
PAGES = 0x39D3                           # [39d3] drawn next, [39d5] on display
DIRTY_LISTS = 0x6733                     # CS:6733 / CS:6735
PAGE_OFFSETS = {0x0000: 0, 0x2000: 1}


def crtc_start(res):
    """The display start the frame wrote to CRTC registers 0Ch (high byte)."""
    outs = [p.value for p in res.ports if p.direction == "out" and p.port == 0x3D4]
    assert len(outs) == 1 and outs[0] & 0xFF == 0x0C
    return outs[0] & 0xFF00


def test_flip_sequence(machine, cgame):
    flip = bind(cgame, "gfx_flip", None)
    draw_id = bind(cgame, "gfx_draw_page_id", ctypes.c_int)
    shown_id = bind(cgame, "gfx_display_page_id", ctypes.c_int)
    for case in fuzz_cases(10):
        rnd = case_rng("engine_pages", case)
        bind(cgame, "gfx_init", None)()
        machine.reset()
        machine.write_u16(CODE1, CODE1_FAR_SEG, CODE0)
        # gfx_init: page 1 drawn, page 0 shown
        machine.write_dgroup(PAGES, struct.pack("<HH", 0x2000, 0x0000))
        lists = (rnd.randrange(0x10000), rnd.randrange(0x10000))
        machine.write(CODE0, DIRTY_LISTS, struct.pack("<HH", *lists))
        for step in range(rnd.randrange(1, 12)):
            res = machine.run_until("1e7b:0185", "1e7b:0191")
            shown = crtc_start(res)
            machine.call("1e7b:029a", far=True)
            flip()
            front, back = struct.unpack("<HH", machine.read_dgroup(PAGES, 4))
            where = f"case {case} flip {step}"
            assert PAGE_OFFSETS[shown] == shown_id(), where
            assert PAGE_OFFSETS[back] == shown_id(), where
            assert PAGE_OFFSETS[front] == draw_id(), where
            lists = lists[::-1]
            assert struct.unpack("<HH", machine.read(CODE0, DIRTY_LISTS, 4)) == lists, where
