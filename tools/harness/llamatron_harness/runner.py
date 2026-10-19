"""Run single routines of the original game86.exe in unicorn (16-bit real mode).

Test-oracle code only (docs/PLAN.md §0.1, §3.2): the port never executes original
code; this module exists so tests can compare our C reimplementation with what the
original routine does to memory.

Typical use:

    m = Machine.from_game86()                 # load + relocate, DS = DGROUP
    m.write_dgroup(0x39E9, b"\\x05\\x00")
    res = m.call("1000:e4de")                 # run until the routine's RET
    res.regs["bx"], res.dgroup_diff           # outputs

A call pushes a sentinel return address and stops when execution reaches it with
the stack back at its initial depth, so nested calls and returns are fine.
Interrupts and port I/O are intercepted: INTs go to Python handlers (unhandled
ones abort the run), port reads come from `io_read`, all port writes are logged.
"""

from __future__ import annotations

import os
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable

from unicorn import (UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR,
                     UC_HOOK_MEM_INVALID, UC_MODE_16, UC_PROT_ALL, Uc, UcError)
from unicorn import x86_const as X

from .ega import EgaPlanes
from .mzimage import PARAGRAPH, LoadedImage, load_image

REPO_ROOT = Path(__file__).resolve().parents[3]

MEM_SIZE = 0x110000           # 1 MB + HMA so A20-wrapping accesses stay mapped
VRAM_BASE, VRAM_SIZE = 0xA0000, 0x10000   # A000 window -> EGA plane model (mmio)
BDA_SEG = 0x0040
BDA_TICKS = 0x006C            # BIOS tick count, the game's RNG seed source
FAR_SENTINEL = (0xF000, 0xFFF0)
NEAR_SENTINEL_IP = 0xFFFE     # sentinel offset inside the routine's own CS
DEFAULT_MAX_INSNS = 5_000_000

REGS16 = {
    "ax": X.UC_X86_REG_AX, "bx": X.UC_X86_REG_BX, "cx": X.UC_X86_REG_CX,
    "dx": X.UC_X86_REG_DX, "si": X.UC_X86_REG_SI, "di": X.UC_X86_REG_DI,
    "bp": X.UC_X86_REG_BP, "sp": X.UC_X86_REG_SP, "ip": X.UC_X86_REG_IP,
    "cs": X.UC_X86_REG_CS, "ds": X.UC_X86_REG_DS, "es": X.UC_X86_REG_ES,
    "ss": X.UC_X86_REG_SS, "flags": X.UC_X86_REG_EFLAGS,
}
# Writing order matters: segment bases must be set before IP/SP are interpreted.
_WRITE_ORDER = ["cs", "ds", "es", "ss", "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
                "flags", "ip"]


def linear(seg: int, off: int) -> int:
    return ((seg & 0xFFFF) << 4) + (off & 0xFFFF)


def parse_addr(addr: str | tuple[int, int]) -> tuple[int, int]:
    """'1000:e4de' or (0x1000, 0xe4de) -> (seg, off)."""
    if isinstance(addr, tuple):
        return addr
    seg, off = addr.split(":")
    return int(seg, 16), int(off, 16)


def default_game86_path() -> Path:
    """Image used by default: $LLAMATRON_GAME86_IMAGE, else the extractor output,
    else the local RE copy in unpacked/."""
    env = os.environ.get("LLAMATRON_GAME86_IMAGE")
    if env:
        if not Path(env).exists():
            raise FileNotFoundError(f"LLAMATRON_GAME86_IMAGE={env} does not exist")
        return Path(env)
    for cand in (REPO_ROOT / "build/generated/exe/game86.unpacked",
                 REPO_ROOT / "unpacked/game86.exe"):
        if cand.exists():
            return cand
    raise FileNotFoundError("no game86 image: set LLAMATRON_GAME86_IMAGE or build the extractor")


class HarnessError(RuntimeError):
    pass


@dataclass
class DiffRange:
    """A run of changed bytes at linear address `start`."""
    start: int
    old: bytes
    new: bytes

    def __repr__(self) -> str:
        return (f"DiffRange({self.start:05x}, {len(self.new)} bytes: "
                f"{self.old[:16].hex()} -> {self.new[:16].hex()})")


