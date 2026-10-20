"""Golden reference runs and the port's replay of them (PLAN §3.3).

Scripted DOSBox-X runs (pinned RNG tick, PROBE.KEY input, PROBE.POK state pokes for what
keys cannot reach, DGROUP/VRAM snapshots every N IRQs) are checked against the hashes in
tests/golden/reference_runs.json (hashes only: the snapshots come from the original's
memory and are never committed). Then the port runs the
same script headless (llamatron_replay, tests/replay/) and tests/replay/compare.py reports,
per snapshot tick, which mapped DGROUP bytes differ.

While the port's game loop is incomplete the comparison is a progress report (written to
the build tree, printed with -s); LLAMATRON_REPLAY_STRICT=1 turns any difference into a
failure. LLAMATRON_GOLDEN_UPDATE=1 rewrites the golden entry from this run (only after a
deliberate change of the probe or the script).
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "harness" / "dosbox"))
sys.path.insert(0, str(ROOT / "tests" / "replay"))

ORIGINAL = Path(os.environ.get("LLAMATRON_ORIGINAL_DIR") or ROOT / "original")
WORKDIR = Path(os.environ.get("LLAMATRON_DOSBOX_WORKDIR") or ROOT / "build-dosbox")
GOLDEN = ROOT / "tests" / "golden" / "reference_runs.json"

pytestmark = pytest.mark.skipif(
    not (shutil.which("dosbox-x") and shutil.which("nasm") and (ORIGINAL / "llama.exe").exists()),
    reason="needs dosbox-x, nasm and the original files")

SPACE, SPACE_UP, RIGHT, RIGHT_UP, UP, UP_UP = 0x39, 0xB9, 0x4D, 0xCD, 0x48, 0xC8
LEFT, LEFT_UP = 0x4B, 0xCB
SEED = 0x0012ABCD

# DS offsets the pokes write (docs/re-notes-game86.md §8): mapped by the adapter, so the
# port's replay applies the same words.
LIVES, CONTINUES, WAVE_RESULT, SCORE_P1 = 0x21A6, 0x061F, 0x0810, 0x4441
WAVE_WARP = 4                   # pickup_effect_01 (1000:0ff7): +5 levels
START = [(150, SPACE), (160, SPACE_UP)]         # fire during the title tune starts a game
WALK = [(400, RIGHT), (480, RIGHT_UP), (500, UP), (560, UP_UP)]


def press(tick, make, hold=8):
    """Name entry moves when tick_count % 8 == 0 and resets it (1000:5b06): 8 ticks = 1 step."""
    return [(tick, make), (tick + hold, make | 0x80)]


# Timelines (phase from the tick hook 39E4) were taken from exploratory runs of this seed.
#
# tick_phase / start_delay: the port replay's clock calibration for the run (llamatron_replay and
# the app's --tick-phase F / --start-delay N, src/engine/timing.h timing_set_phase): the fraction
# of a timer tick elapsed at game_init, and the retraces of timer ticks the original's start-up
# takes before its first frame. Fitted per run by sweeping both (phase in 0.025 steps, then 0.005
# around the edges; delay 0..6) for the latest first real divergence and fewest real bytes outside
# the attract llamas (48C1..4B00). The window the fit held is noted; the value is its middle. Runs
# that start a game with fire on the title tune don't depend on start_delay (0).
RUNS = {
    "walk_vga": dict(
        summary="fire on the title tune starts a game (play from ~175), P1 walks right and up",
        ticks=SEED, machine="vga", interval=50, max_snapshots=14, keys=START + WALK,
        quit_after=720, tick_phase=0.83, start_delay=0),       # 0.825..0.835
    "walk_ega": dict(
        summary="walk_vga on an EGA machine (59.92 Hz frames, no VRAM capture)",
        ticks=SEED, machine="ega", interval=50, max_snapshots=14, keys=START + WALK,
        quit_after=720, tick_phase=0.375, start_delay=0),      # 0.325..0.425 (0.135..0.165)
    "attract": dict(
        summary="no input: title tune to ~1100, title menu to ~2600, then the attract cycle "
                "(1500-tick timeout, 1000:013b)",
        ticks=SEED, machine="vga", interval=50, max_snapshots=60, keys=[], quit_after=3000,
        tick_phase=0.375, start_delay=4, time_limit=150),       # delay 4: 0..0.75
    "two_player": dict(
        summary="title menu right x2 = 2 players with the droid (options 3), fire at 1200, "
                "P1 walks right",
        ticks=SEED, machine="vga", interval=50, max_snapshots=38,
        keys=[(1150, RIGHT), (1156, RIGHT_UP), (1170, RIGHT), (1176, RIGHT_UP),
              (1200, SPACE), (1210, SPACE_UP), (1400, RIGHT), (1480, RIGHT_UP)],
        quit_after=1900, tick_phase=0.3, start_delay=5, time_limit=150,  # delay 5: 0.1..0.5
        input_shift=[(1100, 1, None)]),         # a tick behind from the title menu on
    "warp": dict(
        summary="walk_vga with wave_result poked to 4 (warp) at 300: banner, level 0 -> 5, "
                "next wave from ~375",
        ticks=SEED, machine="vga", interval=25, max_snapshots=40, keys=START + WALK,
        pokes=[(300, WAVE_RESULT, WAVE_WARP)], quit_after=1000,
        tick_phase=0.55, start_delay=0),                        # 0.425..0.7
    "death_continue": dict(
        summary="lives poked to 1 at 250, idle death ~560, continue prompt (0816), fire at "
                "700 continues with 5 lives",
        ticks=SEED, machine="vga", interval=50, max_snapshots=30, keys=START + press(700, SPACE),
        pokes=[(250, LIVES, 1)], quit_after=1500, tick_phase=0.43, start_delay=0,  # 0.425..0.44
        time_limit=120),
    "game_over_hiscore": dict(
        summary="lives 1, continues 1 and score 00500000 poked at 250: idle death = game over, "
                "name entry (5b06) from ~590: a, b, then '*' ends it",
        ticks=SEED, machine="vga", interval=25, max_snapshots=50,
        keys=START + press(650, SPACE) + press(680, RIGHT) + press(720, SPACE)
        + press(750, LEFT) + press(780, LEFT) + press(820, SPACE),
        pokes=[(250, LIVES, 1), (250, CONTINUES, 1), (250, SCORE_P1 + 2, 0x0005)],
        quit_after=1300, tick_phase=0.625, start_delay=0, time_limit=120),  # 0.425..0.825
}


# Warped levels (M4, Prism): level_sweep's own input (tests/oracle/level_sweep.py key_script /
# poke_script, pure, shared with the port-only sweep): fire at 150 starts a game, at 300 the
# level is poked to L - 5 and wave_result to 4 (warp, +5), then walking and fire bursts every 30
# ticks, lives topped up to 5 every 50. A change to those functions regenerates these entries.
LEVEL_TICKS = 3000


def level_run(level: int) -> dict:
    sys.path.insert(0, str(ROOT / "tests" / "oracle"))
    import level_sweep
    keys = level_sweep.key_script(level, LEVEL_TICKS)
    return dict(
        summary=f"level {level} through the warp (level_sweep's script): walk and fire, "
                "lives topped up",
        ticks=SEED, machine="vga", interval=50, max_snapshots=LEVEL_TICKS // 50,
        keys=[(t, code) for t in sorted(keys) for code in keys[t]],
        pokes=level_sweep.poke_script(level, LEVEL_TICKS), quit_after=LEVEL_TICKS + 10,
        # warp's start; 0.69..0.70 is where level_75's overlay frame counts match too (warp
        # holds 0.425..0.7, levels 10/25 0.55..0.70, from 0.705 on the walk keys land a tick off)
        tick_phase=0.695, start_delay=0, time_limit=300)


RUNS.update({f"level_{level:02d}": level_run(level) for level in (10, 25, 50, 75, 99)})
# level 50: a death at ~1058 restarts the wave a tick later in the port (CPU time, DEVIATIONS #20)
RUNS["level_50"]["input_shift"] = [(1059, 1, None)]

# input_shift [(start, ticks, end)]: the port replay's keys from `start` to `end` (exclusive,
# None = the end) come `ticks` later, where the port is a tick behind after a timing-classed
# switch (port_script; the report checks each segment against the timing shift at its start).
#
# Resolved: two_player 1150 (title menu) and 1450 (shots, entities[21]) were input reaching
# both runs at one IRQ while the port was a tick behind: input_shift. Keel's 379b7ab (main_sm
# loops test their exit after frame_run's wait, as the original) removed the play lag in warp
# (400, 425..600), death_continue (800, the continue -> play switch) and two_player (the start
# switch, 1200). two_player 1300 / warp 625 entities[8]+1e: a follower's trail as address vs
# player index, also stale in a reused slot (adapters.gamestate.follower_trail_equal). The
# title llamas' leftover in slots 0..8 (+20 read in play, DEVIATIONS #26): Keel's 7b4ed95
# (attract_end at new_game) keeps it in the port, compare.py no longer skips any of it.
#
# Triage of the real divergences (compare.py's first ones per run and those after them that are
# not consequences): (run, tick, field, owner, status). Printed under each run's report; update
# when a re-run after a fix moves them.
TRIAGE = [
    ("level_50", 1150, "entities[2] hit: the port kills it (type 0101 -> 0002, kills_toward_bonus "
     "and scores move), the original's is alive with shot 0 still flying at x 76h", "Keel",
     "open"),
    ("level_99", 700, "entities[51]+38 overlay.frames: original 12, port 13/11/10 at t-1/t/t+1, "
     "at every tick_phase (entities_animate_all 1000:4374 overlay count)", "Prism / Keel", "open"),
]


def triage(name: str) -> str:
    rows = [r for r in TRIAGE if r[0] == name]
    return "\n".join(["triage (run, tick, field, owner, status):"]
                     + [f"  {r} {t:5d}  {f}  [{o}: {st}]" for r, t, f, o, st in rows]) if rows else ""


# game86's stack: SS = DGROUP + 74Dh paragraphs, SP = 1112h (EXE header), so SS:1000..1111 =
# DS:84D0..85E1, above the LZSS ring buffer at SS:0000 (re-notes §2). The interrupt frames
# there depend on where each IRQ lands, i.e. on host speed: not state, left out of the hash.
GAME_STACK = range(0x84D0, 0x85E2)
# g_page_front/back, g_draw_page (re-notes §8): where game86's frames fall between IRQs, which
# moves with host load. Checked through the pages (shown_page() and its flip rule), not here.
PAGE_BOOKKEEPING = range(0x39D3, 0x39D9)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dgroup_hash(dgroup: bytes) -> str:
    image = bytearray(dgroup)
    image[GAME_STACK.start:GAME_STACK.stop] = bytes(len(GAME_STACK))
    image[PAGE_BOOKKEEPING.start:PAGE_BOOKKEEPING.stop] = bytes(len(PAGE_BOOKKEEPING))
    return sha(bytes(image))


# Pages of mode 0Dh in each plane: the two drawn pages and the clean background copy.
PAGES = (0x0000, 0x2000, 0x4000)
PAGE_BYTES = 40 * 200
# _SPREAD[p][b]: plane p's byte b as 8 pixels (big-endian, leftmost first) holding bit p
_SPREAD = [[sum(((b >> (7 - i)) & 1) << p << (8 * (7 - i)) for i in range(8)) for b in range(256)]
           for p in range(4)]


def rasterise(planes: list[bytes], page: int) -> bytes:
    """Indexed pixels of one page (64000 bytes, row-major, plane p = bit p, MSB leftmost):
    EgaPlanes.rasterise, a byte at a time."""
    p0, p1, p2, p3 = (pl[page:page + PAGE_BYTES] for pl in planes)
    s0, s1, s2, s3 = _SPREAD
    return b"".join((s0[a] | s1[b] | s2[c] | s3[d]).to_bytes(8, "big")
                    for a, b, c, d in zip(p0, p1, p2, p3))


def snapshot_summary(s) -> dict:
    out = {"dgroup": dgroup_hash(s.dgroup), "vram": sha(b"".join(s.vram)) if s.vram else None}
    if s.vram:      # per page, for the port's frame comparison (Lantern, W2)
        out["vram_irq"] = s.vram_irq
        out["pages"] = {f"{page:04X}": sha(rasterise(s.vram, page)) for page in PAGES}
        out["shown"] = f"{s.shown:04X}"
    return out


def summary(result) -> dict:
    return {
        "irq_count": result.irq_count,
        "exit_code": result.exit_code,
        "snapshots": {str(s.irq): snapshot_summary(s) for s in result.snapshots},
    }


CALIBRATION = ("tick_phase", "start_delay", "input_shift")  # the port replay's, not the probe's


def probe_args(run: dict) -> dict:
    return {k: v for k, v in run.items() if k not in ("summary", "time_limit", *CALIBRATION)}


def golden_config(run: dict) -> dict:
    """The reference_runs.json "config": the probe's arguments and the replay calibration
    (visual_check.py / web_parity.py read tick_phase and start_delay from it)."""
    return {k: v for k, v in run.items() if k not in ("summary", "time_limit")}


def port_script(drive: Path, out: Path, input_shift=()) -> list[str]:
    """llamatron_replay --keys/--pokes for a reference run's drive. The keys leave out the q
    presses refrun appends to end the run (no scenario uses q): the port would quit on the
    last snapshot's tick and have no tick after it to check a timing shift against.

    input_shift [(start, ticks, end), ...]: the port's key events from tick `start` up to `end`
    (exclusive; None = the end) come `ticks` later, so that while the port is behind after a
    timing-classed switch (DEVIATIONS #20) input lands at the same game phase as in the
    original. Test side only; compare.input_shift_check holds each segment to a timing shift
    established by its start. A key that itself causes a switch (fire starting a game) is
    left out of the segments: the switch re-establishes the shift from there."""
    import refrun
    size = struct.calcsize(refrun.KEY_FMT)
    data = (drive / "PROBE.KEY").read_bytes()
    records = []
    for i in range(0, len(data), size):
        irq, code, pad = struct.unpack(refrun.KEY_FMT, data[i:i + size])
        if code & 0x7F != refrun.SCAN_Q:
            later = next((t for start, t, end in input_shift
                          if irq >= start and (end is None or irq < end)), 0)
            records.append(struct.pack(refrun.KEY_FMT, irq + later, code, pad))
    out.write_bytes(b"".join(records))
    script = ["--keys", str(out)]
    if (drive / "PROBE.POK").exists():
        script += ["--pokes", str(drive / "PROBE.POK")]
    return script


def replay_args(run: dict) -> list[str]:
    """llamatron_replay flags for the run's clock calibration."""
    return ["--tick-phase", str(run.get("tick_phase", 0)),
            "--start-delay", str(run.get("start_delay", 0))]


@pytest.fixture(scope="module", params=sorted(RUNS))
def reference(request):
    import refrun
    name = request.param
    cfg = probe_args(RUNS[name])
    result = refrun.probe(original=ORIGINAL, workdir=WORKDIR / f"golden-{name}",
                          time_limit=RUNS[name].get("time_limit", 90), **cfg)
    # game86 exits with whatever AL holds at 1000:5f55 (INT 21h/4Ch): 0 for most runs, 8 when
    # q ends the warped levels. It is part of the golden summary, not an error code.
    assert result.exit_type == 0, result
    return name, cfg, result


def test_rasterise_matches_ega_model(reference):
    from llamatron_harness.ega import EgaPlanes
    _, _, result = reference
    snap = next((s for s in result.snapshots if s.vram), None)
    if snap is None:
        pytest.skip("no VRAM in this run")
    model = EgaPlanes()
    for p, plane in enumerate(snap.vram):
        model.planes[p][:len(plane)] = plane
    assert rasterise(snap.vram, 0x2000) == model.rasterise(0x2000)


def test_golden_hashes(reference):
    name, cfg, result = reference
    got = summary(result)
    golden = json.loads(GOLDEN.read_text()) if GOLDEN.exists() else {}
    if os.environ.get("LLAMATRON_GOLDEN_UPDATE") == "1" or name not in golden:
        golden[name] = {"config": golden_config(RUNS[name]), **got}
        GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        GOLDEN.write_text(json.dumps(golden, indent=1, sort_keys=True) + "\n")
        pytest.skip(f"golden entry {name} written to {GOLDEN.relative_to(ROOT)}; commit it")
    want = golden[name]
    # json has lists for tuples; a config edit (calibration included) needs the entry rewritten
    assert want["config"] == json.loads(json.dumps(golden_config(RUNS[name]))), \
        f"RUNS[{name!r}] differs from its golden config: LLAMATRON_GOLDEN_UPDATE=1"
    assert got["irq_count"] == want["irq_count"]
    assert got["exit_code"] == want["exit_code"]
    assert sorted(got["snapshots"], key=int) == sorted(want["snapshots"], key=int)
    unsettled, flips = [], []
    for irq, h in want["snapshots"].items():
        mine = got["snapshots"][irq]
        assert settled(mine) == settled(h), f"snapshot at IRQ {irq} changed"
        if "pages" in h and mine["shown"] != h["shown"]:
            flips.append(irq)
        elif shown_page(mine) != shown_page(h):
            raise AssertionError(f"shown page at IRQ {irq} changed")
        elif mine != h:
            unsettled.append(irq)
    if flips:       # informational: see shown_page()
        print(f"\n{name}: capture on a page flip (shown page differs) at IRQs " + ", ".join(flips))
    if unsettled:   # informational: see settled()
        print(f"\n{name}: hidden page / raw VRAM differ from the golden at IRQs "
              + ", ".join(unsettled))


def settled(snap: dict) -> dict:
    """The parts of a snapshot summary that reproduce run to run: DGROUP, and for VGA the
    capture IRQ and the background page 4000 (what visual_check.py compares). The hidden page
    can be caught mid-draw, and how far the draw got varies with host load: three parallel
    runs of game_over_hiscore agreed with each other but differed from sequential ones in 19
    pixels of the hidden page at IRQ 1250, with DGROUP equal. Its hash and the raw VRAM hash
    are kept, but only reported. The shown page: shown_page()."""
    out = {"dgroup": snap["dgroup"]}
    if "pages" in snap:
        out.update(vram_irq=snap["vram_irq"], background=snap["pages"]["4000"])
    return out


def shown_page(snap: dict) -> tuple | None:
    """The shown page (CRTC start) and its pixels. A capture that lands exactly on a page flip
    sees the old or the new page from run to run (Keel): when the start address differs from
    the golden's, the shown page is only reported; at the same start address its pixels must
    match."""
    if "pages" not in snap:
        return None
    return snap["shown"], snap["pages"].get(snap["shown"])


def test_port_replay_report(reference, tmp_path):
    import compare
    import refrun
    driver = os.environ.get("LLAMATRON_REPLAY")
    if not driver or not Path(driver).exists():
        pytest.skip("llamatron_replay not built (LLAMATRON_REPLAY unset)")
    name, cfg, result = reference
    drive = Path(result.workdir) / "c"
    last = max(s.irq for s in result.snapshots)
    out = tmp_path / f"{name}.lrpl"
    machine = "--vga" if cfg["machine"] == "vga" else "--ega"
    shift = RUNS[name].get("input_shift", ())
    script = port_script(drive, tmp_path / f"{name}.key", shift)
    proc = subprocess.run([driver, "--ticks", str(last + 1), "--every", str(cfg["interval"]),
                           "--around", "1", machine, "--seed", str(cfg["ticks"]),
                           *replay_args(RUNS[name]), *script,
                           "--out", str(out)], capture_output=True, text=True, timeout=300)
    assert proc.returncode == 0, proc.stderr
    port = compare.read_port(out)
    assert port.every == cfg["interval"]
    reference_snaps = {s.irq: s.dgroup for s in result.snapshots}
    diffs = compare.compare(port, reference_snaps)
    # the port ran every tick the reference snapshot: one comparison per reference snapshot
    assert [d.tick for d in diffs] == sorted(reference_snaps)
    text = f"{name}: {RUNS[name]['summary']}\n" + compare.report(diffs, shift)
    if triage(name):
        text += "\n" + triage(name)
    report_path = WORKDIR / f"replay-report-{name}.txt"
    report_path.write_text(text + "\n")
    print(f"\n{name}: {proc.stdout.strip()}\n{text}\n(report: {report_path})")
    if os.environ.get("LLAMATRON_REPLAY_STRICT") == "1":
        assert compare.first_divergence(diffs, shift) is None, text
