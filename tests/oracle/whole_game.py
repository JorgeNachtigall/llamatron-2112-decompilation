"""Whole-game oracle: game86 runs as a full program in unicorn, in lockstep with the port.

Both sides follow the port's own clock (llamatron_replay's sequencing, src/engine/timing.c):
the original's start-up up to 1000:00c2 is game_init; then, per display frame, the timer
ticks that are due (each: the script's key bytes through the INT 09 handler 1000:e567, then
the INT 08 handler 1000:3de4) and one game_frame, which for the original is everything up
to its next wait for the vertical retrace (wait_vsync 1000:e5f3, where main_sm_frame also
returns, src/game/main_sm.h). The ticks are raised while the original sits at the entry of
that wait, so with the same schedule on both sides there is no phase to calibrate: after
every tick and every frame the DGROUP bytes the adapter maps must match.

The original's surroundings, all inside this file:
  - DOS (INT 21h): memory resize, get/set vector (into the real IVT), and read-only access to
    the user's original files (g3.cmp, f.cmp, s.cmp, scores.yak; $LLAMATRON_ORIGINAL_DIR, else
    original/); nothing is written.
  - BIOS: INT 10h mode 0Dh resets the EGA plane model (palette calls are only logged);
    INT 16h has no keys; the tick word 0040:006C holds the seed and stands still.
  - The command tail as the launcher passes it: VGA or EGA, keyboard, no sound (PSP:89h = FF
    selects game86's own dummy driver), flash off.
  - INT 08 / INT 09's previous vectors are an IRET, so the BIOS chaining at 1000:3ec2 returns.

Usage (from tests/, with the testlib built):
  LLAMATRON_GAME_TESTLIB=<build>/tests/libllamatron_game_testlib.dylib \\
      ../tools/harness/.venv/bin/python oracle/whole_game.py walk_vga
  ... oracle/whole_game.py sweep --levels 0-99:5 [--ticks N]
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

TESTS = Path(__file__).resolve().parents[1]
REPO = TESTS.parent
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(REPO / "tools" / "harness"))

import unicorn  # noqa: E402
import unicorn.x86_const as X  # noqa: E402

from adapters.cport import bind, load_testlib  # noqa: E402
from adapters.gamestate import ATTRACT_LLAMAS, DGROUP_SIZE, MASK_UNMAPPED, DgroupMap  # noqa: E402
from llamatron_harness import Machine  # noqa: E402
from llamatron_harness.runner import linear  # noqa: E402

# ---- the original's entry points and DGROUP words ------------------------------------------

GAME_INIT_END = (0x1000, 0x00C2)     # main_sm_start: the flow from video_init on
WAIT_VSYNC = (0x1000, 0xE5F3)        # every frame ends at the entry of this wait
WAIT_VSYNC_END = 0xE605              # 3DAh reads outside e5f3..e604 are another wait
INT_TIMER, INT_KEYBOARD = 0x08, 0x09
STACK = range(0x84D0, 0x85E2)        # game86's stack (interrupt frames), not state
KEY_PORT = 0x60
DRAW_HOOK, TICK_HOOK = 0x39E0, 0x39E4    # [39E0] frame draw hook, [39E4] tick hook
DRAW_HOOK_TITLE = 0x402C
ENTITY_TABLE, ENTITY_SIZE = 0x48C1, 0x40
# Known differences, as tests/replay/compare.py treats them:
# DEVIATIONS #10: the loaded image has trail_head[1] = 2 until P2's first trail write; the
# port starts at entry 0. Skipped while the original still holds 2.
P2_TRAIL_HEAD, P2_TRAIL_HEAD_LOADED = 0x2CF9, 2
# Title llama leftovers in play: fields +02..+3f of slots 0..8 that entities_clear (4307)
# leaves behind and the original never reads before writing (tests/dosbox/llama_residue.py);
# skipped while the original keeps the value it had when play started and the port holds 0.
LEFTOVER_FIELDS = range(0x02, 0x40)

STUB_SEG = 0x9F00                    # our IRET for the chained BIOS vectors
DOS_PATH_MAX = 128
MAX_INSNS_PER_RUN = 20_000_000

# The port's clock (src/engine/timing.c).
PIT_CRYSTAL_NUM, PIT_CRYSTAL_DEN, PIT_GAME_DIVISOR = 315_000_000, 264, 0x5CEC
DISPLAYS = {"vga": (25_175_000, 800 * 449), "ega": (315_000_000, 912 * 262 * 22)}


class Divergence(Exception):
    pass


class OriginalFault(Exception):
    pass


# ---- the tick schedule ---------------------------------------------------------------------

class Clock:
    """timing_init_display + timing_set_phase + timing_advance_frame / timing_take_tick, in
    the same integer units."""

    def __init__(self, machine: str, phase: int = 0):
        from math import gcd
        dot_clock, self.dots_per_frame = DISPLAYS[machine]
        unit_scale = PIT_CRYSTAL_NUM
        tick_cost = PIT_GAME_DIVISOR * PIT_CRYSTAL_DEN * dot_clock
        g = gcd(unit_scale, tick_cost)
        self.unit_scale, self.tick_cost = unit_scale // g, tick_cost // g
        self.pending = 0
        within = (phase * self.tick_cost + 65535) // 65536
        self.pending = self.pending - self.pending % self.tick_cost + within

    def frame_ticks(self) -> int:
        """Advance one retrace; the number of ticks then due."""
        self.pending += self.dots_per_frame * self.unit_scale
        n = self.pending // self.tick_cost
        self.pending -= n * self.tick_cost
        return n


# ---- the original --------------------------------------------------------------------------

class OriginalGame:
    def __init__(self, game86: str | Path | None, original_dir: Path, seed: int,
                 machine: str = "vga"):
        self.m = Machine.from_game86(game86)
        self.uc = self.m.uc
        self.dir = original_dir
        self.files: dict[int, bytes] = {}
        self.positions: dict[int, int] = {}
        self.next_handle = 5
        self.key_byte: int | None = None
        self.stray_waits: list[tuple[int, int]] = []
        self.mode = "run"
        self.resume_skip = False
        self.int_return: tuple[int, int, int] | None = None
        m = self.m
        m.interrupts = {0x10: self._int10, 0x16: self._int16, 0x21: self._int21,
                        0x2F: lambda mm, n: None}
        base_io = m.io_read

        def io_read(port: int, size: int) -> int:
            if port == KEY_PORT and self.key_byte is not None:
                return self.key_byte
            if port == 0x3DA:
                ip = self.uc.reg_read(X.UC_X86_REG_IP)
                cs = self.uc.reg_read(X.UC_X86_REG_CS)
                if not (cs == WAIT_VSYNC[0] and WAIT_VSYNC[1] <= ip < WAIT_VSYNC_END):
                    self.stray_waits.append((cs, ip))
            return base_io(port, size)
        m.io_read = io_read

        # PSP command tail as llama.exe passes it (docs/re-notes-game86.md §1).
        tail = bytes([0x6C, 0x6D, 0x0D, 0, 0, 1 if machine == "vga" else 0, 0, 0, 0xFF])
        m.write(m.psp_seg, 0x80, bytes([len(tail)]) + tail + b"\r")
        m.set_bios_ticks(seed)
        m.write(STUB_SEG, 0, b"\xCF")                       # IRET
        for vec in (INT_TIMER, INT_KEYBOARD, 0x1C):
            m.write(0, 4 * vec, struct.pack("<HH", 0, STUB_SEG))
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self._at_wait, None,
                         linear(*WAIT_VSYNC), linear(*WAIT_VSYNC))
        self._stop_at = None
        self.stop_hook = None

        self._set_regs(cs=m.image.entry_cs, ip=m.image.entry_ip, ds=m.psp_seg, es=m.psp_seg,
                       ss=m.image.stack_ss, sp=m.image.stack_sp)
        self._run_to(linear(*GAME_INIT_END))

    # -- running

    def _set_regs(self, **regs) -> None:
        self.m.set_regs(regs)

    def _pc(self) -> int:
        return linear(self.uc.reg_read(X.UC_X86_REG_CS), self.uc.reg_read(X.UC_X86_REG_IP))

    def _go(self) -> None:
        self.m._fault = None
        try:
            self.uc.emu_start(self._pc(), 0xFFFFFFFF, count=MAX_INSNS_PER_RUN)
        except unicorn.UcError as exc:
            r = self.m.get_regs()
            raise OriginalFault(f"emulation fault {exc} at {r['cs']:04x}:{r['ip']:04x}") from exc
        if self.m._fault:
            raise OriginalFault(str(self.m._fault))

    def _run_to(self, lin: int) -> None:
        def stop(uc, address, size, _u):
            uc.emu_stop()
        h = self.uc.hook_add(unicorn.UC_HOOK_CODE, stop, None, lin, lin)
        try:
            self._go()
        finally:
            self.uc.hook_del(h)
        if self._pc() != lin:
            r = self.m.get_regs()
            raise OriginalFault(f"did not reach {lin:#x} (at {r['cs']:04x}:{r['ip']:04x})")

    def _at_wait(self, uc, address, size, _u) -> None:
        if self.mode == "frame":
            if self.resume_skip:
                self.resume_skip = False
                return
            uc.emu_stop()
        elif self.mode == "interrupt":
            sp = uc.reg_read(X.UC_X86_REG_SP)
            if self.int_return and sp == self.int_return[2]:
                uc.emu_stop()

    def frame(self) -> None:
        """Everything up to the next entry of wait_vsync."""
        self.mode = "frame"
        self.resume_skip = self._pc() == linear(*WAIT_VSYNC)
        try:
            self._go()
        finally:
            self.mode = "run"
        if self._pc() != linear(*WAIT_VSYNC):
            r = self.m.get_regs()
            raise OriginalFault(f"frame did not reach the retrace wait (at {r['cs']:04x}:{r['ip']:04x})")

    def raise_interrupt(self, vec: int) -> None:
        """A hardware interrupt at the current instruction (the entry of wait_vsync): FLAGS,
        CS, IP pushed, IF cleared, run until the handler's IRET is back here."""
        uc = self.uc
        cs, ip = uc.reg_read(X.UC_X86_REG_CS), uc.reg_read(X.UC_X86_REG_IP)
        assert linear(cs, ip) == linear(*WAIT_VSYNC), "interrupts are raised at the retrace wait"
        ss, sp = uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP)
        flags = uc.reg_read(X.UC_X86_REG_EFLAGS) & 0xFFFF
        sp2 = (sp - 6) & 0xFFFF
        self.m.write(ss, sp2, struct.pack("<HHH", ip, cs, flags))
        off, seg = struct.unpack("<HH", self.m.read(0, 4 * vec, 4))
        uc.reg_write(X.UC_X86_REG_SP, sp2)
        uc.reg_write(X.UC_X86_REG_EFLAGS, flags & ~0x0300)
        self._set_regs(cs=seg, ip=off)
        self.int_return = (cs, ip, sp)
        self.mode = "interrupt"
        try:
            self._go()
        finally:
            self.mode = "run"
            self.int_return = None
        if self._pc() != linear(cs, ip) or uc.reg_read(X.UC_X86_REG_SP) != sp:
            r = self.m.get_regs()
            raise OriginalFault(f"INT {vec:02x}h did not return (at {r['cs']:04x}:{r['ip']:04x})")

    def key(self, scan_code: int) -> None:
        self.key_byte = scan_code
        try:
            self.raise_interrupt(INT_KEYBOARD)
        finally:
            self.key_byte = None

    def tick(self) -> None:
        self.raise_interrupt(INT_TIMER)

    def dgroup(self) -> bytes:
        return self.m.read_dgroup(0, DGROUP_SIZE)

    @property
    def ega(self):
        """The original's EGA/VGA plane model (llamatron_harness EgaPlanes)."""
        return self.m.ega

    def poke(self, offset: int, value: int) -> None:
        self.m.write_dgroup(offset, struct.pack("<H", value & 0xFFFF))

    # -- DOS and BIOS

    def _flags(self, carry: bool | None = None, zero: bool | None = None) -> None:
        f = self.uc.reg_read(X.UC_X86_REG_EFLAGS)
        if carry is not None:
            f = (f | 1) if carry else (f & ~1)
        if zero is not None:
            f = (f | 0x40) if zero else (f & ~0x40)
        self.uc.reg_write(X.UC_X86_REG_EFLAGS, f)

    def _int10(self, m: Machine, _n: int) -> None:
        ax = self.uc.reg_read(X.UC_X86_REG_AX)
        if ax >> 8 == 0x00 and ax & 0x7F == 0x0D:
            m.ega.set_mode_0d()
        elif ax >> 8 == 0x0F:
            self.uc.reg_write(X.UC_X86_REG_AX, 0x280D)

    def _int16(self, m: Machine, _n: int) -> None:
        ah = self.uc.reg_read(X.UC_X86_REG_AX) >> 8
        if ah in (0x01, 0x11):
            self._flags(zero=True)
        elif ah in (0x00, 0x10):
            raise_fault(m, "INT 16h read with no key")

    def _c_string(self, seg: int, off: int) -> str:
        raw = self.m.read(seg, off, DOS_PATH_MAX)
        return raw.split(b"\0", 1)[0].decode("ascii")

    def _int21(self, m: Machine, _n: int) -> None:
        r = m.get_regs()
        ah, al = r["ax"] >> 8, r["ax"] & 0xFF
        if ah == 0x4A:                                  # resize memory block
            self._flags(carry=False)
        elif ah == 0x0B or (ah == 0x0C and al in (0, 0x0B)):    # input status: no key
            m.set_regs({"ax": r["ax"] & 0xFF00})
        elif ah == 0x0C and al in (0x06, 0x07, 0x08) or ah in (0x06, 0x07, 0x08):
            raise_fault(m, f"INT 21h AH={ah:02x} reads a key at {r['cs']:04x}:{(r['ip'] - 2) & 0xFFFF:04x}")
        elif ah == 0x35:                                # get vector
            off, seg = struct.unpack("<HH", m.read(0, 4 * al, 4))
            m.set_regs({"es": seg, "bx": off})
        elif ah == 0x25:                                # set vector
            m.write(0, 4 * al, struct.pack("<HH", r["dx"], r["ds"]))
        elif ah == 0x3D:                                # open (read-only)
            name = self._c_string(r["ds"], r["dx"])
            path = self.dir / Path(name.replace("\\", "/")).name.lower()
            if al & 3 or not path.is_file():
                m.set_regs({"ax": 2})
                self._flags(carry=True)
                return
            h = self.next_handle
            self.next_handle += 1
            self.files[h], self.positions[h] = path.read_bytes(), 0
            m.set_regs({"ax": h})
            self._flags(carry=False)
        elif ah == 0x3F:                                # read
            h = r["bx"]
            if h not in self.files:
                m.set_regs({"ax": 6})
                self._flags(carry=True)
                return
            pos = self.positions[h]
            data = self.files[h][pos:pos + r["cx"]]
            m.write(r["ds"], r["dx"], data)
            self.positions[h] = pos + len(data)
            m.set_regs({"ax": len(data)})
            self._flags(carry=False)
        elif ah == 0x3E:                                # close
            self.files.pop(r["bx"], None)
            self._flags(carry=False)
        elif ah == 0x42:                                # seek
            h = r["bx"]
            dist = (r["cx"] << 16) | r["dx"]
            base = {0: 0, 1: self.positions.get(h, 0), 2: len(self.files.get(h, b""))}[al]
            pos = (base + dist) & 0xFFFFFFFF
            self.positions[h] = pos
            m.set_regs({"ax": pos & 0xFFFF, "dx": pos >> 16})
            self._flags(carry=False)
        elif ah == 0x4C:                                # exit
            self.exit_code = al
            self.uc.emu_stop()
            raise_fault(m, f"game86 exited with code {al}")
        else:
            raise_fault(m, f"unhandled INT 21h AH={ah:02x} at {r['cs']:04x}:{(r['ip'] - 2) & 0xFFFF:04x}")


