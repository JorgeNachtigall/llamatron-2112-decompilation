"""W6 hi-score table and name entry: game86 hiscore_check_insert 1000:5da2 (name entry and
save stubbed), hiscore_table_show 5c3c..5d16 with text_draw_row_coloured 5d5d and
vram_clear_lower 639f, hiscore_name_entry 59fa..5a85, name_entry_draw_hook 5ac4 and
name_entry_tick_hook 5b06, against src/game/hiscore.c through llamatron_game_testlib.

Fonts, texts, alphabets and sprites are random; the rude word is read from the original's
code bytes at run time (the CMP immediates of 1000:5e52..5e67), never written here.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind
from llamatron_harness.ega import HEIGHT, WIDTH
from test_scroller import Scroller, ScrollerConfig, window
from test_sprites import DrawTarget as _DrawTarget  # noqa: F401  (same layout as test_text's)
from test_sprites import Sprite
from test_text import (CANVAS_PIXELS, CELLS, CODE0, CS_CLEAR, CS_COLOUR, CS_OPAQUE,
                       DRAW_PAGE, PRINTABLE, SCROLL_MODE, TEXT_X, DrawTarget, Font, TextState,
                       first_diff)

TABLE = 0x40A0
SCORES = (0x4441, 0x4449)
MARKS = 0x09DE
RUDE = 0x0874
OPTIONS = 0x2CF6
NAME_BUF = 0x09BE
LENGTH, CURSOR, FIRE_HELD = 0x0822, 0x0947, 0x0949
INPUT_A = 0x07FC
TICKS = 0x39E6
SND_DRIVER = 0x0400
ALPHABET = 0x0994
ROW_COLOURS = 0x09C9
TABLE_HEADING, ENTRY_HEADING, PROMPT = 0x0AAE, 0x094B, 0x09B5
SCROLL_OFF = 0x043D
CS_SAVED_DS = 0x0255
CS_SPRITE_SEG, CS_G3_SEG = 0x025F, 0x0261
CS_DIRTY_ENABLE, CS_DIRTY_TMP = 0x672F, 0x6731
DIRTY_LIST, LIST_END = 0x6737, 0xFE0C
G3_SEG, SPRITE_SEG, STUB_SEG = 0x8000, 0x9000, 0x9800
MARKER_OFFSETS = (0x245A, 0x4268)
RUDE_IMMEDIATES = (0x5E55, 0x5E5E, 0x5E64, 0x5E6A)
NAME_ENTRY, SAVE, TUNE_START, REVEAL = 0x59FA, 0x5E89, 0x083B, 0x61FF
PAGE_IDS = {0x0000: 0, 0x2000: 1, 0x4000: 2}


class HiscoreEntry(ctypes.Structure):
    _fields_ = [("digits", ctypes.c_char * 8), ("name", ctypes.c_char * 8)]


class HiscoreData(ctypes.Structure):
    _fields_ = [("alphabet", ctypes.c_char * 34), ("row_colours", ctypes.c_uint8 * 10),
                ("rude_word", ctypes.c_char * 4)]


class HiscoreArt(ctypes.Structure):
    _fields_ = [("logo_left", ctypes.c_void_p), ("logo_right", ctypes.c_void_p),
                ("cursor", ctypes.c_void_p), ("marker", ctypes.c_void_p * 2),
                ("control_labels", ctypes.c_void_p), ("table_heading", ctypes.c_void_p),
                ("entry_heading", ctypes.c_void_p), ("entry_prompt", ctypes.c_void_p),
                ("data", ctypes.POINTER(HiscoreData))]


class NameEntry(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 8), ("length", ctypes.c_int16),
                ("cursor", ctypes.c_int16), ("fire_held", ctypes.c_bool)]


class NameEntrySound(ctypes.Structure):
    _fields_ = [("sample", ctypes.c_uint8), ("rate_index", ctypes.c_uint8),
                ("priority", ctypes.c_uint8)]


SHAPE_LOOKUP = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint)
ENTRIES = HiscoreEntry * 10
MARKS2 = ctypes.c_int16 * 2
DIGITS8 = ctypes.c_uint8 * 8


class C:
    def __init__(self, lib):
        self.lib = lib
        bind(lib, "gfx_init", None)()
        self.page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.target = bind(lib, "draw_target_page", DrawTarget, ctypes.c_int)
        self.canvas = bind(lib, "scroller_canvas", DrawTarget)
        self.set_dirty = bind(lib, "draw_set_dirty_recording", None, ctypes.c_bool)
        self.set_wrapped = bind(lib, "draw_set_canvas_wrapped", None, ctypes.c_bool)
        self.find_rank = bind(lib, "hiscore_find_rank", ctypes.c_int, ctypes.POINTER(HiscoreEntry),
                              ctypes.POINTER(ctypes.c_uint8))
        self.insert = bind(lib, "hiscore_insert", None, ctypes.POINTER(HiscoreEntry),
                           ctypes.POINTER(ctypes.c_int16), ctypes.c_int, ctypes.c_int,
                           ctypes.POINTER(ctypes.c_uint8))
        self.set_name = bind(lib, "hiscore_set_name", None, ctypes.POINTER(HiscoreEntry),
                             ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_bool))
        self.table_draw = bind(lib, "hiscore_table_draw", None, ctypes.POINTER(Scroller),
                               ctypes.POINTER(TextState), ctypes.POINTER(HiscoreEntry),
                               ctypes.POINTER(ctypes.c_int16), ctypes.POINTER(HiscoreArt))
        self.entry_begin = bind(lib, "name_entry_begin", None, ctypes.POINTER(NameEntry),
                                ctypes.POINTER(TextState), ctypes.c_uint8,
                                ctypes.POINTER(HiscoreArt))
        self.entry_draw = bind(lib, "name_entry_draw", None, ctypes.POINTER(NameEntry),
                               ctypes.POINTER(DrawTarget), ctypes.POINTER(TextState),
                               ctypes.POINTER(HiscoreArt))
        self.entry_tick = bind(lib, "name_entry_tick", NameEntrySound, ctypes.POINTER(NameEntry),
                               ctypes.c_uint8, ctypes.POINTER(ctypes.c_uint16),
                               ctypes.POINTER(HiscoreArt))

    def load(self, pid, pixels):
        ctypes.memmove(self.page(pid), pixels, len(pixels))

    def read(self, pid):
        return ctypes.string_at(self.page(pid), WIDTH * HEIGHT)


@pytest.fixture
def c(cgame):
    return C(cgame)


def rude_word(machine):
    return bytes(machine.read(CODE0, off, 1)[0] for off in RUDE_IMMEDIATES)


def random_table(rnd):
    rows = []
    for _ in range(10):
        lead = rnd.choice([0, 0, 0, rnd.randrange(1, 9)])     # leading zeros (drawn blank)
        digits = (b"0" * lead + bytes(rnd.choice(b"0123456789") for _ in range(8)))[:8]
        rows.append(digits + bytes(rnd.choice(PRINTABLE + [0x20]) for _ in range(8)))
    rows.sort(key=lambda r: r[:8], reverse=rnd.random() < 0.8)
    return rows


def entries_from(rows):
    t = ENTRIES()
    for i, r in enumerate(rows):
        t[i].digits = r[:8]
        t[i].name = r[8:]
    return t


def random_score(rnd, rows):
    if rnd.random() < 0.3:
        return bytes(d - 0x30 for d in rnd.choice(rows)[:8])          # a tie
    lead = rnd.choice([0, 1, 2, 3, 4])
    return bytes([0] * lead + [rnd.randrange(10) for _ in range(8 - lead)])


def random_name(rnd, word):
    name = bytearray(rnd.choice(PRINTABLE + [0x2D, 0x2D]) for _ in range(8))
    if rnd.random() < 0.3:
        at = rnd.randrange(0, 5)
        name[at:at + 4] = word
    return bytes(name)


# ---- check / insert -------------------------------------------------------------------

@pytest.mark.parametrize("case", fuzz_cases(64))
def test_check_insert(machine, c, case):
    rnd = case_rng("hiscore_insert", case)
    word = rude_word(machine)
    rows = random_table(rnd)
    player = rnd.randrange(2)
    scores = [random_score(rnd, rows), random_score(rnd, rows)]
    marks = [rnd.choice([-1, -1, rnd.randrange(10)]) for _ in range(2)]
    if rnd.random() < 0.4:      # the other player's mark at or next to the new row
        table = entries_from(rows)
        rank = c.find_rank(table, DIGITS8(*scores[player]))
        if rank >= 0:
            marks[1 - player] = min(9, max(0, rank + rnd.choice([-1, 0, 0, 1])))
    name = random_name(rnd, word)
    rude_in = rnd.random() < 0.2

    machine.write_dgroup(TABLE, b"".join(rows))
    for p in range(2):
        machine.write_dgroup(SCORES[p], scores[p])
    machine.write_dgroup(MARKS, struct.pack("<2h", *marks))
    machine.write_dgroup(OPTIONS, bytes([0x10 if player else 0]))
    machine.write_dgroup(NAME_BUF, name)
    machine.write_dgroup(RUDE, struct.pack("<H", rude_in))
    machine.write(CODE0, NAME_ENTRY, b"\xc3")
    machine.write(CODE0, SAVE, b"\xc3")
    machine.call("1000:5da2", max_insns=1_000_000)

    table = entries_from(rows)
    cmarks = MARKS2(*marks)
    score = DIGITS8(*scores[player])
    rank = c.find_rank(table, score)
    rude = ctypes.c_bool(rude_in)
    if rank >= 0:
        c.insert(table, cmarks, player, rank, score)
        c.set_name(ctypes.byref(table[rank]), name, word, ctypes.byref(rude))

    want_table = machine.read_dgroup(TABLE, 160)
    got_table = b"".join(ctypes.string_at(ctypes.addressof(e), 16) for e in table)
    assert got_table == want_table, f"rank {rank}"
    assert tuple(cmarks) == struct.unpack("<2h", machine.read_dgroup(MARKS, 4))
    assert rude.value == bool(struct.unpack("<H", machine.read_dgroup(RUDE, 2))[0])


# ---- art ------------------------------------------------------------------------------

class Art:
    """Random sprites in the original's segments plus the C HiscoreArt."""

    def __init__(self, machine, rnd, shapes=(0, 7, 8, 9, 10, 11, 14, 15)):
        self.g3 = {}
        blob, base = bytearray(), 0x40
        for index in shapes:
            # Turn labels (10, 11) are centred at y = 15, unclipped: keep them on the page.
            s = Sprite(rnd, rows=rnd.randrange(1, 25) if index in (10, 11) else None)
            machine.write_u16(G3_SEG, 2 * index, base + len(blob))
            blob += s.original_bytes()
            self.g3[index] = s
        machine.write(G3_SEG, base, bytes(blob))
        machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)
        self.markers = []
        for off in MARKER_OFFSETS:
            s = Sprite(rnd)
            machine.write(SPRITE_SEG, off, s.original_bytes())
            self.markers.append(s)
        machine.write_u16(CODE0, CS_SPRITE_SEG, SPRITE_SEG)

        self.c_g3 = {i: s.to_c() for i, s in self.g3.items()}
        self.c_markers = [s.to_c() for s in self.markers]
        self.data = HiscoreData()
        self.texts = {}

        @SHAPE_LOOKUP
        def lookup(index):
            return ctypes.addressof(self.c_g3[index]) if index in self.c_g3 else None

        self._lookup = lookup
        self.art = HiscoreArt()
        for field, index in (("logo_left", 15), ("logo_right", 14), ("cursor", 0)):
            if index in self.c_g3:
                setattr(self.art, field, ctypes.addressof(self.c_g3[index]))
        self.art.marker[0] = ctypes.addressof(self.c_markers[0])
        self.art.marker[1] = ctypes.addressof(self.c_markers[1])
        self.art.control_labels = ctypes.cast(lookup, ctypes.c_void_p)
        self.art.data = ctypes.pointer(self.data)

    def text(self, field, raw):
        buf = ctypes.create_string_buffer(raw, len(raw) + 1)
        self.texts[field] = buf
        setattr(self.art, field, ctypes.addressof(buf))


