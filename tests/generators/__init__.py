"""Seeded generators of random but valid inputs for the oracle tests (PLAN §3.2).

Every case is reproducible: `case_rng(name, i)` derives its seed from the test name,
the case number and $LLAMATRON_FUZZ_SEED (default 0). $LLAMATRON_FUZZ_SCALE
multiplies the number of cases (`fuzz_cases`) for long local runs, e.g.
LLAMATRON_FUZZ_SCALE=50 ctest -L oracle.

Generators only produce states that respect the invariants we know. They say which
invariants are hypotheses ([H]) so W4/W5 can tighten them as routines get ported.
"""

from __future__ import annotations

import os
import random
import zlib

from adapters.entities import ENTITY_COUNT, ENTITY_SIZE, EntityRecord
from adapters.rng import PERM_SIZE, RngState

SCREEN_W, SCREEN_H = 320, 200


def fuzz_cases(base: int) -> range:
    return range(base * max(1, int(os.environ.get("LLAMATRON_FUZZ_SCALE", "1"))))


def case_rng(name: str, case: int) -> random.Random:
    seed = int(os.environ.get("LLAMATRON_FUZZ_SEED", "0"))
    return random.Random(zlib.crc32(f"{seed}:{name}:{case}".encode()))


# ---- RNG ----------------------------------------------------------------------

def rng_state(rnd: random.Random) -> RngState:
    """Any table contents, index in range (edge values favoured), any mixer words."""
    return RngState(table=bytes(rnd.randrange(256) for _ in range(PERM_SIZE)),
                    index=rnd.choice([0, PERM_SIZE - 1, rnd.randrange(PERM_SIZE)]),
                    mixer=tuple(rnd.randrange(0x10000) for _ in range(4)))


# ---- video --------------------------------------------------------------------

def indexed_page(rnd: random.Random, style: str = "noise") -> bytes:
    """A 320x200 page of 4-bit colour indices.
    noise: every pixel random; blocks: random filled rectangles (byte-aligned and
    not, to exercise partial bit masks); blank: one random colour."""
    if style == "blank":
        return bytes([rnd.randrange(16)]) * (SCREEN_W * SCREEN_H)
    if style == "noise":
        return bytes(rnd.randrange(16) for _ in range(SCREEN_W * SCREEN_H))
    page = bytearray([rnd.randrange(16)]) * (SCREEN_W * SCREEN_H)
    for _ in range(rnd.randrange(4, 20)):
        x0, y0, x1, y1 = screen_rect(rnd)
        c = rnd.randrange(16)
        for y in range(y0, y1 + 1):
            page[y * SCREEN_W + x0:y * SCREEN_W + x1 + 1] = bytes([c]) * (x1 - x0 + 1)
    return bytes(page)


def screen_rect(rnd: random.Random, margin_x: int = 0, margin_y: int = 0) -> tuple[int, int, int, int]:
    """Inclusive (x0, y0, x1, y1) with x0 < x1, y0 < y1, inside the screen minus margins."""
    x0, x1 = sorted(rnd.sample(range(0, SCREEN_W - margin_x), 2))
    y0, y1 = sorted(rnd.sample(range(0, SCREEN_H - margin_y), 2))
    return x0, y0, x1, y1


# ---- entities -----------------------------------------------------------------

PLAYFIELD_X = (0x0F, 0x130)   # player clamp range from 1000:0981.. (re-notes §4)
PLAYFIELD_Y = (0x12, 0xBC)


def entity(rnd: random.Random, active: bool = True, n_types: int = 41,
           n_states: int = 19, n_hit: int = 18) -> EntityRecord:
    """One record. Free slots: type = -1, rest zero. Active slots:
    [H] type in [0, n_types) (41 enemy init handlers), state < n_states (DS:202E),
    hit indices < n_hit (CS:0C04), position inside the player clamp box, other
    bytes random (unknown fields: tighten once mapped)."""
    rec = EntityRecord()
    if not active:
        rec.set("type", -1)
        return rec
    rec.raw[:] = bytes(rnd.randrange(256) for _ in range(ENTITY_SIZE))
    rec.set("type", rnd.randrange(n_types))
    rec.set("x", rnd.randrange(*PLAYFIELD_X))
    rec.set("y", rnd.randrange(*PLAYFIELD_Y))
    rec.set("state", rnd.randrange(n_states))
    rec.set("hit_a", rnd.randrange(n_hit))
    rec.set("hit_b", rnd.randrange(n_hit))
    return rec


def entity_table(rnd: random.Random, n_active: int | None = None, **kw) -> list[EntityRecord]:
    """70 records, n_active of them in use (random count and slots by default)."""
    if n_active is None:
        n_active = rnd.randrange(ENTITY_COUNT + 1)
    used = set(rnd.sample(range(ENTITY_COUNT), n_active))
    return [entity(rnd, i in used, **kw) for i in range(ENTITY_COUNT)]
