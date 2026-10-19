"""The GameState <-> DGROUP adapter (tests/adapters/dgroup_map.c) itself: layout table
without overlaps, round trips on random and real images, and the encodings that are not
plain little-endian words (score digits, high-score rows, spawn cursor, flash list)."""

import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.gamestate import DGROUP_SIZE, MASK_DERIVED, MASK_FIELD, MASK_UNMAPPED, DgroupMap

LEVEL_TABLE = 0x2BDA            # tbl_level_scripts, 100 words
ROSTER = 0x220A
SPAWN_CURSOR = 0x39EB
FLASH_LIST = 0x06C7
SCORES = 0x4441
HISCORES = 0x40A0
# operand bytes of level-script opcodes 0xFF..0xF8 (tools/extract/FORMATS.md, levels.dat)
OP_OPERANDS = [2, 4, 4, 2, 2, 2, 3, 1]


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


@pytest.fixture
def image(machine):
    return machine.read_dgroup(0, DGROUP_SIZE)


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def put16(buf, off, v):
    b = bytearray(buf)
    struct.pack_into("<H", b, off, v & 0xFFFF)
    return bytes(b)


def test_mask_has_no_overlaps(dg):
    assert dg.overlaps == 0
    m = dg.mask
    assert m[0x21A6] == MASK_FIELD and m[0x21CC] == MASK_DERIVED and m[0x2209] == MASK_DERIVED
    assert m[0x058D] == MASK_DERIVED          # keyboard = !joystick
    assert m[0x0413] == MASK_UNMAPPED         # routine scratch
    assert all(m[0x48C1 + i] == MASK_FIELD for i in range(70 * 64))
    assert all(m[0x4881 + i] == MASK_FIELD for i in range(64))


def test_mask_matches_data_availability(dg):
    mapped = dg.mask[SPAWN_CURSOR] != MASK_UNMAPPED and dg.mask[FLASH_LIST] != MASK_UNMAPPED
    assert mapped == dg.has_data


@pytest.mark.parametrize("case", fuzz_cases(20))
def test_round_trip_random(dg, case):
    rnd = case_rng("dgroup_map.round_trip", case)
    raw = rnd.randbytes(DGROUP_SIZE)
    canon = dg.canonical(raw)
    assert dg.canonical(canon) == canon
    assert all(canon[i] == raw[i] for i in range(DGROUP_SIZE) if dg.mask[i] == MASK_UNMAPPED)


def test_round_trip_game86_image(dg, image):
    canon = dg.canonical(image)
    assert dg.canonical(canon) == canon
    # Before the game runs, the loaded image is canonical except for the never-referenced
    # player bytes, the spawn cursor (0, no list yet) and the high-score rows (zeros until
    # scores.yak is read). P2's trail_head (2CF9) starts as 2 in the image, not a multiple of
    # 8; the trail code masks it with 0x3F8 before use. The tick hook (39E4) is 0 until
    # entry installs hook_nop, which is what TICK_HOOK_NONE writes.
    allowed = {0x2CF9, *range(HISCORES, HISCORES + 160)}
    if u16(image, 0x39E4) == 0:
        allowed |= {0x39E4, 0x39E5}
    diff = [i for i in range(DGROUP_SIZE)
            if canon[i] != image[i] and dg.mask[i] != MASK_DERIVED and i not in allowed]
    assert diff == [], [hex(i) for i in diff[:16]]


def test_score_digits(dg, image):
    raw = bytearray(image)
    raw[SCORES:SCORES + 8] = bytes([0, 1, 2, 3, 4, 5, 6, 7])
    raw[SCORES + 8:SCORES + 16] = bytes([9] * 8)
    assert dg.canonical(bytes(raw))[SCORES:SCORES + 16] == bytes(raw[SCORES:SCORES + 16])
    raw[SCORES + 7] = 12              # not a digit: carried into the number
    assert dg.canonical(bytes(raw))[SCORES:SCORES + 8] == bytes([0, 1, 2, 3, 4, 5, 7, 2])


def test_trail_positions_are_offsets(dg, image):
    raw = bytearray(image)
    struct.pack_into("<HH", raw, 0x2CF7, 0x3F8, 0x0010)      # trail_head = entry index * 8
    struct.pack_into("<HH", raw, 0x06BA, 0x0028, 0x0050)      # follower_lag = entries * 8
    canon = dg.canonical(bytes(raw))
    assert canon[0x2CF7:0x2CFB] == raw[0x2CF7:0x2CFB]
    assert canon[0x06BA:0x06BE] == raw[0x06BA:0x06BE]
    struct.pack_into("<H", raw, 0x2CF7, 0x3FF)                # low bits are not representable
    assert struct.unpack_from("<H", dg.canonical(bytes(raw)), 0x2CF7)[0] == 0x3F8