@dataclass
class IntEvent:
    number: int
    regs: dict[str, int]


@dataclass
class PortEvent:
    direction: str          # "in" | "out"
    port: int
    size: int
    value: int


@dataclass
class CallResult:
    entry: tuple[int, int]
    regs_in: dict[str, int]
    regs: dict[str, int]                     # registers at the sentinel
    memory_diff: list[DiffRange]             # whole 1 MB, minus free stack below SP
    dgroup_diff: dict[int, tuple[int, int]]  # DGROUP offset -> (old byte, new byte)
    interrupts: list[IntEvent] = field(default_factory=list)
    ports: list[PortEvent] = field(default_factory=list)
    stack_scratch: list[DiffRange] = field(default_factory=list)  # writes below SP
    vram_in: list[bytes] = field(default_factory=list)   # 4 planes before the call
    vram: list[bytes] = field(default_factory=list)      # 4 planes after the call
    vram_changed: list[int] = field(default_factory=list)  # VRAM offsets changed in any plane

    def changed_dgroup_offsets(self) -> list[int]:
        return sorted(self.dgroup_diff)


IntHandler = Callable[["Machine", int], None]   # (machine, intno); edits regs/memory in place


class DefaultPorts:
    """Port reads when a test supplies nothing better.  3DAh (input status 1)
    alternates display/retrace on every read so both halves of the vsync wait
    (game86 1000:e5f3) terminate; everything else floats high."""

    def __init__(self):
        self._retrace = False

    def __call__(self, port: int, size: int) -> int:
        if port == 0x3DA:
            self._retrace = not self._retrace
            return 0x09 if self._retrace else 0x00
        return 0xFF if size == 1 else 0xFFFF


def bios_int10(m: "Machine", _intno: int) -> None:
    """Minimal INT 10h for tests that opt in (interrupts={0x10: bios_int10}):
    AH=00h with AL=0Dh resets the EGA model; everything else is only logged."""
    ax = m.uc.reg_read(X.UC_X86_REG_AX)
    if ax >> 8 == 0x00 and ax & 0x7F == 0x0D:
        m.ega.set_mode_0d()


