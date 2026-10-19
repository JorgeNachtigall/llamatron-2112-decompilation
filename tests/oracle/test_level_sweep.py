"""M4 level sweep: every one of the 100 level scripts starts through the warp poke and plays
for a while without a crash, an unported handler, a runaway entity or a draw outside its page
(tests/oracle/level_sweep.py, which also runs the whole sweep by hand and through a sanitizer
build of llamatron_replay with --replay).

$LLAMATRON_SWEEP_REPLAY (a llamatron_replay binary, e.g. an ASan/UBSan build) adds the same
input run through it per level.
"""

import os
from pathlib import Path

import pytest

from level_sweep import LEVELS, describe, repro, run_child, run_replay

TICKS = 3000            # about 60 s of play after the warp


@pytest.mark.parametrize("level", range(LEVELS))
def test_level(cgame, level):
    r = run_child(level, TICKS)
    assert r["ok"], f"{describe(r)}\n  repro: {repro(level, TICKS)}"


@pytest.mark.skipif(not os.environ.get("LLAMATRON_SWEEP_REPLAY"),
                    reason="LLAMATRON_SWEEP_REPLAY (a sanitizer llamatron_replay) unset")
@pytest.mark.parametrize("level", range(LEVELS))
def test_level_replay(tmp_path, level):
    r = run_replay(Path(os.environ["LLAMATRON_SWEEP_REPLAY"]), level, TICKS, tmp_path)
    assert r["ok"], describe(r)
