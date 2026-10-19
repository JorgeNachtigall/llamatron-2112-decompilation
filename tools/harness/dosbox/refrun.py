#!/usr/bin/env python3
"""Scripted DOSBox-X runs of the original game (docs/PLAN.md §3.3), test-only.

Each run works on a fresh copy of the user's original folder (the original is never
modified), writes the chosen llama.inf configuration into the copy, generates a
DOSBox-X config with fixed CPU settings, injects keys with DOSBox-X's AUTOTYPE, and
runs headless (-silent) with a time limit. After the game exits, the batch file
writes a marker file, so callers can tell a clean exit through the launcher from a
hang or a kill.

    tools/harness/dosbox/refrun.py smoke                  # space past the title, q quits
    tools/harness/dosbox/refrun.py run --keys "space" --time-limit 60 --window
    tools/harness/dosbox/refrun.py probe --ticks 0x0012ABCD  # pinned-seed DGROUP snapshot

Requires `dosbox-x` on PATH (brew install dosbox-x); `probe` also needs nasm
(brew install nasm) to assemble probe.asm. Probe outputs (PROBE.DG etc.) are copies of
the original's memory: they stay in the gitignored work directory, never commit them.
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_ORIGINAL = REPO_ROOT / "original"
DEFAULT_WORKDIR = REPO_ROOT / "build-dosbox"
MARKER = "REFRUN.TXT"
PROBE_ASM = Path(__file__).resolve().parent / "probe.asm"
HEADER_FMT = "<4sHHIIBBBB"     # PROBE.HDR, see probe.asm

MACHINES = {"vga": "vgaonly", "ega": "ega"}
VIDEO_CHAR = {"vga": "V", "ega": "E"}


@dataclass
class RunResult:
    returncode: int
    clean_exit: bool          # batch reached the line after llama.exe
    workdir: str
    conf: str


@dataclass
class ProbeResult:
    returncode: int
    ticks: int                # tick count pinned during load + shuffle
    ticks_seen: int           # tick word when the snapshot was taken (should equal ticks)
    snapshot_taken: bool
    dgroup_seg: int           # where DOS loaded DGROUP in this run
    exit_code: int            # game86's exit code (INT 21h/4Dh AL)
    exit_type: int            # 0 normal, 0xFF = EXEC failed
    exec_error: int
    workdir: str
    dgroup: bytes = field(default=b"", repr=False)   # 64 KB snapshot (never commit)


def find_dosbox() -> str | None:
    return shutil.which("dosbox-x")


def assemble_probe(out: Path) -> Path:
    nasm = shutil.which("nasm")
    if not nasm:
        raise FileNotFoundError("nasm not found (brew install nasm)")
    subprocess.run([nasm, "-f", "bin", "-o", str(out), str(PROBE_ASM)], check=True)
    return out


def prepare_drive(original: Path, drive: Path, video: str, flash: str) -> None:
    """Fresh copy of the original files, with llama.inf set to: video V/E, keyboard,
    no sound, 8086 build, flash option Y/P/N (layout: docs/re-notes-game86.md §1)."""
    if drive.exists():
        shutil.rmtree(drive)
    drive.mkdir(parents=True)
    for f in original.iterdir():
        if f.is_file():
            shutil.copy2(f, drive / f.name)
    inf = bytearray(b"V K N 0 N\x00")
    inf[0] = ord(VIDEO_CHAR[video])
    inf[8] = ord(flash)
    (drive / "llama.inf").write_bytes(bytes(inf))


def write_conf(path: Path, drive: Path, machine: str, cycles: int, keys: str,
               wait: float, pause: float, extra: list[str],
               program: list[str] | None = None) -> None:
    lines = [
        "[sdl]", "output=surface",
        "[dosbox]", f"machine={MACHINES[machine]}", "memsize=16",
        "[cpu]", "core=normal", "cputype=386", f"cycles=fixed {cycles}",
        "[sblaster]", "sbtype=none",
        "[speaker]", "pcspeaker=false",
        "[autoexec]",
        f'mount c "{drive}"',
        "c:",
    ]
    if keys:
        lines.append(f"autotype -w {wait} -p {pause} {keys}")
    lines += extra
    lines += program or ["llama.exe"]
    lines += [f"echo exited > c:\\{MARKER}", "exit"]
    path.write_text("\n".join(lines) + "\n")


def run(original: Path = DEFAULT_ORIGINAL, workdir: Path = DEFAULT_WORKDIR,
        machine: str = "vga", flash: str = "N", cycles: int = 20000, keys: str = "",
        wait: float = 3, pause: float = 2, time_limit: int = 60, window: bool = False,
        extra: list[str] | None = None) -> RunResult:
    exe = find_dosbox()
    if not exe:
        raise FileNotFoundError("dosbox-x not found (brew install dosbox-x)")
    workdir.mkdir(parents=True, exist_ok=True)
    drive = workdir / "c"
    prepare_drive(original, drive, machine, flash)
    conf = workdir / "refrun.conf"
    write_conf(conf, drive, machine, cycles, keys, wait, pause, extra or [])
    cmd = [exe, "-defaultconf", "-conf", str(conf), "-nolog", "-fastlaunch",
           "-time-limit", str(time_limit)]
    cmd += ["-nomenu"] if window else ["-silent"]
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                          timeout=time_limit + 30)
    clean = any(p.name.upper() == MARKER for p in drive.iterdir())
    return RunResult(proc.returncode, clean, str(workdir), str(conf))


def probe(ticks: int, original: Path = DEFAULT_ORIGINAL, workdir: Path = DEFAULT_WORKDIR,
          machine: str = "vga", flash: str = "N", cycles: int = 20000,
          time_limit: int = 60) -> ProbeResult:
    """Run game86 under PROBE.COM with the BIOS tick pinned to `ticks` during
    start-up; return the DGROUP snapshot taken right after the RNG shuffle (at the
    first mode-0Dh INT 10h). q is typed until the game quits so the probe can
    write its files."""
    exe = find_dosbox()
    if not exe:
        raise FileNotFoundError("dosbox-x not found (brew install dosbox-x)")
    workdir.mkdir(parents=True, exist_ok=True)
    drive = workdir / "c"
    prepare_drive(original, drive, machine, flash)
    assemble_probe(drive / "PROBE.COM")
    vga = 1 if machine == "vga" else 0
    flash_arg = {"N": 0, "P": 1, "Y": 2}[flash]
    conf = workdir / "probe.conf"
    write_conf(conf, drive, machine, cycles, " ".join(["q"] * 8), 3, 2, [],
               program=[f"probe.com {ticks & 0xFFFFFFFF:08X} {vga} {flash_arg}"])
    proc = subprocess.run([exe, "-defaultconf", "-conf", str(conf), "-nolog", "-fastlaunch",
                           "-silent", "-time-limit", str(time_limit)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                          timeout=time_limit + 30)
    files = {p.name.upper(): p for p in drive.iterdir()}
    if "PROBE.HDR" not in files:
        raise RuntimeError(f"probe wrote no PROBE.HDR (dosbox-x rc {proc.returncode}); "
                           f"see {conf}")
    magic, version, seg, pinned, seen, taken, code, etype, err = struct.unpack(
        HEADER_FMT, files["PROBE.HDR"].read_bytes()[:struct.calcsize(HEADER_FMT)])
    if magic != b"LTPB" or version != 1:
        raise RuntimeError("bad PROBE.HDR")
    dgroup = files["PROBE.DG"].read_bytes() if "PROBE.DG" in files else b""
    return ProbeResult(proc.returncode, pinned, seen, bool(taken), seg, code, etype, err,
                       str(workdir), dgroup)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["smoke", "run", "probe"])
    ap.add_argument("--ticks", type=lambda v: int(v, 0), default=0x0012ABCD,
                    help="probe: BIOS tick count to pin (RNG seed)")
    ap.add_argument("--original", type=Path, default=DEFAULT_ORIGINAL)
    ap.add_argument("--workdir", type=Path, default=None)
    ap.add_argument("--machine", choices=sorted(MACHINES), default="vga")
    ap.add_argument("--flash", choices=["Y", "P", "N"], default="N")
    ap.add_argument("--cycles", type=int, default=20000)
    ap.add_argument("--keys", default="", help="AUTOTYPE key names, e.g. 'space q'")
    ap.add_argument("--wait", type=float, default=3)
    ap.add_argument("--pause", type=float, default=2)
    ap.add_argument("--time-limit", type=int, default=60)
    ap.add_argument("--window", action="store_true", help="show the window (not -silent)")
    args = ap.parse_args(argv)

    if args.action == "probe":
        res = probe(args.ticks, args.original, args.workdir or DEFAULT_WORKDIR / "probe",
                    args.machine, args.flash, args.cycles, args.time_limit)
        print(json.dumps(asdict(res) | {"dgroup": f"{len(res.dgroup)} bytes"}, indent=2))
        return 0 if res.snapshot_taken and res.exit_type == 0 else 1

    keys = args.keys
    if args.action == "smoke":
        # space leaves the fs.exe title picture; q is repeated until game86 has
        # installed its keyboard handler (1000:e567) and quits (1e7b:013f -> 5ed9).
        keys = keys or "space " + " ".join(["q"] * 6)
    workdir = args.workdir or DEFAULT_WORKDIR / f"{args.action}-{args.machine}"
    res = run(args.original, workdir, args.machine, args.flash, args.cycles, keys,
              args.wait, args.pause, args.time_limit, args.window)
    print(json.dumps(asdict(res), indent=2))
    if args.action == "smoke":
        return 0 if res.clean_exit else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