class Machine:
    """A 1 MB real-mode address space holding the relocated game image.
    The A000 window is the EGA plane model `self.ega`; access it through that
    object (read/write/snapshot here only cover ordinary RAM)."""

    def __init__(self, image: LoadedImage, dgroup: int | None = None):
        self.image = image
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.ega = EgaPlanes()
        self.uc.mem_map(0, VRAM_BASE, UC_PROT_ALL)
        self.uc.mmio_map(VRAM_BASE, VRAM_SIZE, self.ega.mmio_read, None,
                         self.ega.mmio_write, None)
        self.uc.mem_map(VRAM_BASE + VRAM_SIZE, MEM_SIZE - VRAM_BASE - VRAM_SIZE, UC_PROT_ALL)
        self.dgroup = dgroup if dgroup is not None else self._detect_dgroup()
        self.psp_seg = image.load_seg - 0x10
        self.interrupts: dict[int, IntHandler] = {}
        self.io_read: Callable[[int, int], int] = DefaultPorts()
        self._int_log: list[IntEvent] = []
        self._port_log: list[PortEvent] = []
        self._fault: Exception | None = None
        self._stop_lin: int | None = None
        self._stop_sp: tuple[int, int] | None = None
        self._install_hooks()
        self.reset()

    @classmethod
    def from_game86(cls, path: str | Path | None = None) -> "Machine":
        return cls(load_image(path or default_game86_path()))

    # ---- memory image -------------------------------------------------------

    def _detect_dgroup(self) -> int:
        """game86 entry starts with MOV DX,<DGROUP> (relocated). game86 1000:0010"""
        lin = linear(self.image.entry_cs, self.image.entry_ip) - self.image.base
        code = self.image.data[lin:lin + 3]
        if len(code) == 3 and code[0] == 0xBA:
            return struct.unpack_from("<H", code, 1)[0]
        raise HarnessError("cannot detect DGROUP from entry code; pass dgroup= explicitly")

    def reset(self) -> None:
        """Fresh memory: zeroed RAM, cleared mode-0Dh VRAM, image at its load
        segment, minimal PSP."""
        self.uc.mem_write(0, bytes(VRAM_BASE))
        self.uc.mem_write(VRAM_BASE + VRAM_SIZE, bytes(MEM_SIZE - VRAM_BASE - VRAM_SIZE))
        self.ega.set_mode_0d()
        self.uc.mem_write(self.image.base, self.image.data)
        psp = bytearray(256)
        psp[0:2] = b"\xCD\x20"                        # INT 20h
        struct.pack_into("<H", psp, 2, self.image.end // PARAGRAPH)
        psp[0x80] = 0
        psp[0x81] = 0x0D
        self.uc.mem_write(linear(self.psp_seg, 0), bytes(psp))

    def read(self, seg: int, off: int, n: int) -> bytes:
        return bytes(self.uc.mem_read(linear(seg, off), n))

    def write(self, seg: int, off: int, data: bytes) -> None:
        self.uc.mem_write(linear(seg, off), bytes(data))

    def read_u16(self, seg: int, off: int) -> int:
        return struct.unpack("<H", self.read(seg, off, 2))[0]

    def write_u16(self, seg: int, off: int, value: int) -> None:
        self.write(seg, off, struct.pack("<H", value & 0xFFFF))

    def read_dgroup(self, off: int, n: int) -> bytes:
        return self.read(self.dgroup, off, n)

    def write_dgroup(self, off: int, data: bytes) -> None:
        self.write(self.dgroup, off, data)

    def set_bios_ticks(self, ticks: int) -> None:
        self.write(BDA_SEG, BDA_TICKS, struct.pack("<I", ticks & 0xFFFFFFFF))

    def snapshot(self) -> bytes:
        """All RAM as one MEM_SIZE buffer; the VRAM window reads as zeros (reading
        it through the bus would load the latches)."""
        return (bytes(self.uc.mem_read(0, VRAM_BASE)) + bytes(VRAM_SIZE) +
                bytes(self.uc.mem_read(VRAM_BASE + VRAM_SIZE,
                                       MEM_SIZE - VRAM_BASE - VRAM_SIZE)))

    # ---- registers ----------------------------------------------------------

    def get_regs(self) -> dict[str, int]:
        regs = {name: self.uc.reg_read(rid) & 0xFFFF for name, rid in REGS16.items()}
        regs["flags"] = self.uc.reg_read(X.UC_X86_REG_EFLAGS) & 0xFFFF
        return regs

    def set_regs(self, regs: dict[str, int]) -> None:
        for name in _WRITE_ORDER:
            if name in regs:
                self.uc.reg_write(REGS16[name], regs[name] & 0xFFFF)

    # ---- hooks --------------------------------------------------------------

    def _install_hooks(self) -> None:
        self.uc.hook_add(UC_HOOK_INTR, self._on_interrupt)
        self.uc.hook_add(UC_HOOK_INSN, self._on_in, None, 1, 0, X.UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._on_out, None, 1, 0, X.UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_bad_mem)

    def _on_interrupt(self, uc: Uc, intno: int, _user) -> None:
        self._int_log.append(IntEvent(intno, self.get_regs()))
        handler = self.interrupts.get(intno)
        if handler is None:
            regs = self.get_regs()
            self._fault = HarnessError(
                f"unhandled INT {intno:02x}h (AX={regs['ax']:04x}) at "
                f"{regs['cs']:04x}:{(regs['ip'] - 2) & 0xFFFF:04x}")
            uc.emu_stop()
            return
        handler(self, intno)

    def _on_in(self, uc: Uc, port: int, size: int, _user) -> int:
        value = self.ega.port_in(port) if size == 1 else None
        if value is None:
            value = self.io_read(port, size)
        self._port_log.append(PortEvent("in", port, size, value))
        return value

    def _on_out(self, uc: Uc, port: int, size: int, value: int, _user) -> None:
        self._port_log.append(PortEvent("out", port, size, value))
        self.ega.port_out(port, value, size)

    def _on_bad_mem(self, uc: Uc, access, address, size, value, _user) -> bool:
        self._fault = HarnessError(f"invalid memory access at {address:#x}")
        return False

    def _on_sentinel(self, uc: Uc, address: int, size: int, _user) -> None:
        sp = (uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP))
        if address == self._stop_lin and self._stop_sp in (None, sp):
            uc.emu_stop()

    # ---- running a routine --------------------------------------------------

    def call(self, entry: str | tuple[int, int], regs: dict[str, int] | None = None,
             far: bool = False, max_insns: int = DEFAULT_MAX_INSNS,
             interrupts: dict[int, IntHandler] | None = None) -> CallResult:
        """Run the routine at `entry` until its (near or far) RET.

        Unspecified registers default to DS=ES=DGROUP, SS:SP = the image's stack,
        everything else 0.  Memory is whatever the machine holds now (set it up
        with write*/set_bios_ticks first).
        """
        seg, off = parse_addr(entry)
        full = self._initial_regs(seg, off, regs)
        ss, sp = full["ss"], full["sp"]
        if far:
            sp = (sp - 4) & 0xFFFF
            self.write(ss, sp, struct.pack("<HH", FAR_SENTINEL[1], FAR_SENTINEL[0]))
            stop_lin = linear(*FAR_SENTINEL)
        else:
            sp = (sp - 2) & 0xFFFF
            self.write_u16(ss, sp, NEAR_SENTINEL_IP)
            stop_lin = linear(seg, NEAR_SENTINEL_IP)
        stop_sp = (ss, full["sp"])
        full["sp"] = sp
        return self._execute(full, stop_lin, stop_sp, max_insns, interrupts)

    def run_until(self, start: str | tuple[int, int], stop: str | tuple[int, int],
                  regs: dict[str, int] | None = None, max_insns: int = DEFAULT_MAX_INSNS,
                  interrupts: dict[int, IntHandler] | None = None) -> CallResult:
        """Run straight-line code from `start` until execution first reaches `stop`
        (for inline sequences that are not separate routines, e.g. init loops)."""
        seg, off = parse_addr(start)
        full = self._initial_regs(seg, off, regs)
        return self._execute(full, linear(*parse_addr(stop)), None, max_insns, interrupts)

    def _initial_regs(self, seg: int, off: int, regs: dict[str, int] | None) -> dict[str, int]:
        full = {"ax": 0, "bx": 0, "cx": 0, "dx": 0, "si": 0, "di": 0, "bp": 0,
                "ds": self.dgroup, "es": self.dgroup, "ss": self.image.stack_ss,
                "sp": self.image.stack_sp, "flags": 0x0202}
        full.update(regs or {})
        full["cs"], full["ip"] = seg, off
        return full

    def _execute(self, full: dict[str, int], stop_lin: int, stop_sp: tuple[int, int] | None,
                 max_insns: int, interrupts: dict[int, IntHandler] | None) -> CallResult:
        seg, off = full["cs"], full["ip"]
        # Everything below the caller's SP is free stack: the routine's pushes and
        # locals (and our sentinel) land there and are not state.  game86's stack
        # (SS 3CA6) lies inside the DGROUP window, so this must be filtered out.
        top_sp = stop_sp[1] if stop_sp else full["sp"]
        scratch = (linear(full["ss"], 0), top_sp)
        self._stop_lin, self._stop_sp = stop_lin, stop_sp
        saved_ints = self.interrupts
        if interrupts is not None:
            self.interrupts = {**self.interrupts, **interrupts}
        self._int_log, self._port_log, self._fault = [], [], None

        before = self.snapshot()
        vram_in = self.ega.snapshot()
        self.set_regs(full)
        regs_in = self.get_regs()
        hook = self.uc.hook_add(UC_HOOK_CODE, self._on_sentinel, None,
                                self._stop_lin, self._stop_lin)
        try:
            self.uc.emu_start(linear(seg, off), 0xFFFFFFFF, count=max_insns)
        except UcError as exc:
            regs = self.get_regs()
            raise HarnessError(f"emulation fault {exc} at {regs['cs']:04x}:{regs['ip']:04x}") from exc
        finally:
            self.uc.hook_del(hook)
            self.interrupts = saved_ints
        if self._fault:
            raise self._fault
        regs = self.get_regs()
        if linear(regs["cs"], regs["ip"]) != self._stop_lin:
            raise HarnessError(f"routine {seg:04x}:{off:04x} did not return within "
                               f"{max_insns} instructions (at {regs['cs']:04x}:{regs['ip']:04x})")
        after = self.snapshot()
        vram = self.ega.snapshot()
        full_diff = diff_memory(before, after)
        diff = _drop_range(full_diff, *scratch)
        return CallResult(entry=(seg, off), regs_in=regs_in, regs=regs, memory_diff=diff,
                          dgroup_diff=dgroup_view(diff, self.dgroup),
                          stack_scratch=_keep_range(full_diff, *scratch),
                          interrupts=self._int_log, ports=self._port_log,
                          vram_in=vram_in, vram=vram, vram_changed=_vram_changed(vram_in, vram))


