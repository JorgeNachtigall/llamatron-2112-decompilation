"""llama.inf as the launcher reads it and the installer writes it, the original run in the
unicorn harness (test oracle only):

  extractor  llama.exe inf_read 1000:01e7 (open, read 9 bytes to DS:0185, parse 0219..02c8)
             against `llamatron-extract --llama-inf` (data/config.dat's record, dgroup.c)
  parse      the same against settings_parse_inf (src/platform/settings.c, settings_inf_probe)
  format     install.exe write_config 1000:050f (video / control / sound menu results at
             106A:04EF / 0648 / 0726 -> the 10 bytes written to LLAMA.INF; in install mode
             ([0177] = 0) also LLAMA.BAT / INSTALL.BAT) against settings_format_inf

The launcher upper-cases [0], [2], [4], [8] (AND 0DFh) and rejects the file unless [0] is V/E,
[2] J/K and [4] A/S/R/N ("No or Invalid Game Configuration found", 1060:0110, then exit). It
keeps cpu = [6] - '0' (1000:0221), vga (CS:0087), joystick (CS:0089), sound type (CS:008A) and
flash mode (CS:0086: P = 1, Y = 2, else 0). After an accepted A/S/R it goes on to detect the
card (02c8 detect_adlib); 'N' returns at 02c2. A shorter file leaves the rest of the 9-byte buffer
as it was (zeros): the port requires 9 bytes, so files under 9 bytes are only reported.

usage: launcher_oracle.py EXTRACT EXE_DIR WORK_DIR [--probe PROBE] [--check extractor,parse,format]
EXE_DIR holds the extractor's llama.unpacked / install.unpacked. Exit 77 = skipped.
"""

import argparse
import random
import subprocess
import sys
from pathlib import Path

SKIP = 77
REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/harness"))
try:
    from unicorn import x86_const as X
    from llamatron_harness import Machine, load_image
except ImportError as exc:
    print(f"skipped: harness not importable ({exc})")
    sys.exit(SKIP)

INF_READ = "1000:01e7"
LAUNCHER_ERROR, DETECT_CARD = 0x01C3, 0x02C8   # patched to RET so inf_read returns to us
MSG_INVALID = 0x0110
CS_CPU, CS_FLASH, CS_VGA, CS_JOYSTICK, CS_SOUND = 0x85, 0x86, 0x87, 0x89, 0x8A
INF_SIZE = 9
CASES = 400
WRITE_CONFIG, INSTALL_MODE = "1000:050f", 0x0177
VIDEO_CHOICE, CONTROL_CHOICE, SOUND_CHOICE = 0x04EF, 0x0648, 0x0726
SOUND_BLASTER, SOUND_NONE = 0x0C, 0xFF
CARRY = 0x0001
RET = b"\xC3"


class Dos:
    """INT 21h: open (3Dh) / read (3Fh) / close (3Eh) llama.inf, print (09h) recorded."""

    def __init__(self, inf: bytes | None):
        self.inf, self.printed = inf, []

    def __call__(self, m, _intno):
        r = m.get_regs()
        ah, ok, ax = r["ax"] >> 8, True, r["ax"]
        if ah == 0x3D:
            raw = m.read(r["ds"], r["dx"], 16)
            assert raw[:raw.index(0)].lower() == b"llama.inf"
            ok, ax = (False, 2) if self.inf is None else (True, 5)
        elif ah == 0x3F:
            chunk = self.inf[:r["cx"]]
            m.write(r["ds"], r["dx"], chunk)
            ax = len(chunk)
        elif ah == 0x3E:
            pass
        elif ah == 0x09:
            self.printed.append(r["dx"])
        else:
            raise AssertionError(f"inf_read called INT 21h AH={ah:02x}h")
        m.uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
        flags = m.uc.reg_read(X.UC_X86_REG_EFLAGS)
        m.uc.reg_write(X.UC_X86_REG_EFLAGS, flags & ~CARRY if ok else flags | CARRY)


def original(m, inf: bytes | None) -> tuple:
    """(accepted, vga, joystick, sound, cpu, flash) as llama.exe leaves them."""
    m.reset()
    m.write(0x1000, LAUNCHER_ERROR, RET)
    m.write(0x1000, DETECT_CARD, RET)
    dos = Dos(inf)
    m.call(INF_READ, interrupts={0x21: dos})
    if dos.printed:
        assert dos.printed == [MSG_INVALID], dos.printed
        return (0,)
    cs = m.read(0x1000, 0, 0x90)
    return (1, cs[CS_VGA], cs[CS_JOYSTICK], cs[CS_SOUND], cs[CS_CPU], cs[CS_FLASH])


def extractor(extract: Path, work: Path, inf: bytes | None) -> tuple:
    src, dst = work / "llama.inf", work / "config.rec"
    if inf is None:
        src.unlink(missing_ok=True)
        rec = None
    else:
        src.write_bytes(inf)
        subprocess.run([str(extract), "--llama-inf", str(src), str(dst)], check=True, capture_output=True)
        rec = dst.read_bytes()
    if rec is None or rec[6] == 0:
        return (0,)
    return (1, rec[7], rec[8], rec[9], rec[10], rec[11])


def settings(probe: Path, cases: list[bytes | None]) -> list[tuple]:
    text = "".join((inf or b"").hex() + "\n" for inf in cases)
    out = subprocess.run([str(probe)], input=text, capture_output=True, text=True, check=True).stdout
    rows = [tuple(int(v) for v in line.split()) for line in out.splitlines()]
    return [(0,) if inf is None or not row[0] else row for inf, row in zip(cases, rows)]


