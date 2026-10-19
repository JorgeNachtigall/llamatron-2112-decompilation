"""fs.exe (the front screen) in unicorn, for the W2 title tests (test-only, docs/PLAN.md §0.1).

Runs fs.exe's own show_title (1000:01bf): its ILBM loader and decoder, palette code and VRAM
writes, with
  * INT 21h file I/O (open 3Dh, set DTA 1Ah, find first 4Eh, read 3Fh, close 3Eh) served from
    the user's original files, each checked against tools/manifest/original-files.json first;
  * INT 10h mode sets (13h VGA, 10h EGA) resetting the plane model, and the palette calls
    (AX=1012h DAC block, AX=1002h EGA registers) recorded;
  * the A000 window as the harness's plane model: with map-mask writes it holds VGA mode X
    (320x400, 80 bytes per row, pixel x in plane x & 3) as well as EGA mode 10h (640x350).
Nothing read here is written into the repository.
"""

from __future__ import annotations

import hashlib
import json
import os
import struct
from dataclasses import dataclass, field
from pathlib import Path

from unicorn import x86_const as X

from llamatron_harness import Machine
from llamatron_harness.mzimage import load_image

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools" / "manifest" / "original-files.json"

CODE0 = 0x1000
CS_SAVED_DS = 0x00BC                 # fs 1000:0052 stores DS here
CS_VGA_FLAG = 0x00DD                 # PSP:86h copy (1000:0082)
SHOW_TITLE = "1000:01bf"
CARRY = 0x0001

VGA_WIDTH, VGA_HEIGHT, MODEX_STRIDE = 320, 400, 80
EGA_WIDTH, EGA_HEIGHT, EGA_STRIDE = 640, 350, 80


def fs_image_path() -> Path | None:
    env = os.environ.get("LLAMATRON_FS_IMAGE")
    if env:
        return Path(env)
    cands = []
    game86 = os.environ.get("LLAMATRON_GAME86_IMAGE")
    if game86:
        cands.append(Path(game86).with_name("fs.unpacked"))
    cands += [ROOT / "build/generated/exe/fs.unpacked", ROOT / "unpacked/fs.exe"]
    return next((c for c in cands if c.exists()), None)


def original_dir() -> Path:
    return Path(os.environ.get("LLAMATRON_ORIGINAL_DIR") or ROOT / "original")


def verified_originals(directory: Path) -> dict[str, bytes]:
    """name (upper case) -> contents, for every manifest file present with the right hash."""
    files = {}
    for entry in json.loads(MANIFEST.read_text())["files"]:
        path = directory / entry["name"]
        if not path.exists():
            continue
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"{path} does not match the manifest")
        files[entry["name"].upper()] = data
    return files


@dataclass
class FsRun:
    vga: bool
    machine: Machine
    dac_calls: list[tuple[int, list[tuple[int, int, int]]]] = field(default_factory=list)
    ega_calls: list[bytes] = field(default_factory=list)
    opened: list[str] = field(default_factory=list)

    def pixels(self) -> bytes:
        ega = self.machine.ega
        assert ega.start_address == 0, f"display start {ega.start_address:#x}"
        if not self.vga:
            return ega.rasterise(0, EGA_WIDTH, EGA_HEIGHT, EGA_STRIDE)
        planes = ega.planes
        out = bytearray(VGA_WIDTH * VGA_HEIGHT)
        for y in range(VGA_HEIGHT):
            row = y * MODEX_STRIDE
            for x in range(VGA_WIDTH):
                out[y * VGA_WIDTH + x] = planes[x & 3][row + (x >> 2)]
        return bytes(out)

    def dac(self) -> list[tuple[int, int, int]]:
        """The 256 DAC entries (6-bit) as the card holds them at the end."""
        dac = [(0, 0, 0)] * 256
        for first, colours in self.dac_calls:
            for i, c in enumerate(colours):
                dac[first + i] = c
        return dac


class DosFiles:
    """The few INT 21h services fs.exe's loader uses, over in-memory files."""

    def __init__(self, files: dict[str, bytes], run: FsRun):
        self.files, self.run = files, run
        self.handles: dict[int, list] = {}
        self.dta = (0, 0)

    def __call__(self, m: Machine, _intno: int) -> None:
        r = m.get_regs()
        ah = r["ax"] >> 8
        ok, ax = True, r["ax"]
        if ah == 0x3D:                                   # open
            name = self._name(m, r["ds"], r["dx"])
            self.run.opened.append(name)
            if name in self.files:
                handle = 5 + len(self.handles)
                self.handles[handle] = [self.files[name], 0]
                ax = handle
            else:
                ok, ax = False, 2
        elif ah == 0x1A:                                 # set DTA
            self.dta = (r["ds"], r["dx"])
        elif ah == 0x4E:                                 # find first: size at DTA+1Ah
            name = self._name(m, r["ds"], r["dx"])
            if name in self.files:
                m.write(self.dta[0], self.dta[1] + 0x1A, struct.pack("<I", len(self.files[name])))
                ax = 0
            else:
                ok, ax = False, 2
        elif ah == 0x3F:                                 # read
            data, pos = self.handles[r["bx"]]
            chunk = data[pos:pos + r["cx"]]
            m.write(r["ds"], r["dx"], chunk)
            self.handles[r["bx"]][1] = pos + len(chunk)
            ax = len(chunk)
        elif ah == 0x3E:                                 # close
            self.handles.pop(r["bx"], None)
        else:
            raise AssertionError(f"fs.exe called INT 21h AH={ah:02x}h")
        m.uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
        flags = m.uc.reg_read(X.UC_X86_REG_EFLAGS)
        m.uc.reg_write(X.UC_X86_REG_EFLAGS, flags & ~CARRY if ok else flags | CARRY)

    @staticmethod
    def _name(m: Machine, seg: int, off: int) -> str:
        raw = m.read(seg, off, 64)
        return raw[:raw.index(0)].decode("ascii").upper()


def video_bios(run: FsRun):
    def int10(m: Machine, _intno: int) -> None:
        r = m.get_regs()
        if r["ax"] in (0x0013, 0x0010):
            m.ega.set_mode_0d()                          # registers reset, VRAM cleared
        elif r["ax"] == 0x1012:
            raw = m.read(r["es"], r["dx"], 3 * r["cx"])
            run.dac_calls.append((r["bx"], [tuple(b & 0x3F for b in raw[3 * i:3 * i + 3])
                                            for i in range(r["cx"])]))
        elif r["ax"] == 0x1002:
            run.ega_calls.append(m.read(r["es"], r["dx"], 17))
        else:
            raise AssertionError(f"fs.exe called INT 10h AX={r['ax']:04x}h")
    return int10


def run_show_title(image: Path, files: dict[str, bytes], vga: bool) -> FsRun:
    machine = Machine(load_image(image))
    run = FsRun(vga, machine)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write(CODE0, CS_VGA_FLAG, bytes([1 if vga else 0]))
    machine.call(SHOW_TITLE, max_insns=400_000_000,
                 interrupts={0x21: DosFiles(files, run), 0x10: video_bios(run)})
    return run