# ---- diffs ------------------------------------------------------------------

def _vram_changed(before: list[bytes], after: list[bytes]) -> list[int]:
    changed: set[int] = set()
    for b, a in zip(before, after):
        if b != a:
            changed.update(diff.start + i for diff in diff_memory(b, a)
                           for i in range(len(diff.new)))
    return sorted(changed)


def diff_memory(before: bytes, after: bytes, chunk: int = 256) -> list[DiffRange]:
    """Changed byte runs between two equally sized snapshots."""
    out: list[DiffRange] = []
    run_start = None
    for base in range(0, len(before), chunk):
        if before[base:base + chunk] == after[base:base + chunk]:
            if run_start is not None:
                out.append(DiffRange(run_start, before[run_start:base], after[run_start:base]))
                run_start = None
            continue
        for i in range(base, min(base + chunk, len(before))):
            if before[i] != after[i]:
                if run_start is None:
                    run_start = i
            elif run_start is not None:
                out.append(DiffRange(run_start, before[run_start:i], after[run_start:i]))
                run_start = None
    if run_start is not None:
        out.append(DiffRange(run_start, before[run_start:], after[run_start:]))
    return out


def _filter_range(diff: Iterable[DiffRange], start: int, length: int,
                  inside: bool) -> list[DiffRange]:
    out = []
    for d in diff:
        if inside or d.start < start + length and d.start + len(d.new) > start:
            for i, (o, n) in enumerate(zip(d.old, d.new)):
                if (start <= d.start + i < start + length) == inside:
                    out.append((d.start + i, o, n))
        else:
            out.extend((d.start + i, o, n) for i, (o, n) in enumerate(zip(d.old, d.new)))
    return _bytes_to_ranges(out)


def _drop_range(diff: Iterable[DiffRange], start: int, length: int) -> list[DiffRange]:
    return _filter_range(diff, start, length, inside=False)


def _keep_range(diff: Iterable[DiffRange], start: int, length: int) -> list[DiffRange]:
    return _filter_range(diff, start, length, inside=True)


def _bytes_to_ranges(items: list[tuple[int, int, int]]) -> list[DiffRange]:
    ranges: list[DiffRange] = []
    for addr, o, n in items:
        if ranges and ranges[-1].start + len(ranges[-1].new) == addr:
            last = ranges[-1]
            ranges[-1] = DiffRange(last.start, last.old + bytes([o]), last.new + bytes([n]))
        else:
            ranges.append(DiffRange(addr, bytes([o]), bytes([n])))
    return ranges


def dgroup_view(diff: Iterable[DiffRange], dgroup: int) -> dict[int, tuple[int, int]]:
    """Restrict a memory diff to the 64 KB DGROUP window, keyed by DS offset."""
    base = dgroup * PARAGRAPH
    out = {}
    for d in diff:
        for i, (o, n) in enumerate(zip(d.old, d.new)):
            off = d.start + i - base
            if 0 <= off < 0x10000:
                out[off] = (o, n)
    return out