def random_inf(rnd: random.Random) -> bytes:
    def letter(valid: bytes) -> int:
        roll = rnd.random()
        if roll < 0.75:
            c = rnd.choice(valid)
            return c | 0x20 if rnd.random() < 0.3 else c
        return rnd.randrange(256) if roll < 0.9 else rnd.choice(b"XQZ@\x00 ")
    inf = bytearray(b"V K N 0 N\x00")
    inf[0], inf[2], inf[4] = letter(b"VE"), letter(b"JK"), letter(b"ASRN")
    inf[6] = rnd.choice(b"0123") if rnd.random() < 0.8 else rnd.randrange(256)
    inf[8] = letter(b"YPN")
    for i in (1, 3, 5, 7):
        if rnd.random() < 0.1:
            inf[i] = rnd.randrange(256)
    return bytes(inf[:rnd.choice((9, 10, 10, 10, 12))])


class DosCreate:
    """INT 21h for write_config: create (3Ch) / write (40h) / close (3Eh), recorded per file."""

    def __init__(self):
        self.files: dict[str, bytes] = {}
        self.handles: dict[int, str] = {}

    def __call__(self, m, _intno):
        r = m.get_regs()
        ah, ax = r["ax"] >> 8, r["ax"]
        if ah == 0x3C:
            raw = m.read(r["ds"], r["dx"], 32)
            ax = 5 + len(self.handles)
            self.handles[ax] = raw[:raw.index(0)].decode("ascii")
            self.files[self.handles[ax]] = b""
        elif ah == 0x40:
            self.files[self.handles[r["bx"]]] += m.read(r["ds"], r["dx"], r["cx"])
            ax = r["cx"]
        elif ah != 0x3E:
            raise AssertionError(f"write_config called INT 21h AH={ah:02x}h")
        m.uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
        m.uc.reg_write(X.UC_X86_REG_EFLAGS, m.uc.reg_read(X.UC_X86_REG_EFLAGS) & ~CARRY)


def check_format(m, probe: Path) -> int:
    """Every menu combination, in both modes: LLAMA.INF as settings_format_inf writes it."""
    combos = [(video, control, sound, mode) for video in range(6) for control in range(2)
              for sound in range(2) for mode in (0, 1)]
    requests = []
    for video, control, sound, _mode in combos:
        flash = (2, 1, 0)[video % 3]                 # Normal / no flashes / no flashes or pulsing
        requests.append(f"F {int(video < 3)} {int(control == 0)} "
                        f"{SOUND_BLASTER if sound == 0 else SOUND_NONE} 0 {flash}\n")
    out = subprocess.run([str(probe)], input="".join(requests), capture_output=True, text=True,
                         check=True).stdout.split()
    failures = 0
    for (video, control, sound, mode), want_hex in zip(combos, out):
        m.reset()
        for at, value in ((VIDEO_CHOICE, video), (CONTROL_CHOICE, control), (SOUND_CHOICE, sound),
                          (INSTALL_MODE, mode)):
            m.write_dgroup(at, bytes([value]))
        dos = DosCreate()
        m.call(WRITE_CONFIG, regs={"es": 0xB800}, interrupts={0x21: dos})
        written = dos.files.get("@:LLAMA.INF")
        expect_files = {"@:LLAMA.INF"} | ({"@:..\\LLAMA.BAT", "@:..\\INSTALL.BAT"} if mode == 0 else set())
        if written != bytes.fromhex(want_hex) or set(dos.files) != expect_files:
            failures += 1
            print(f"  menus {video}/{control}/{sound} mode {mode}: install.exe {written!r} "
                  f"{sorted(dos.files)}, settings_format_inf {bytes.fromhex(want_hex)!r}")
    print(f"format: {len(combos) - failures}/{len(combos)} as install.exe")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("extract", type=Path)
    ap.add_argument("exe_dir", type=Path)
    ap.add_argument("work", type=Path)
    ap.add_argument("--probe", type=Path)
    ap.add_argument("--check", default="extractor")
    args = ap.parse_args()
    checks = set(args.check.split(","))
    if checks & {"parse", "format"} and args.probe is None:
        ap.error("parse/format need --probe")
    llama, install = args.exe_dir / "llama.unpacked", args.exe_dir / "install.unpacked"
    if not llama.exists() or ("format" in checks and not install.exists()):
        print(f"skipped: no llama/install image in {args.exe_dir}")
        return SKIP
    args.work.mkdir(parents=True, exist_ok=True)
    failures = 0
    if checks & {"extractor", "parse"}:
        m = Machine(load_image(llama))
        rnd = random.Random(0x01E7)
        cases: list[bytes | None] = [None, b"V K N 0 N\x00", b"E J S 0 Y\x00", b"v j a 1 p\x00", b"V K X 0 N\x00"]
        cases += [random_inf(rnd) for _ in range(CASES)]
        want = [original(m, inf) for inf in cases]
        sides = {}
        if "extractor" in checks:
            sides["extractor"] = [extractor(args.extract, args.work, inf) for inf in cases]
        if "parse" in checks:
            sides["parse"] = settings(args.probe, cases)
        for name, got in sides.items():
            bad = [i for i in range(len(cases)) if got[i] != want[i]]
            failures += len(bad)
            print(f"{name}: {len(cases) - len(bad)}/{len(cases)} as llama.exe")
            for i in bad[:8]:
                print(f"  {cases[i]!r}: llama.exe {want[i]}, {name} {got[i]}")
        for short in (b"V K N", b"E J S 0"):         # reported only (see above)
            print(f"short file {short!r}: llama.exe {original(m, short)}, "
                  f"extractor {extractor(args.extract, args.work, short)}")
    if "format" in checks:
        failures += check_format(Machine(load_image(install)), args.probe)
    print("launcher oracle:", "all checks passed" if failures == 0 else f"{failures} differences")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