def short_text(rnd, words, breaks=2):
    out = bytearray()
    for _ in range(words):
        k = rnd.random()
        if k < 0.75:
            out += bytes(rnd.choice(PRINTABLE) for _ in range(rnd.randrange(1, 7))) + b" "
        elif k < 0.85:
            out += b"\\c" + bytes([rnd.randrange(256)])
        elif k < 0.92 and breaks:
            breaks -= 1
            out += b"\\r"
        else:
            out += b"\\q" + bytes(rnd.choice(PRINTABLE) for _ in range(rnd.randrange(0, 10))) + b"\0"
    return bytes(out) + b"\\e"


def random_alphabet(rnd):
    letters = [rnd.choice(PRINTABLE) for _ in range(33)]
    for symbol in (b"$"[0], b"*"[0]):
        letters[rnd.randrange(33)] = symbol
    return bytes(letters)


def set_colour(machine, rnd):
    colour, clear = rnd.randrange(16), rnd.choice([0, rnd.randrange(16)])
    machine.write(CODE0, CS_COLOUR, bytes([colour]))
    machine.write(CODE0, CS_CLEAR, bytes([clear]))
    machine.write(CODE0, CS_OPAQUE, b"\0")
    return colour, clear


def check_text_state(machine, state):
    assert state.color == machine.read(CODE0, CS_COLOUR, 1)[0]
    assert state.clear_planes == machine.read(CODE0, CS_CLEAR, 1)[0]
    assert (state.x, state.y, state.margin) == struct.unpack("<3h", machine.read_dgroup(TEXT_X, 6))


