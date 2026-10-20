"""W5b title screen (src/game/attract.c) against game86: title_tune_start (083b) and the tune
set-up 5c3f..5c54, title_tune_tick (084e), title_draw (3ee0..3f6b), attract_llamas_init
(44d7), title_tick_hook (3f6f with attract_llamas_tick 4571; with the sound driver and with
the sound-off stub e785) and title_draw_hook (402c with
title_draw_logo_row 40fc and attract_llamas_draw 4603).

The title llamas live in the entity table's first 576 bytes in the original and in attract.c's
own array in the port: they are compared record by record, and those DGROUP bytes are left
out of the image compare. Sprites are random (as in test_hiscore), placed in the original's
sprite-bank and g3 segments and handed to the port through AttractArt lookups.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page
from think_oracle import ThinkCase, ThinkOracle

from adapters.cport import bind
from adapters.gamestate import ATTRACT_LLAMAS, SCRATCH
from llamatron_harness.ega import HEIGHT, WIDTH
from test_sprites import Sprite

CODE0 = 0x1000
CS_SAVED_DS, CS_SPRITE_SEG, CS_G3_SEG = 0x0255, 0x025F, 0x0261
CS_DIRTY_ENABLE, CS_DIRTY_TMP = 0x672F, 0x6731
DIRTY_LIST, LIST_END = 0x6737, 0xFE0C
CS_DIRTY_PTR_B = 0x6735             # list restored by the frame loop
CS_CODE0_SEG = (0x1E7B, 0x013D)
G3_SEG, SPRITE_SEG = 0x8000, 0x9000
SPRITE_TABLE = 0x1C                 # sprite n at SPRITE_SEG:[0x1c + 2n]
PAGE_IDS = {0x0000: 0, 0x2000: 1, 0x4000: 2}
DRAW_PAGE, FRONT_PAGE, BACK_PAGE = 0x39D7, 0x39D3, 0x39D5
DRAW_FLAGS, DRAW_SCALE, DRAW_MIRROR = 0x39D9, 0x39DA, 0x39DC
SCROLL_MODE = 0x0442

CONTROL_A = 0x07FC
TUNE_FIRE, TUNE_PERIOD, TUNE_COUNTDOWN, TUNE_POS, TUNE_RESTART = 0x0625, 0x0627, 0x0629, 0x062B, 0x062D
TUNE_LIST = 0x09D5
OPTIONS, OPTIONS_DEFAULT = 0x2CF6, 0x059E
TITLE_OBJECTS, TITLE_TIMER, KEY_LATCH = 0x2104, 0x0904, 0x0906
TICKS = 0x39E6
SOUND_ON = 0x0688
SOUND_OFF_STUB = (0x1000, 0xE785)     # XOR AX,AX; RETF
LLAMAS, LLAMA_COUNT, LLAMA_SIZE = 0x48C1, 32, 18
assert set(ATTRACT_LLAMAS) == set(range(LLAMAS, LLAMAS + LLAMA_COUNT * LLAMA_SIZE))
DRAW_STATE = {DRAW_PAGE, DRAW_PAGE + 1, DRAW_FLAGS, DRAW_SCALE, DRAW_SCALE + 1, DRAW_MIRROR,
              DRAW_MIRROR + 1}
IGNORE = set(SCRATCH) | set(ATTRACT_LLAMAS) | DRAW_STATE
G3_SHAPES = (4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)

LOOKUP = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint)


class AttractArt(ctypes.Structure):
    _fields_ = [("sprite", ctypes.c_void_p), ("shape", ctypes.c_void_p)]


class AttractLlama(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int32), ("height", ctypes.c_int32), ("vx", ctypes.c_int32),
                ("vy", ctypes.c_int32), ("sprite_base", ctypes.c_int16)]


class TitleTables(ctypes.Structure):
    _fields_ = [("letter_sprites", ctypes.c_uint8 * 10), ("llama_sprites", ctypes.c_uint16 * 4),
                ("tune_length", ctypes.c_uint16), ("tune", ctypes.c_uint8 * 256)]


class InputPrefix(ctypes.Structure):
    _fields_ = [("device", ctypes.c_int), ("single_stick", ctypes.c_bool),
                ("keys", ctypes.c_uint8 * 6), ("controls", ctypes.c_uint8 * 2)]


class C:
    def __init__(self, lib):
        self.lib = lib
        bind(lib, "gfx_init", None)()
        self.page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.draw_page_id = bind(lib, "gfx_draw_page_id", ctypes.c_int)
        self.display_page_id = bind(lib, "gfx_display_page_id", ctypes.c_int)
        from test_text import DrawTarget
        self.DrawTarget = DrawTarget
        self.target = bind(lib, "draw_target_page", DrawTarget, ctypes.c_int)
        self.set_dirty = bind(lib, "draw_set_dirty_recording", None, ctypes.c_bool)
        self.restore_dirty = bind(lib, "gfx_restore_dirty", None, ctypes.c_int)
        self.fn = {name: bind(lib, name, None, ctypes.c_void_p)
                   for name in ("title_tune_start", "title_tune_begin", "title_tune_tick",
                                "title_tick_hook")}
        self.llamas_init = bind(lib, "attract_llamas_init", None)
        self.attract_end = bind(lib, "attract_end", None, ctypes.c_void_p)
        self.title_draw = bind(lib, "title_draw", None, ctypes.c_void_p, ctypes.POINTER(AttractArt))
        self.draw_hook = bind(lib, "title_draw_hook", None, ctypes.c_void_p,
                              ctypes.POINTER(DrawTarget), ctypes.POINTER(AttractArt))
        self.llamas = ctypes.cast(bind(lib, "attract_llamas", ctypes.c_void_p)(),
                                  ctypes.POINTER(AttractLlama * LLAMA_COUNT)).contents
        self.tables = ctypes.cast(bind(lib, "asset_title_tables", ctypes.c_void_p)(),
                                  ctypes.POINTER(TitleTables)).contents
        self.controls = ctypes.cast(bind(lib, "input_state", ctypes.c_void_p)(),
                                    ctypes.POINTER(InputPrefix)).contents.controls

    def load(self, pid, pixels):
        ctypes.memmove(self.page(pid), pixels, len(pixels))

    def read(self, pid):
        return ctypes.string_at(self.page(pid), WIDTH * HEIGHT)


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


@pytest.fixture(scope="module")
def c(cgame):
    return C(cgame)


# ---- state ---------------------------------------------------------------------------------

def llama_records(rnd, c):
    bases = list(c.tables.llama_sprites)
    out = []
    for _ in range(LLAMA_COUNT):
        x = rnd.choice([rnd.randrange(0x10, 0x131), 0x10, 0x130, 0x12f, 0x11]) << 16 | rnd.randrange(0x10000)
        if rnd.random() < 0.2:
            x = rnd.choice([0x130 << 16, 0x10 << 16])
        height = rnd.choice([0, rnd.randrange(0x40)]) << 16 | rnd.randrange(0x10000)
        vx = rnd.choice([-1, 1]) * rnd.randrange(0x1000, 0x21000)
        vy = rnd.randrange(-0x30000, 0x30000)
        edge = rnd.random()
        if edge < 0.1:              # lands exactly on the ground
            vy = height
        elif edge < 0.2:            # reaches the right wall exactly
            vx = rnd.randrange(0x1000, 0x21000)
            x = (0x130 << 16) - vx
        elif edge < 0.25:
            vx = 0
        out.append((x, height, vx, vy, rnd.choice(bases)))
    return out


def put_llamas(image, c, records):
    for i, (x, h, vx, vy, base) in enumerate(records):
        struct.pack_into("<iiiih", image, LLAMAS + LLAMA_SIZE * i, x, h, vx, vy, base)
        c.llamas[i] = AttractLlama(x, h, vx, vy, base)


def check_llamas(want_image, c):
    for i in range(LLAMA_COUNT):
        want = struct.unpack_from("<iiiih", want_image, LLAMAS + LLAMA_SIZE * i)
        l = c.llamas[i]
        assert (l.x, l.height, l.vx, l.vy, l.sprite_base) == want, f"llama {i}"


def base_image(oracle, machine, rnd, c):
    """A random title-time image: options, tune, letters, title timer, controller."""
    img = bytearray(oracle.make_case(machine, rnd, 0x01).image)
    img[OPTIONS] = rnd.randrange(256)
    img[OPTIONS_DEFAULT] = rnd.randrange(256)
    img[CONTROL_A] = rnd.choice([0, 0x80]) | rnd.choice([0, 0, rnd.randrange(16), 1, 2, 4, 8])
    img[KEY_LATCH] = rnd.choice([0, 0, 0, rnd.randrange(16)])
    tune_len = c.tables.tune_length
    struct.pack_into("<HhhHH", img, TUNE_FIRE, rnd.randrange(2), rnd.choice([0x24, rnd.randrange(1, 0x40)]),
                     rnd.choice([-1, 1, 1, 2, rnd.randrange(0x30)]),
                     TUNE_LIST + rnd.choice([0, 2 * rnd.randrange(tune_len // 2 + 1)]), TUNE_LIST)
    for i in range(10):
        struct.pack_into("<h", img, TITLE_OBJECTS + 2 * i,
                         rnd.choice([-1, 0, 1, 2, 3, 0x80 + 0x10 * i, rnd.randrange(-2, 0x120)]))
    struct.pack_into("<h", img, TITLE_TIMER, rnd.choice([0x50, 0x50, 0x4f, 1, 0, -1, -1,
                                                         rnd.randrange(1, 0x50)]))
    struct.pack_into("<H", img, TICKS, rnd.randrange(0x10000))
    return img


def finish(oracle, img, rnd):
    status = (0, 0)
    return ThinkCase(oracle.dg.canonical(bytes(img)), 0, status)


def compare(oracle, want, got):
    bad = oracle.dg.mismatches(want, got, ignore=IGNORE)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"


def port_call(c, case, call):
    def run(state):
        c.controls[0] = case.image[CONTROL_A]
        call(ctypes.addressof(state))
    return run


# ---- logic ---------------------------------------------------------------------------------

@pytest.mark.parametrize("name, entry, stop", [("title_tune_start", "1000:083b", None),
                                               ("title_tune_begin", "1000:5c3f", "1000:5c5a"),
                                               ("title_tune_tick", "1000:084e", None)])
@pytest.mark.parametrize("case", fuzz_cases(96))
def test_tune(machine, oracle, c, name, entry, stop, case):
    rnd = case_rng(name, case)
    case_ = finish(oracle, base_image(oracle, machine, rnd, c), rnd)
    want, want_sounds = oracle.run_original(machine, case_, None, entry, stop)
    got, got_sounds = oracle.run_port(case_, None, port_call(c, case_, c.fn[name]))
    compare(oracle, want, got)
    assert got_sounds == want_sounds


@pytest.mark.parametrize("sound", ["driver", "stub"])
@pytest.mark.parametrize("case", fuzz_cases(128))
def test_title_tick_hook(machine, oracle, c, sound, case):
    # 1000:1514 swaps sound_on and [0400] together: the driver (AX = 0300h after a request)
    # with sound on, game86's stub (AX = 0) with it off. The landing request's AX is 3fa1's
    # "still dropping" flag (see attract.c).
    rnd = case_rng(f"title_tick_hook_{sound}", case)
    img = base_image(oracle, machine, rnd, c)
    if rnd.random() < 0.4:                  # the last dropping letters land on this tick
        for i in range(10):
            struct.pack_into("<h", img, TITLE_OBJECTS + 2 * i, rnd.choice([-1, 0, 0, 1, 2]))
        struct.pack_into("<h", img, TITLE_TIMER, 0x50)
    img[SOUND_ON] = sound == "driver"
    put_llamas(img, c, llama_records(rnd, c))
    case_ = finish(oracle, img, rnd)
    want, want_sounds = oracle.run_original(machine, case_, None, "1000:3f6f",
                                            driver=None if sound == "driver" else SOUND_OFF_STUB)
    got, got_sounds = oracle.run_port(case_, None, port_call(c, case_, c.fn["title_tick_hook"]))
    compare(oracle, want, got)
    assert got_sounds == want_sounds
    check_llamas(want, c)


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_attract_end(machine, oracle, c, case):
    """attract_end: after a title tick, the port's slots 0..8 hold the original's llama bytes."""
    rnd = case_rng("attract_end", case)
    img = base_image(oracle, machine, rnd, c)
    img[SOUND_ON] = 1                       # the oracle's driver, not the sound-off stub
    put_llamas(img, c, llama_records(rnd, c))
    case_ = finish(oracle, img, rnd)
    want, _ = oracle.run_original(machine, case_, None, "1000:3f6f")

    def run(state):
        port_call(c, case_, c.fn["title_tick_hook"])(state)
        c.attract_end(ctypes.addressof(state))
    got, _ = oracle.run_port(case_, None, run)
    bad = oracle.dg.mismatches(want, got, ignore=set(SCRATCH) | DRAW_STATE)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"


@pytest.mark.parametrize("case", fuzz_cases(8))
def test_attract_llamas_init(machine, oracle, c, case):
    rnd = case_rng("attract_llamas_init", case)
    img = base_image(oracle, machine, rnd, c)
    put_llamas(img, c, llama_records(rnd, c))
    case_ = finish(oracle, img, rnd)
    want, _ = oracle.run_original(machine, case_, None, "1000:44d7")
    c.llamas_init()
    check_llamas(want, c)


# ---- drawing -------------------------------------------------------------------------------

class Art:
    """Random sprite-bank sprites (the numbers the title uses) and g3 shapes, in the
    original's segments and as C lookups."""

    def __init__(self, machine, rnd, c):
        numbers = set(c.tables.letter_sprites) | set(range(8, 13))
        for base in c.tables.llama_sprites:
            numbers |= set(range(base, base + 8))
        self.sprites, blob, start = {}, bytearray(), SPRITE_TABLE + 2 * (max(numbers) + 1)
        llama_numbers = numbers - set(c.tables.letter_sprites) - set(range(8, 13))
        for n in sorted(numbers):
            # Like the real frames: one column; llamas short enough (real: 10..12 rows) to end
            # above the page's last row, since with dirty recording off the original does not
            # clip in y and would run on into the next page.
            s = Sprite(rnd, columns=1, rows=rnd.randrange(1, 14) if n in llama_numbers else None)
            machine.write_u16(SPRITE_SEG, SPRITE_TABLE + 2 * n, start + len(blob))
            blob += s.original_bytes()
            self.sprites[n] = s
        machine.write(SPRITE_SEG, start, bytes(blob))
        machine.write_u16(CODE0, CS_SPRITE_SEG, SPRITE_SEG)
        self.shapes, blob, start = {}, bytearray(), 0x40
        for i in G3_SHAPES:
            s = Sprite(rnd, columns=rnd.choice([1, 1, 2]))
            machine.write_u16(G3_SEG, 2 * i, start + len(blob))
            blob += s.original_bytes()
            self.shapes[i] = s
        machine.write(G3_SEG, start, bytes(blob))
        machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)

        self.c_sprites = {n: s.to_c() for n, s in self.sprites.items()}
        self.c_shapes = {i: s.to_c() for i, s in self.shapes.items()}

        @LOOKUP
        def sprite(n):
            return ctypes.addressof(self.c_sprites[n]) if n in self.c_sprites else None

        @LOOKUP
        def shape(i):
            return ctypes.addressof(self.c_shapes[i]) if i in self.c_shapes else None

        self._keep = (sprite, shape)
        self.art = AttractArt(ctypes.cast(sprite, ctypes.c_void_p), ctypes.cast(shape, ctypes.c_void_p))


