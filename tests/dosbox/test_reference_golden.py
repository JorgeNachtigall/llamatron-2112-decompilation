"""Golden reference runs and the port's replay of them (PLAN §3.3).

One scripted DOSBox-X run (pinned RNG tick, PROBE.KEY input, DGROUP/VRAM snapshots every 50
IRQs) is checked against the hashes in tests/golden/reference_runs.json (hashes only: the
snapshots come from the original's memory and are never committed). Then the port runs the
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
RUNS = {
    # Fire on the title starts a game, then P1 walks right and up (as test_probe_replay).
    "walk_vga": dict(ticks=0x0012ABCD, machine="vga", interval=50, max_snapshots=14,
                     keys=[(150, SPACE), (160, SPACE_UP), (400, RIGHT), (480, RIGHT_UP),
                           (500, UP), (560, UP_UP)],
                     quit_after=720),
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def summary(result) -> dict:
    return {
        "irq_count": result.irq_count,
        "exit_code": result.exit_code,
        "snapshots": {str(s.irq): {"dgroup": sha(s.dgroup),
                                   "vram": sha(b"".join(s.vram)) if s.vram else None}
                      for s in result.snapshots},
    }


@pytest.fixture(scope="module", params=sorted(RUNS))
def reference(request):
    import refrun
    name = request.param
    cfg = RUNS[name]
    result = refrun.probe(original=ORIGINAL, workdir=WORKDIR / f"golden-{name}", time_limit=90,
                          **cfg)
    assert result.exit_type == 0 and result.exit_code == 0, result
    return name, cfg, result


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
    keys = Path(result.workdir) / "c" / "PROBE.KEY"
    last = max(s.irq for s in result.snapshots)
    out = tmp_path / f"{name}.lrpl"
    machine = "--vga" if cfg["machine"] == "vga" else "--ega"
    proc = subprocess.run([driver, "--ticks", str(last), "--every", str(cfg["interval"]),
                           machine, "--seed", str(cfg["ticks"]), "--keys", str(keys),
                           "--out", str(out)], capture_output=True, text=True, timeout=300)
    assert proc.returncode == 0, proc.stderr
    port = compare.read_port(out)
    assert port.every == cfg["interval"]
    reference_snaps = {s.irq: s.dgroup for s in result.snapshots}
    diffs = compare.compare(port, reference_snaps)
    # the port ran every tick the reference snapshot: one comparison per reference snapshot
    assert [d.tick for d in diffs] == sorted(reference_snaps)
    text = compare.report(diffs)
    report_path = WORKDIR / f"replay-report-{name}.txt"
    report_path.write_text(text + "\n")
    print(f"\n{name}: {proc.stdout.strip()}\n{text}\n(report: {report_path})")
    if os.environ.get("LLAMATRON_REPLAY_STRICT") == "1":
        assert all(not d.offsets for d in diffs), text