# ---- the table ------------------------------------------------------------------------

@pytest.mark.parametrize("case", fuzz_cases(24))
def test_table_draw(machine, c, case):
    rnd = case_rng("hiscore_table", case)
    font = Font(rnd)
    font.load_original(machine)
    font.to_c(c.lib)
    art = Art(machine, rnd)
    # Like the real heading, start with \g so the margin is on the screen (a margin past
    # x 304 wraps after every glyph and runs off the original's row table).
    heading = (b"\\g" + bytes([rnd.randrange(0, 160), rnd.randrange(0, 40)])
               + short_text(rnd, rnd.randrange(1, 12)))
    art.text("table_heading", heading)
    machine.write_dgroup(TABLE_HEADING, heading)
    colours = bytes(rnd.randrange(256) for _ in range(10))
    art.data.row_colours[:] = list(colours)
    machine.write_dgroup(ROW_COLOURS, colours)
    rows = random_table(rnd)
    machine.write_dgroup(TABLE, b"".join(rows))
    marks = [rnd.choice([-1, rnd.randrange(10)]) for _ in range(2)]
    machine.write_dgroup(MARKS, struct.pack("<2h", *marks))

    for p in machine.ega.planes:
        p[:] = bytes(rnd.randrange(256) for _ in range(CELLS))
    canvas = c.canvas()
    ctypes.memmove(canvas.pixels, window(machine.ega.planes, 0, CELLS), CANVAS_PIXELS)
    offset = rnd.randrange(0, 1639) * 40 & 0xFFFF
    wrapped = rnd.random() < 0.4
    x, y = rnd.randrange(0, 320), rnd.randrange(0, 1638 - 16 - 400)
    colour, clear = set_colour(machine, rnd)
    machine.write_dgroup(SCROLL_OFF, struct.pack("<H", offset))
    machine.write_dgroup(SCROLL_MODE, bytes([2 if wrapped else 0]))
    machine.write_dgroup(TEXT_X, struct.pack("<3h", x, y, 0))
    machine.write_dgroup(DRAW_PAGE, b"\x11\x11")
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write(CODE0, TUNE_START, b"\xc3")
    machine.run_until("1000:5c3c", "1000:5d16", max_insns=20_000_000)

    sc = Scroller()
    sc.offset = offset
    sc.mode = 2 if wrapped else 0
    state = TextState(x, y, 0, colour, clear, False)
    c.set_wrapped(wrapped)
    c.table_draw(ctypes.byref(sc), ctypes.byref(state), entries_from(rows), MARKS2(*marks),
                 ctypes.byref(art.art))
    c.set_wrapped(False)

    got = ctypes.string_at(canvas.pixels, CANVAS_PIXELS)
    want = window(machine.ega.planes, 0, CELLS)
    assert got == want, (f"offset {offset:#x} wrapped {wrapped} pen {x},{y} -> {state.y} "
                         f"marks {marks}: canvas: " + first_diff(got, want))
    check_text_state(machine, state)


