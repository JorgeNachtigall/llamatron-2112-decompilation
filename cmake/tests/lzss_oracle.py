"""The extractor's LZSS decoder (tools/extract/lzss.c, `llamatron-extract --lzss`) against the
original's, run in the unicorn harness (tools/harness, test oracle only):

  cmp_load 1000:e65e        open DS:DX, read 0x400 bytes into DS:0000, decode into ES:DI, close;
                            a failed open or read prints DOS message DS:0463 and exits (5f5a)
  lzss_decode 1000:e6bb     ring at SS:0000 (only 0x000..0xFED zeroed), write position 0xFEE
  lzss_getbyte 1000:e73d    1 KB input window at DS:0000, refilled by INT 21h/3Fh
  lzss_ungetbyte 1000:e778  push-back after a 00 00 pair not followed by 00
  data_load 1000:60aa       g3.cmp -> cs_g3_seg:0000, f.cmp -> DS:39EE, s.cmp -> DS:4141, then
                            scores.yak (read 0xA1 bytes, kept only if exactly 0xA0 came) at DS:40A0,
                            else the built-in table DS:0AD2

Cases: random valid streams (literals, matches across the ring's wrap, the 00 00 <byte> copy,
inputs longer than one window), and the user's g3/f/s.cmp, used only if their sha256 matches
the manifest (and read at run time; nothing proprietary is stored). For the user's files the
ring's undefined tail (SS:0FEE..0FFF) is filled with a marker: their output must not depend
on it, as the extractor's ring starts zeroed.

usage: lzss_oracle.py EXTRACT GAME86_IMAGE ORIGINAL_DIR MANIFEST WORK_DIR
Exit 77 = skipped (no image or harness).
"""

import hashlib
import json
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

CMP_LOAD, DATA_LOAD, EXIT_WITH_MESSAGE = "1000:e65e", "1000:60aa", "1000:5f5a"
CS_SAVED_DS, CS_G3_SEG = 0x0255, 0x0261   # 1000:0255 (set at 1000:0040), 1000:0261
NAME_G3, NAME_F, NAME_S, NAME_SCORES = 0x0436, 0x0430, 0x042A, 0x0408
F_AT, S_AT, SCORES_AT, DEFAULT_SCORES, SCORES_SIZE = 0x39EE, 0x4141, 0x40A0, 0x0AD2, 0xA0
MSG_FILE_ERROR = 0x0463
RING_TAIL, RING_TAIL_SIZE = 0xFEE, 0x12
OUT_SEG = 0x6000
CARRY = 0x0001
CASES = 300


class Dos:
    """INT 21h open (3Dh), read (3Fh), close (3Eh) over in-memory files."""

    def __init__(self, files: dict[str, bytes]):
        self.files = {k.lower(): v for k, v in files.items()}
        self.open: dict[int, list] = {}
        self.next_handle = 5

    def __call__(self, m, _intno):
        r = m.get_regs()
        ah, ok, ax = r["ax"] >> 8, True, 0
        if ah == 0x3D:
            raw = m.read(r["ds"], r["dx"], 64)
            name = raw[:raw.index(0)].decode("ascii").lower()
            if name in self.files:
                ax, self.next_handle = self.next_handle, self.next_handle + 1
                self.open[ax] = [self.files[name], 0]
            else:
                ok, ax = False, 2
        elif ah == 0x3F:
            data, pos = self.open[r["bx"]]
            chunk = data[pos:pos + r["cx"]]
            m.write(r["ds"], r["dx"], chunk)
            self.open[r["bx"]][1] = pos + len(chunk)
            ax = len(chunk)
        elif ah == 0x3E:
            del self.open[r["bx"]]
        else:
            raise AssertionError(f"INT 21h AH={ah:02x}h")
        m.uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
        flags = m.uc.reg_read(X.UC_X86_REG_EFLAGS)
        m.uc.reg_write(X.UC_X86_REG_EFLAGS, flags & ~CARRY if ok else flags | CARRY)


def prepare(m, tail: bytes = bytes(RING_TAIL_SIZE)) -> None:
    m.reset()
    m.write_u16(0x1000, CS_SAVED_DS, m.dgroup)
    m.write_u16(0x1000, CS_G3_SEG, OUT_SEG)
    m.write(m.image.stack_ss, RING_TAIL, tail)


def original_decode(m, stream: bytes, tail: bytes = bytes(RING_TAIL_SIZE)) -> bytes:
    prepare(m, tail)
    result = m.call(CMP_LOAD, regs={"dx": NAME_G3, "es": OUT_SEG, "di": 0},
                    interrupts={0x21: Dos({"g3.cmp": stream})}, max_insns=20_000_000)
    return m.read(OUT_SEG, 0, result.regs["di"])


def extractor_decode(extract: Path, work: Path, stream: bytes) -> bytes | None:
    src, dst = work / "in.cmp", work / "out.bin"
    src.write_bytes(stream)
    dst.unlink(missing_ok=True)
    run = subprocess.run([str(extract), "--lzss", str(src), str(dst)], capture_output=True, text=True)
    return dst.read_bytes() if run.returncode == 0 else None