def raise_fault(m: Machine, text: str) -> None:
    m._fault = OriginalFault(text)
    m.uc.emu_stop()


# ---- the port ------------------------------------------------------------------------------

class GameLaunch(ctypes.Structure):
    _fields_ = [("vga", ctypes.c_bool), ("joystick", ctypes.c_bool), ("sound", ctypes.c_bool),
                ("flash_mode", ctypes.c_uint8), ("bios_ticks", ctypes.c_uint32),
                ("shuffle_read", ctypes.c_void_p), ("shuffle_ctx", ctypes.c_void_p),
                ("saved_scores", ctypes.c_void_p), ("save_scores", ctypes.c_void_p),
                ("host_user", ctypes.c_void_p)]


class GameHostInput(ctypes.Structure):
    _fields_ = [("requests", ctypes.c_uint), ("focused", ctypes.c_bool)]


class PortGame:
    """llamatron_replay's set-up (tests/replay/replay_driver.c), in this process."""

    def __init__(self, lib: ctypes.CDLL, seed: int, machine: str = "vga"):
        self.dg = DgroupMap(lib)
        self.state = self.dg.new_state()
        self._tick = bind(lib, "game_tick", None, ctypes.c_void_p)
        self._frame = bind(lib, "game_frame", None, ctypes.c_void_p, ctypes.POINTER(GameHostInput))
        self._key = bind(lib, "input_keyboard_byte", None, ctypes.c_uint8)
        bind(lib, "input_init", None, ctypes.c_int, ctypes.c_bool)(0, True)
        bind(lib, "gfx_init", None)()
        if not bind(lib, "game_data_load", ctypes.c_bool)():
            raise RuntimeError("the game data could not be loaded")
        palettes = bind(lib, "asset_palettes", ctypes.c_void_p)()
        bind(lib, "gfx_set_adapter", None, ctypes.c_int, ctypes.c_void_p)(
            1 if machine == "vga" else 0, palettes + 17)
        launch = GameLaunch(vga=machine == "vga", joystick=False, sound=False, flash_mode=0,
                            bios_ticks=seed)
        if not bind(lib, "game_init", ctypes.c_bool, ctypes.c_void_p,
                    ctypes.POINTER(GameLaunch))(self.state, ctypes.byref(launch)):
            raise RuntimeError("the game could not start")
        self.host = GameHostInput(0, True)

    def key(self, code: int) -> None:
        self._key(code)

    def tick(self) -> None:
        self._tick(self.state)

    def frame(self) -> None:
        self._frame(self.state, ctypes.byref(self.host))

    def dgroup(self, base: bytes) -> bytes:
        return self.dg.to_dgroup(self.state, base)

    def poke(self, offset: int, value: int) -> None:
        img = bytearray(self.dg.to_dgroup(self.state))
        struct.pack_into("<H", img, offset, value & 0xFFFF)
        self.dg.from_dgroup(bytes(img), self.state)