THINK_TABLE = 0x1FD2


def clobber_range(image):
    """The 4 bytes enemy_shot_init zeroes at 1000:123c/1241: [SI+8], [SI+0A] with SI =
    think_13's code offset (DS:1FD2 entry 0x13) taken as a DS address."""
    si = u16(image, THINK_TABLE + 2 * 0x13)
    return range(si + 8, si + 12)


def test_spawns_clobbered_10b0(dg, image):
    if not dg.has_data:
        pytest.skip("extracted data not available")
    clobber = clobber_range(image)
    assert all(dg.mask[i] == MASK_FIELD for i in clobber)
    assert any(image[i] for i in clobber)          # intact in the loaded image
    assert dg.canonical(image)[clobber.start:clobber.stop] == image[clobber.start:clobber.stop]
    zeroed = bytearray(image)
    zeroed[clobber.start:clobber.stop] = bytes(4)
    state = dg.from_dgroup(bytes(zeroed))           # reads as clobbered ...
    assert dg.to_dgroup(state, image)[clobber.start:clobber.stop] == bytes(4)   # ... and writes 0
    # intact: written from levels.dat, so a blank base (the replay driver) gets the script bytes
    blank = dg.to_dgroup(dg.from_dgroup(image))
    assert blank[clobber.start:clobber.stop] == image[clobber.start:clobber.stop]


PAUSE_MESSAGE, PAUSE_POINTERS = 0x09EC, 0x09E2


def test_pause_message(dg, image):
    assert all(dg.mask[i] == MASK_FIELD for i in (PAUSE_MESSAGE, PAUSE_MESSAGE + 1))
    pointers = struct.unpack_from("<4H", image, PAUSE_POINTERS)
    assert u16(dg.canonical(put16(image, PAUSE_MESSAGE, 0)), PAUSE_MESSAGE) == 0
    for p in pointers:                              # 1000:15aa stores one of these words
        state = dg.from_dgroup(put16(image, PAUSE_MESSAGE, p))
        assert u16(dg.to_dgroup(state, image), PAUSE_MESSAGE) == p
        if dg.has_data:                             # from the extracted sprites, not the image
            assert u16(dg.to_dgroup(state), PAUSE_MESSAGE) == p


def test_tune_pointers_are_list_offsets(dg, image):
    raw = bytearray(image)
    struct.pack_into("<HH", raw, 0x062B, 0x09D5 + 7, 0x09D5)
    assert dg.canonical(bytes(raw))[0x062B:0x062F] == raw[0x062B:0x062F]
    state = dg.from_dgroup(bytes(raw))
    blank = dg.to_dgroup(state)                     # offsets survive a write into zeros
    assert struct.unpack_from("<HH", blank, 0x062B) == (0x09D5 + 7, 0x09D5)


def test_hiscore_rows(dg, image):
    raw = bytearray(image)
    raw[HISCORES:HISCORES + 16] = b"00123450ABCDEFGH"
    assert dg.canonical(bytes(raw))[HISCORES:HISCORES + 16] == b"00123450ABCDEFGH"


def level_spawn_list(image, level):
    """DS address of a level's first (type, count) pair, walked in the original bytes."""
    off = u16(image, LEVEL_TABLE + 2 * level)
    while image[off] >= 0xF8:
        off += 1 + OP_OPERANDS[0xFF - image[off]]
    return off + 1                    # the wave-type byte


def test_spawn_cursor_roster(dg, image):
    if not dg.has_data:
        pytest.skip("extracted data not available")
    for index in (0, 1, 7, 49):
        raw = put16(image, SPAWN_CURSOR, ROSTER + 2 * index)
        assert u16(dg.canonical(raw), SPAWN_CURSOR) == ROSTER + 2 * index


@pytest.mark.parametrize("level", [0, 1, 17, 63, 99])
def test_spawn_cursor_level(dg, image, level):
    if not dg.has_data:
        pytest.skip("extracted data not available")
    start = level_spawn_list(image, level)
    end = 0                           # index of the (x, 0xFF) end marker, a valid position too
    while image[start + 2 * end + 1] != 0xFF:
        end += 1
    for index in range(end + 1):
        raw = put16(image, SPAWN_CURSOR, start + 2 * index)
        assert u16(dg.canonical(raw), SPAWN_CURSOR) == start + 2 * index


def test_flash_list_null_and_unknown(dg, image):
    if not dg.has_data:
        pytest.skip("extracted data not available")
    assert u16(dg.canonical(put16(image, FLASH_LIST, 0)), FLASH_LIST) == 0
    # an address that is not a flash list cannot be represented: reads back as none
    assert u16(dg.canonical(put16(image, FLASH_LIST, 0x0001)), FLASH_LIST) == 0