def random_stream(rnd: random.Random) -> bytes:
    """A valid stream: flag bytes, literals, matches (00 00 <non-zero> included), terminator."""
    items: list[bytes] = []
    for _ in range(rnd.choice((rnd.randrange(1, 40), rnd.randrange(200, 2500)))):
        kind = rnd.random()
        if kind < 0.45:
            items.append(bytes([rnd.choice((0, rnd.randrange(256)))]))
        elif kind < 0.5:
            items.append(b"\x00\x00")          # copy 3 from ring 0; the next byte is not 0
        else:
            pos, length = rnd.randrange(RING_TAIL), rnd.randrange(16)
            if rnd.random() < 0.3:
                pos = rnd.choice((0, RING_TAIL - 3, 0xFF0 - length))
            items.append(bytes([pos & 0xFF, (pos >> 8) << 4 | length]))
    # A 00 00 pair is a copy only if the next input byte is not 0: put a literal right after
    # it (its bit then makes the next flag byte non-zero, should the pair end a group).
    fixed: list[bytes] = []
    for item in items:
        fixed.append(item)
        if item == b"\x00\x00":
            fixed.append(bytes([rnd.randrange(1, 256)]))
    fixed.append(b"\x00\x00")                 # terminator pair, then a 0 byte
    out = bytearray()
    for first in range(0, len(fixed), 8):
        group = fixed[first:first + 8]
        out.append(sum(1 << i for i, it in enumerate(group) if len(it) == 1))
        for it in group:
            out.extend(it)
    out.append(0)
    return bytes(out)


def check_random(m, extract: Path, work: Path) -> int:
    rnd = random.Random(0x1AA5)
    failures = 0
    for case in range(CASES):
        stream = random_stream(rnd)
        want = extractor_decode(extract, work, stream)
        got = original_decode(m, stream)
        if want != got:
            failures += 1
            print(f"case {case}: {len(stream)} input bytes: extractor "
                  f"{'failed' if want is None else f'{len(want)} bytes'}, original {len(got)} bytes")
    print(f"random streams: {CASES - failures}/{CASES} equal")
    return failures


def user_files(original: Path, manifest: Path) -> dict[str, bytes]:
    wanted = {f["name"].lower(): f["sha256"] for f in json.loads(manifest.read_text())["files"]}
    found = {}
    for path in original.iterdir() if original.is_dir() else []:
        name = path.name.lower()
        if name in ("g3.cmp", "f.cmp", "s.cmp", "scores.yak"):
            data = path.read_bytes()
            if name == "scores.yak" or hashlib.sha256(data).hexdigest() == wanted.get(name):
                found[name] = data
    return found


def check_user_files(m, extract: Path, work: Path, files: dict[str, bytes]) -> int:
    failures = 0
    for name in ("g3.cmp", "f.cmp", "s.cmp"):
        if name not in files:
            print(f"{name}: skipped (missing or not the supported release)")
            continue
        want = extractor_decode(extract, work, files[name])
        got = original_decode(m, files[name], tail=b"\xA5" * RING_TAIL_SIZE)
        same = want == got
        failures += not same
        print(f"{name}: {len(got)} bytes, {'equal' if same else 'DIFFERENT'}")
    return failures


def check_open_failure(m) -> int:
    prepare(m)
    m.run_until(CMP_LOAD, EXIT_WITH_MESSAGE, regs={"dx": NAME_G3, "es": OUT_SEG},
                interrupts={0x21: Dos({})})
    ok = m.get_regs()["dx"] == MSG_FILE_ERROR
    print(f"missing file: exit with DS:{m.get_regs()['dx']:04X} ({'ok' if ok else 'expected 0463'})")
    return not ok


def check_data_load(m, extract: Path, work: Path, files: dict[str, bytes]) -> int:
    if not all(n in files for n in ("g3.cmp", "f.cmp", "s.cmp")):
        print("data_load: skipped (needs the user's g3/f/s.cmp)")
        return 0
    decoded = {n: extractor_decode(extract, work, files[n]) for n in ("g3.cmp", "f.cmp", "s.cmp")}
    if None in decoded.values():
        print("data_load: the extractor cannot decode the user's files")
        return 1
    rnd = random.Random(0x60AA)
    table = bytes(rnd.randrange(256) for _ in range(SCORES_SIZE + 0x60))
    failures = 0
    for size in (None, 0, SCORES_SIZE - 1, SCORES_SIZE, SCORES_SIZE + 1, SCORES_SIZE + 0x60):
        prepare(m)
        defaults = m.read_dgroup(DEFAULT_SCORES, SCORES_SIZE)
        dos_files = {n: files[n] for n in ("g3.cmp", "f.cmp", "s.cmp")}
        if size is not None:
            dos_files["scores.yak"] = table[:size]
        m.call(DATA_LOAD, interrupts={0x21: Dos(dos_files)}, max_insns=50_000_000)
        expect_scores = table[:SCORES_SIZE] if size == SCORES_SIZE else defaults
        checks = {
            "g3": m.read(OUT_SEG, 0, len(decoded["g3.cmp"])) == decoded["g3.cmp"],
            "f": m.read_dgroup(F_AT, len(decoded["f.cmp"])) == decoded["f.cmp"],
            "s": m.read_dgroup(S_AT, len(decoded["s.cmp"])) == decoded["s.cmp"],
            "scores": m.read_dgroup(SCORES_AT, SCORES_SIZE) == expect_scores,
        }
        bad = [k for k, v in checks.items() if not v]
        failures += bool(bad)
        print(f"data_load, scores.yak {'missing' if size is None else f'{size} bytes'}: "
              f"{'ok' if not bad else 'DIFFERENT ' + ', '.join(bad)}")
    return failures


def main() -> int:
    if len(sys.argv) != 6:
        print(__doc__)
        return 2
    extract, image, original, manifest, work = map(Path, sys.argv[1:])
    if not image.exists():
        print(f"skipped: no game86 image at {image}")
        return SKIP
    work.mkdir(parents=True, exist_ok=True)
    m = Machine(load_image(image))
    files = user_files(original, manifest)
    failures = (check_random(m, extract, work) + check_user_files(m, extract, work, files) +
                check_open_failure(m) + check_data_load(m, extract, work, files))
    print("lzss oracle:", "all checks passed" if failures == 0 else f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