# ---- name entry -----------------------------------------------------------------------

@pytest.mark.parametrize("case", fuzz_cases(16))
def test_name_entry_begin(machine, c, case):
    rnd = case_rng("name_entry_begin", case)
    font = Font(rnd)
    font.load_original(machine)
    font.to_c(c.lib)
    art = Art(machine, rnd)
    heading = short_text(rnd, rnd.randrange(1, 6), breaks=1)
    art.text("entry_heading", heading)
    machine.write_dgroup(ENTRY_HEADING, heading)
    alphabet = random_alphabet(rnd)
    art.data.alphabet = alphabet
    machine.write_dgroup(ALPHABET, alphabet)
    options = rnd.randrange(256)
    machine.write_dgroup(OPTIONS, bytes([options]))
    for off, pid in PAGE_IDS.items():
        px = indexed_page(rnd, rnd.choice(["noise", "blocks"]))
        machine.ega.load_indexed(px, page=off)
        c.load(pid, px)
    colour, clear = set_colour(machine, rnd)
    machine.write_dgroup(TEXT_X, struct.pack("<3h", 5, 5, 0))
    machine.write_dgroup(NAME_BUF, bytes(rnd.randrange(256) for _ in range(8)))
    machine.write_dgroup(SCROLL_MODE, b"\0")
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.run_until("1000:59fa", "1000:5a85", max_insns=20_000_000)

    entry = NameEntry()
    state = TextState(5, 5, 0, colour, clear, False)
    c.entry_begin(ctypes.byref(entry), ctypes.byref(state), options, ctypes.byref(art.art))

    for off, pid in PAGE_IDS.items():
        got, want = c.read(pid), machine.ega.rasterise(off)
        assert got == want, f"page {pid}: " + first_diff(got, want)
    assert bytes(entry.name) == machine.read_dgroup(NAME_BUF, 8)
    check_text_state(machine, state)


