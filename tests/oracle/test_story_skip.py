"""W4 story scroller skip: game86 text_scroller 1000:5883..58a4 (skip -> hiscore_table_show
at 5941 unless g_scroll_state locks it) against src/game/draw_scroller.c, the path main_sm
takes from SCROLLER_SHOW_HISCORE to the table (main_sm.c scroller_step).

A story skip (space or stick button 1) hands over to the hi-score table at once, except while
g_scroll_state is set: from the late text's print (window offset past 0xD000, 58eb) until the
offset wraps (58c4). A skip in that window is ignored and the text scrolls on. Uses the
lockstep harness of test_scroller.py with one skip at a chosen input check.
"""

import pytest
from generators import case_rng

from test_scroller import c, run_scroller  # noqa: F401  (c is a fixture)

ROW = 0x28                     # window offset step per frame
LATE_START = 0xD000            # 58e5: late text and the lock after this offset
WRAP_CHECK = 0x10000 // ROW    # input checks until the offset wraps and the lock ends


def skip_at(check):
    return [(False, False)] * check + [(True, False)]


@pytest.mark.parametrize("check", [3, 0x400, LATE_START // ROW - 8, WRAP_CHECK + 40, WRAP_CHECK + 600])
def test_story_skip_outside_late_window_shows_table(machine, c, check):
    """Before the late text or after the wrap: the skip ends the run at that very check."""
    log = run_scroller(machine, c, case_rng("story_skip_out", check), story=True,
                       script=skip_at(check))
    assert log["hiscore_counter"] is not None
    assert log["checks"] == check + 1


@pytest.mark.parametrize("check", [LATE_START // ROW + 8, 0xE000 // ROW + 8, WRAP_CHECK - 4])
def test_story_skip_inside_late_window_is_ignored(machine, c, check):
    """Between the late text and the wrap: the skip changes nothing; the run ends normally
    at the table (5928) much later."""
    log = run_scroller(machine, c, case_rng("story_skip_in", check), story=True,
                       script=skip_at(check))
    assert log["hiscore_counter"] is not None
    assert log["checks"] > WRAP_CHECK
