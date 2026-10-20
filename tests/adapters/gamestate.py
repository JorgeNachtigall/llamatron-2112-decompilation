"""GameState <-> game86 DGROUP, through the C adapter in the testlib (dgroup_map.h).

Typical oracle comparison of one routine:

    dg = DgroupMap(cgame)
    image = dg.canonical(machine.read_dgroup(0, 0x10000))  # or a generated state
    machine.write_dgroup(0, image)
    state = dg.from_dgroup(image)          # opaque GameState buffer for the C call
    res = machine.call(ENTRY, ...)         # the original
    c_function(state, ...)                 # the port, on a GameState*
    assert dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, image)) == []

The GameState is passed to C as an opaque buffer (ctypes.byref works); tests that need a
field from Python read it back through to_dgroup and the DS offset.
"""

from __future__ import annotations

import ctypes
import struct

from adapters.cport import bind

DGROUP_SIZE = 0x10000
MASK_UNMAPPED, MASK_FIELD, MASK_DERIVED = 0, 1, 2

# Words that game86 uses as scratch inside routines (not state): ignored in comparisons.
# 0413/0415 (Thorn), 0419..0421 enemy-shot template (W5: scratch, not in GameState).
SCRATCH = frozenset({*range(0x0413, 0x0417), *range(0x0419, 0x0423)})

# 48C1..4B00 (entity slots 0..8) hold the 32 title llamas while the attract/title hooks run;
# the port keeps them inside attract.c, not in GameState.entities. Attract-mode comparisons
# pass this in `ignore`; entities_clear rebuilds the table when a game starts.
ATTRACT_LLAMAS = range(0x48C1, 0x4B01)


# A follower (think_06, 1000:4bfb) keeps the trail it walks in kill_action (+1E): the original
# as the trail's DS address (think_05 1000:4d04..4d97), the port as the player index
# (think_llamas.c FOLLOW_P1/P2). The word stays after the slot stops being a follower (hit, or
# reused by another entity), so the adapter's write-back cannot tell from the slot's type what
# it is (a mapping there broke test_w5a_types). Compared instead: equal where the original
# holds exactly 6CC9/70C9 and the port the matching 0/1 (Thorn's rule, whole_game.py).
ENTITY_TABLE, ENTITY_COUNT, ENTITY_SIZE = 0x48C1, 70, 0x40
FOLLOWER_TRAIL = 0x1E
TRAIL_ADDRESS = {0x6CC9: 0, 0x70C9: 1}


def follower_trail_equal(original: bytes, port: bytes) -> set[int]:
    """DS offsets (both bytes of +1E) where the two images hold the same follower trail."""
    same = set()
    for slot in range(ENTITY_COUNT):
        at = ENTITY_TABLE + ENTITY_SIZE * slot + FOLLOWER_TRAIL
        address = struct.unpack_from("<H", original, at)[0]
        if address in TRAIL_ADDRESS and struct.unpack_from("<h", port, at)[0] == TRAIL_ADDRESS[address]:
            same.update((at, at + 1))
    return same


# DEVIATIONS #14: chain_tail (06A4/06A6) is P1's trail entry at trail_head[0] + follower_lag[0]
# (1000:194f, unmasked, in bytes from P1's ring at 6CC9). Past the end of P2's ring (74C9) the
# original reads BSS and then the LZSS loader's ring buffer at SS:0000, which the port does not
# model (it wraps to P1's start). Skipped on such a snapshot only (Moss): an entry >= 512, with
# the head as it is or one step back (the tick read the tail before the head moved on: level 99
# lockstep tick 1414, head 0 after 3F8 with lag 438).
CHAIN_TAIL = range(0x06A4, 0x06A8)
TRAIL_HEAD_P1, FOLLOWER_LAG_P1 = 0x2CF7, 0x06BA
TRAIL_STEP, TRAIL_HEAD_MASK = 8, 0x3F8
TRAILS_END = 0x800                     # P1's ring and P2's, in bytes from 6CC9


def chain_tail_past_rings(original: bytes) -> set[int]:
    """CHAIN_TAIL's offsets when the original read it past P2's ring, else nothing."""
    head, lag = (struct.unpack_from("<H", original, at)[0] for at in (TRAIL_HEAD_P1, FOLLOWER_LAG_P1))
    before = (head - TRAIL_STEP) & TRAIL_HEAD_MASK
    return set(CHAIN_TAIL) if max(head, before) + lag >= TRAILS_END else set()


class DgroupMap:
    def __init__(self, lib: ctypes.CDLL):
        self.lib = lib
        self._size = bind(lib, "gamestate_size", ctypes.c_size_t)()
        self._from = bind(lib, "gamestate_from_dgroup", None, ctypes.c_void_p, ctypes.c_char_p)
        self._to = bind(lib, "gamestate_to_dgroup", None, ctypes.c_void_p, ctypes.c_void_p)
        self._mask = bind(lib, "dgroup_map_mask", ctypes.c_uint, ctypes.c_void_p)
        self.has_data = bool(bind(lib, "dgroup_map_has_data", ctypes.c_bool)())
        self.entity = bind(lib, "gamestate_entity", ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)
        buf = ctypes.create_string_buffer(DGROUP_SIZE)
        self.overlaps = self._mask(buf)
        self.mask = buf.raw

    def new_state(self) -> ctypes.Array:
        """A zeroed GameState."""
        return ctypes.create_string_buffer(self._size)

    def from_dgroup(self, dgroup: bytes, state: ctypes.Array | None = None) -> ctypes.Array:
        assert len(dgroup) == DGROUP_SIZE
        state = state if state is not None else self.new_state()
        self._from(state, bytes(dgroup))
        return state

    def to_dgroup(self, state: ctypes.Array, base: bytes | None = None) -> bytes:
        """`base` (zeros by default) with every mapped byte replaced from `state`."""
        buf = ctypes.create_string_buffer(bytes(base) if base is not None else bytes(DGROUP_SIZE),
                                          DGROUP_SIZE)
        self._to(state, buf)
        return buf.raw

    def canonical(self, dgroup: bytes) -> bytes:
        """`dgroup` with its mapped bytes in the encoding the adapter writes back."""
        return self.to_dgroup(self.from_dgroup(dgroup), dgroup)

    def mismatches(self, original: bytes, port: bytes, ignore=SCRATCH) -> list[int]:
        """Mapped DS offsets where the two images differ (a follower's trail held as address
        and as player index is the same: follower_trail_equal; chain_tail read past the trail
        rings is DEVIATIONS #14: chain_tail_past_rings)."""
        same = follower_trail_equal(original, port) | chain_tail_past_rings(original)
        return [i for i in range(DGROUP_SIZE)
                if self.mask[i] != MASK_UNMAPPED and original[i] != port[i] and i not in ignore
                and i not in same]