def prompt_template(rnd):
    """The original's layout: \\c<colour>\\g<x><dy>\\q, the 8-byte name buffer, NUL, \\e."""
    return (b"\\c" + bytes([rnd.randrange(256)]) + b"\\g" + bytes([rnd.randrange(256), rnd.randrange(0, 120)])
            + b"\\q")


@pytest.mark.parametrize("case", fuzz_cases(32))
def test_name_entry_draw(machine, c, case):
    rnd = case_rng("name_entry_draw", case)
    font = Font(rnd)
    font.load_original(machine)
    font.to_c(c.lib)
    art = Art(machine, rnd, shapes=(0,))
    head = prompt_template(rnd)
    name = bytes(rnd.choice(PRINTABLE + [0x2D] * 8) for _ in range(8))
    machine.write_dgroup(PROMPT, head + name + b"\0\\e")
    art.text("entry_prompt", head + b"--------" + b"\0\\e")
    cursor = rnd.randrange(33)
    machine.write_dgroup(CURSOR, struct.pack("<H", cursor))
    page = rnd.choice([0x0000, 0x2000])
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", page))
    machine.write_dgroup(SCROLL_MODE, b"\0")
    for off, pid in PAGE_IDS.items():
        px = indexed_page(rnd, rnd.choice(["noise", "blocks"]))
        machine.ega.load_indexed(px, page=off)
        c.load(pid, px)
    dirty = rnd.random() < 0.5
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1 if dirty else 0)
    machine.write_u16(CODE0, CS_DIRTY_TMP, DIRTY_LIST)
    machine.write_u16(CODE0, DIRTY_LIST, LIST_END)
    colour, clear = set_colour(machine, rnd)
    machine.write_dgroup(TEXT_X, struct.pack("<3h", 9, 9, 0))
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.call("1000:5ac4", far=True, max_insns=20_000_000)

    entry = NameEntry()
    entry.name = name
    entry.cursor = cursor
    state = TextState(9, 9, 0, colour, clear, False)
    target = c.target(PAGE_IDS[page])
    c.set_dirty(dirty)
    c.entry_draw(ctypes.byref(entry), ctypes.byref(target), ctypes.byref(state), ctypes.byref(art.art))
    c.set_dirty(False)

    got, want = c.read(PAGE_IDS[page]), machine.ega.rasterise(page)
    assert got == want, first_diff(got, want)
    check_text_state(machine, state)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_name_entry_tick(machine, c, case):
    rnd = case_rng("name_entry_tick", case)
    alphabet = random_alphabet(rnd)
    machine.write_dgroup(ALPHABET, alphabet)
    art = HiscoreArt()
    data = HiscoreData()
    data.alphabet = alphabet
    art.data = ctypes.pointer(data)

    entry = NameEntry()
    entry.name = bytes(rnd.choice(PRINTABLE + [0x2D] * 8) for _ in range(8))
    entry.length = rnd.randrange(8)
    entry.cursor = rnd.randrange(33)
    entry.fire_held = rnd.random() < 0.3
    tick = rnd.randrange(0, 0x10000)
    machine.write_dgroup(NAME_BUF, bytes(entry.name))
    machine.write_dgroup(LENGTH, struct.pack("<H", entry.length))
    machine.write_dgroup(CURSOR, struct.pack("<H", entry.cursor))
    machine.write_dgroup(FIRE_HELD, struct.pack("<H", entry.fire_held))

    machine.write(STUB_SEG, 0, b"\xcb")
    machine.write_dgroup(SND_DRIVER, struct.pack("<2H", 0, STUB_SEG))
    heard = []
    machine.add_breakpoint((STUB_SEG, 0), lambda m: heard.append(m.get_regs()))

    held = 0
    for step in range(300):
        if rnd.random() < 0.3:
            held = rnd.choice([0, 0x80, 0x01, 0x02, 0x04, 0x08, 0x81, 0x88, 0x05, 0x0A,
                               rnd.randrange(256)])
        tick = (tick + 1) & 0xFFFF
        if rnd.random() < 0.05:
            tick = rnd.choice([0x1767, 0x1770, 0x7FFF, 0x8000]) + rnd.randrange(0, 3) & 0xFFFF
        machine.write_dgroup(INPUT_A, bytes([held]))
        machine.write_dgroup(TICKS, struct.pack("<H", tick))
        heard.clear()
        machine.call("1000:5b06", max_insns=100_000)

        ctick = ctypes.c_uint16(tick)
        sound = c.entry_tick(ctypes.byref(entry), held, ctypes.byref(ctick), ctypes.byref(art))
        tick = struct.unpack("<H", machine.read_dgroup(TICKS, 2))[0]
        where = f"step {step} input {held:#x}"
        assert ctick.value == tick, where
        assert bytes(entry.name) == machine.read_dgroup(NAME_BUF, 8), where
        assert (entry.length, entry.cursor) == struct.unpack("<2h", machine.read_dgroup(LENGTH, 2)
                                                             + machine.read_dgroup(CURSOR, 2)), where
        assert entry.fire_held == bool(struct.unpack("<H", machine.read_dgroup(FIRE_HELD, 2))[0]), where
        if heard:
            r = heard[0]
            assert (r["ax"] >> 8, sound.sample, sound.rate_index, sound.priority) == \
                (3, r["ax"] & 0xFF, r["cx"] & 0xFF, r["cx"] >> 8), where
        else:
            assert sound.sample == 0, where