def setup_pages(machine, c, rnd):
    for off, pid in PAGE_IDS.items():
        px = indexed_page(rnd, rnd.choice(["noise", "blocks"]))
        machine.ega.load_indexed(px, page=off)
        c.load(pid, px)


def page_offset(pid):
    return {v: k for k, v in PAGE_IDS.items()}[pid]


def setup_machine(machine, img, c, dirty):
    draw, shown = page_offset(c.draw_page_id()), page_offset(c.display_page_id())
    struct.pack_into("<HHH", img, FRONT_PAGE, draw, shown, draw)
    img[SCROLL_MODE] = 0
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1 if dirty else 0)
    machine.write_u16(CODE0, CS_DIRTY_TMP, DIRTY_LIST)
    machine.write_u16(CODE0, DIRTY_LIST, LIST_END)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)


def check_pages(machine, c):
    for off, pid in PAGE_IDS.items():
        got, want = c.read(pid), machine.ega.rasterise(off)
        if got != want:
            i = next(k for k in range(len(got)) if got[k] != want[k])
            raise AssertionError(f"page {pid}: first difference at x={i % WIDTH} y={i // WIDTH}: "
                                 f"port {got[i]}, original {want[i]}")


def restore_frame(machine, c):
    """Erase the frame with the recorded rectangles (1e7b:0213..0299 vs gfx_restore_dirty)
    and compare the pages again."""
    draw, shown = page_offset(c.draw_page_id()), page_offset(c.display_page_id())
    machine.write_u16(CODE0, CS_DIRTY_PTR_B, DIRTY_LIST)
    machine.write_u16(*CS_CODE0_SEG, CODE0)
    machine.write_dgroup(FRONT_PAGE, struct.pack("<2H", shown, draw))
    machine.run_until("1e7b:0213", "1e7b:029a", max_insns=2_000_000)
    c.restore_dirty(c.draw_page_id())
    check_pages(machine, c)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_title_draw(machine, oracle, c, case):
    rnd = case_rng("title_draw", case)
    art = Art(machine, rnd, c)
    img = base_image(oracle, machine, rnd, c)
    setup_pages(machine, c, rnd)
    setup_machine(machine, img, c, dirty=False)
    case_ = finish(oracle, img, rnd)
    want, _ = oracle.run_original(machine, case_, None, "1000:3ee0", "1000:3f6b")
    got, _ = oracle.run_port(case_, None, lambda state: c.title_draw(ctypes.addressof(state),
                                                                      ctypes.byref(art.art)))
    c.set_dirty(False)
    compare(oracle, want, got)
    check_pages(machine, c)


@pytest.mark.parametrize("case", fuzz_cases(96))
def test_title_draw_hook(machine, oracle, c, case):
    rnd = case_rng("title_draw_hook", case)
    art = Art(machine, rnd, c)
    img = base_image(oracle, machine, rnd, c)
    put_llamas(img, c, llama_records(rnd, c))
    setup_pages(machine, c, rnd)
    dirty = rnd.random() < 0.5
    setup_machine(machine, img, c, dirty)
    case_ = finish(oracle, img, rnd)

    machine.write_dgroup(0, case_.image)
    machine.call("1000:402c", far=True, max_insns=50_000_000)
    want = machine.read_dgroup(0, 0x10000)

    state = oracle.dg.from_dgroup(case_.image)
    c.controls[0] = case_.image[CONTROL_A]
    target = c.target(c.draw_page_id())
    c.set_dirty(dirty)
    c.draw_hook(ctypes.addressof(state), ctypes.byref(target), ctypes.byref(art.art))
    c.set_dirty(False)
    got = oracle.dg.to_dgroup(state, case_.image)
    compare(oracle, want, got)
    check_pages(machine, c)
    if dirty:
        restore_frame(machine, c)