# ---- lockstep ------------------------------------------------------------------------------

@dataclass
class Script:
    keys: dict[int, list[int]] = field(default_factory=dict)        # tick -> scan codes
    pokes: dict[int, list[tuple[int, int]]] = field(default_factory=dict)   # tick -> (off, word)


@dataclass
class Result:
    ok: bool
    tick: int
    frame: int
    where: str = ""                  # "tick" / "frame" / "original" / "port"
    offsets: list[int] = field(default_factory=list)
    detail: str = ""
    values: dict[int, tuple[int, int]] = field(default_factory=dict)   # off -> (orig, port)


class Comparer:
    """Mapped DGROUP bytes, less game86's stack and `ignore`. While the title's draw hook
    runs, the 32 title llamas in entity slots 0..8 live in attract.c, not in the GameState
    (adapters.gamestate.ATTRACT_LLAMAS), so those bytes are left out then."""

    def __init__(self, dg: DgroupMap, ignore=()):
        keep = bytearray(0xFF if dg.mask[i] != MASK_UNMAPPED else 0 for i in range(DGROUP_SIZE))
        for i in list(STACK) + list(ignore):
            keep[i] = 0
        self.keep = int.from_bytes(keep, "little")
        for i in ATTRACT_LLAMAS:
            keep[i] = 0
        self.keep_title = int.from_bytes(keep, "little")

    def differ(self, a: bytes, b: bytes) -> list[int]:
        title = struct.unpack_from("<H", a, DRAW_HOOK)[0] == DRAW_HOOK_TITLE
        keep = self.keep_title if title else self.keep
        x = (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")) & keep
        if not x:
            return []
        raw = x.to_bytes(DGROUP_SIZE, "little")
        return [i for i, v in enumerate(raw) if v]


def known(off: int, a: bytes, b: bytes, play_start: bytes | None) -> bool:
    """A documented difference (see P2_TRAIL_HEAD, LEFTOVER_FIELDS); a = original, b = port."""
    if off == P2_TRAIL_HEAD and a[off] == P2_TRAIL_HEAD_LOADED and b[off] == 0:
        return True
    return (play_start is not None and off in ATTRACT_LLAMAS
            and (off - ENTITY_TABLE) % ENTITY_SIZE in LEFTOVER_FIELDS
            and b[off] == 0 and a[off] == play_start[off])


def run_lockstep(lib, script: Script, ticks: int, seed: int, machine: str = "vga",
                 phase: int = 0, start_delay: int = 0, game86=None,
                 original_dir: Path | None = None, ignore=(), progress=None,
                 on_frame=None) -> Result:
    """on_frame(orig, port, tick, frame), called after each frame whose DGROUP matched, may
    return a Result to stop with (e.g. a page comparison, tests/oracle/whole_game_pages.py)."""
    original_dir = original_dir or Path(os.environ.get("LLAMATRON_ORIGINAL_DIR", REPO / "original"))
    try:
        orig = OriginalGame(game86, original_dir, seed, machine)
    except OriginalFault as exc:
        return Result(False, 0, 0, "original", detail=f"start-up: {exc}")
    port = PortGame(lib, seed, machine)
    cmp = Comparer(port.dg, ignore)
    clock = Clock(machine, phase)
    tick = frame = 0

    play_start: bytes | None = None
    in_title = False

    def check(where: str) -> Result | None:
        nonlocal play_start, in_title
        a = orig.dgroup()
        b = port.dgroup(a)
        title = struct.unpack_from("<H", a, DRAW_HOOK)[0] == DRAW_HOOK_TITLE
        if title:
            in_title, play_start = True, None
        elif in_title:
            in_title, play_start = False, a
        bad = cmp.differ(a, b)
        bad = [o for o in bad if not known(o, a, b, play_start)]
        if bad:
            hooks = (struct.unpack_from("<H", a, DRAW_HOOK)[0], struct.unpack_from("<H", a, TICK_HOOK)[0])
            return Result(False, tick, frame, where, bad,
                          values={o: (a[o], b[o]) for o in bad},
                          detail=f"draw hook {hooks[0]:04x}, tick hook {hooks[1]:04x}")
        return None

    r = check("init")
    if r:
        return r

    def run_ticks(n: int) -> Result | None:
        nonlocal tick
        for _ in range(n):
            if tick >= ticks:
                return None
            tick += 1
            for off, value in script.pokes.get(tick, ()):
                orig.poke(off, value)
                port.poke(off, value)
            for code in script.keys.get(tick, ()):
                orig.key(code)
                port.key(code)
            orig.tick()
            port.tick()
            bad = check("tick")
            if bad:
                return bad
        return None

    try:
        for _ in range(start_delay):
            r = run_ticks(clock.frame_ticks())
            if r:
                return r
        while tick < ticks:
            r = run_ticks(clock.frame_ticks())
            if r:
                return r
            orig.frame()
            port.frame()
            r = check("frame")
            if r:
                return r
            if on_frame:
                r = on_frame(orig, port, tick, frame)
                if isinstance(r, Result):
                    return r
            if orig.stray_waits:
                cs, ip = orig.stray_waits[0]
                return Result(False, tick, frame, "original",
                              detail=f"3DAh polled outside wait_vsync at {cs:04x}:{ip:04x}")
            frame += 1
            if progress and frame % 500 == 0:
                progress(tick, frame)
    except OriginalFault as exc:
        return Result(False, tick, frame, "original", detail=str(exc))
    return Result(True, tick, frame)


# ---- reports -------------------------------------------------------------------------------

def field_names(lib) -> dict[int, str]:
    """DS offset -> field name, from the adapter's table when it has one."""
    try:
        name_of = bind(lib, "dgroup_map_field_name", ctypes.c_char_p, ctypes.c_uint)
    except AttributeError:
        return {}
    out = {}
    for off in range(DGROUP_SIZE):
        n = name_of(off)
        if n:
            out[off] = n.decode()
    return out


def describe(res: Result, orig_img: bytes | None = None, port_img: bytes | None = None) -> str:
    if res.ok:
        return f"ok ({res.tick} ticks, {res.frame} frames)"
    if res.where == "original":
        return f"original stopped at tick {res.tick} frame {res.frame}: {res.detail}"
    offs = ", ".join(f"{o:04x}={res.values[o][0]:02x}/{res.values[o][1]:02x}"
                     for o in res.offsets[:24])
    more = f" (+{len(res.offsets) - 24})" if len(res.offsets) > 24 else ""
    return (f"first divergence after {res.where} at tick {res.tick} frame {res.frame}: "
            f"DS offset=original/port {offs}{more} ({res.detail})")


def load_reference_run(name: str) -> tuple[Script, int, dict]:
    runs = json.loads((TESTS / "golden" / "reference_runs.json").read_text())
    cfg = runs[name]["config"]
    script = Script()
    for t, code in cfg.get("keys", []):
        script.keys.setdefault(t, []).append(code)
    for t, off, value in cfg.get("pokes", []):
        script.pokes.setdefault(t, []).append((off, value))
    return script, cfg.get("quit_after", 720), cfg


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run", help="a reference run name (tests/golden/reference_runs.json) or 'sweep'")
    ap.add_argument("--ticks", type=int)
    ap.add_argument("--seed", type=int)
    ap.add_argument("--phase", type=float, default=0.0,
                    help="tick phase (fraction of a tick, as llamatron_replay --tick-phase)")
    ap.add_argument("--levels", default="0-99:5")
    ap.add_argument("--game86", default=os.environ.get("LLAMATRON_GAME86_IMAGE"))
    ap.add_argument("--ignore", default="",
                    help="DS offsets (hex, a-b ranges) to leave out, e.g. known divergences")
    args = ap.parse_args()
    ignore = set()
    for part in filter(None, args.ignore.split(",")):
        lo, _, hi = part.partition("-")
        ignore.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    lib = load_testlib()
    if lib is None:
        print("LLAMATRON_GAME_TESTLIB is not set or not built", file=sys.stderr)
        return 2
    phase = min(65535, int(args.phase * 65536 + 0.5)) if args.phase > 0 else 0
    if args.run != "sweep":
        script, ticks, cfg = load_reference_run(args.run)
        res = run_lockstep(lib, script, args.ticks or ticks,
                           args.seed if args.seed is not None else cfg["ticks"],
                           cfg.get("machine", "vga"), phase, game86=args.game86, ignore=ignore,
                           progress=lambda t, f: print(f"  tick {t} frame {f}", flush=True))
        print(f"{args.run}: {describe(res)}")
        return 0 if res.ok else 1
    print("sweep: not yet", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
