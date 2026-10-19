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
RUNS = {
    "walk_vga": dict(
        summary="fire on the title tune starts a game (play from ~175), P1 walks right and up",
        ticks=SEED, machine="vga", interval=50, max_snapshots=14, keys=START + WALK,
        quit_after=720),
    "walk_ega": dict(
        summary="walk_vga on an EGA machine (59.92 Hz frames, no VRAM capture)",
        ticks=SEED, machine="ega", interval=50, max_snapshots=14, keys=START + WALK,
        quit_after=720),
    "attract": dict(
        summary="no input: title tune to ~1100, title menu to ~2600, then the attract cycle "
                "(1500-tick timeout, 1000:013b)",
        ticks=SEED, machine="vga", interval=50, max_snapshots=60, keys=[], quit_after=3000,
        time_limit=150),
    "two_player": dict(
        summary="title menu right x2 = 2 players with the droid (options 3), fire at 1200, "
                "P1 walks right",
        ticks=SEED, machine="vga", interval=50, max_snapshots=38,
        keys=[(1150, RIGHT), (1156, RIGHT_UP), (1170, RIGHT), (1176, RIGHT_UP),
              (1200, SPACE), (1210, SPACE_UP), (1400, RIGHT), (1480, RIGHT_UP)],
        quit_after=1900, time_limit=150),
    "warp": dict(
        summary="walk_vga with wave_result poked to 4 (warp) at 300: banner, level 0 -> 5, "
                "next wave from ~375",
        ticks=SEED, machine="vga", interval=25, max_snapshots=40, keys=START + WALK,
        pokes=[(300, WAVE_RESULT, WAVE_WARP)], quit_after=1000),
    "death_continue": dict(
        summary="lives poked to 1 at 250, idle death ~560, continue prompt (0816), fire at "
                "700 continues with 5 lives",
        ticks=SEED, machine="vga", interval=50, max_snapshots=30, keys=START + press(700, SPACE),
        pokes=[(250, LIVES, 1)], quit_after=1500, time_limit=120),
    "game_over_hiscore": dict(
        summary="lives 1, continues 1 and score 00500000 poked at 250: idle death = game over, "
                "name entry (5b06) from ~590: a, b, then '*' ends it",
        ticks=SEED, machine="vga", interval=25, max_snapshots=50,
        keys=START + press(650, SPACE) + press(680, RIGHT) + press(720, SPACE)
        + press(750, LEFT) + press(780, LEFT) + press(820, SPACE),
        pokes=[(250, LIVES, 1), (250, CONTINUES, 1), (250, SCORE_P1 + 2, 0x0005)],
        quit_after=1300, time_limit=120),
}


# game86's stack: SS = DGROUP + 74Dh paragraphs, SP = 1112h (EXE header), so SS:1000..1111 =
# DS:84D0..85E1, above the LZSS ring buffer at SS:0000 (re-notes §2). The interrupt frames
# there depend on where each IRQ lands, i.e. on host speed: not state, left out of the hash.
GAME_STACK = range(0x84D0, 0x85E2)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dgroup_hash(dgroup: bytes) -> str:
    image = bytearray(dgroup)
    image[GAME_STACK.start:GAME_STACK.stop] = bytes(len(GAME_STACK))
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


def probe_args(run: dict) -> dict:
    return {k: v for k, v in run.items() if k not in ("summary", "time_limit")}


@pytest.fixture(scope="module", params=sorted(RUNS))
def reference(request):
    import refrun
    name = request.param
    cfg = probe_args(RUNS[name])
    result = refrun.probe(original=ORIGINAL, workdir=WORKDIR / f"golden-{name}",
                          time_limit=RUNS[name].get("time_limit", 90), **cfg)
    assert result.exit_type == 0 and result.exit_code == 0, result
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
        config = {k: v for k, v in cfg.items()}
        golden[name] = {"config": config, **got}
        GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        GOLDEN.write_text(json.dumps(golden, indent=1, sort_keys=True) + "\n")
        pytest.skip(f"golden entry {name} written to {GOLDEN.relative_to(ROOT)}; commit it")
    want = golden[name]
    assert got["irq_count"] == want["irq_count"]
    assert got["exit_code"] == want["exit_code"]
    assert sorted(got["snapshots"], key=int) == sorted(want["snapshots"], key=int)
    for irq, h in want["snapshots"].items():
        assert got["snapshots"][irq] == h, f"snapshot at IRQ {irq} changed"


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
    script = ["--keys", str(drive / "PROBE.KEY")]
    if (drive / "PROBE.POK").exists():
        script += ["--pokes", str(drive / "PROBE.POK")]
    proc = subprocess.run([driver, "--ticks", str(last), "--every", str(cfg["interval"]),
                           machine, "--seed", str(cfg["ticks"]), *script,
                           "--out", str(out)], capture_output=True, text=True, timeout=300)
    assert proc.returncode == 0, proc.stderr
    port = compare.read_port(out)
    assert port.every == cfg["interval"]
    reference_snaps = {s.irq: s.dgroup for s in result.snapshots}
    diffs = compare.compare(port, reference_snaps)
    # the port ran every tick the reference snapshot: one comparison per reference snapshot
    assert [d.tick for d in diffs] == sorted(reference_snaps)
    text = f"{name}: {RUNS[name]['summary']}\n" + compare.report(diffs)
    report_path = WORKDIR / f"replay-report-{name}.txt"
    report_path.write_text(text + "\n")
    print(f"\n{name}: {proc.stdout.strip()}\n{text}\n(report: {report_path})")
    if os.environ.get("LLAMATRON_REPLAY_STRICT") == "1":
        assert all(not d.offsets for d in diffs), text
