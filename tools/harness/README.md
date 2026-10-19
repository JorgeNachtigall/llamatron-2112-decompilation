# Differential-test harness (test oracle only)

Runs routines of the **original** `game86.exe` in unicorn (16-bit real mode) so tests
can compare the port's C functions against them. Per docs/PLAN.md §0.1 this is never
linked into or called from `src/`; the shipped game never executes original code.

Setup (creates the gitignored `tools/harness/.venv`):

    tools/harness/setup_venv.sh

Image: `$LLAMATRON_GAME86_IMAGE`, else `build/generated/exe/game86.unpacked` (flat load
module + `.layout.json` from tools/extract), else `unpacked/game86.exe` (MZ, relocated
on load). Loaded at segment 0x1000, so Ghidra addresses are emulator addresses; DGROUP
(3559) is read from the entry `MOV DX,imm`.

API (`llamatron_harness.Machine`):

| call | purpose |
|------|---------|
| `Machine.from_game86(path=None)` | fresh 1 MB memory with the relocated image + PSP |
| `write_dgroup / read_dgroup / write / read / set_bios_ticks` | build the input state |
| `call("1000:e4de", regs={}, far=False)` | run to the routine's RET (sentinel return address, stack-depth checked) |
| `run_until(start, stop)` | run inline code that is not a routine (e.g. init loops) |
| `interrupts={0x10: fn}` / `io_read` | stub INTs (unhandled INT aborts the run) and port reads; all port I/O is logged |

`CallResult` holds `regs`, `memory_diff` (changed byte runs over 1 MB), `dgroup_diff`
(`{ds_offset: (old, new)}`), `interrupts`, `ports`, and `stack_scratch`: writes below
the caller's SP. These are kept out of the diffs because game86's stack segment
lies inside the DGROUP window.

CLI for quick experiments:

    tools/harness/.venv/bin/python -m llamatron_harness 1000:e4de --ds 39e9=ff01

VRAM: the A000 window is mapped (unicorn mmio) to `Machine.ega`, a Python model of
the EGA/VGA planar path (`ega.py`): 4 planes, latches, SEQ map mask, GC set/reset,
enable set/reset, colour compare, rotate/logical op, read map, write modes 0-3, read
modes 0/1, bit mask; CRTC registers stored. Port writes to 3C4/3C5, 3CE/3CF and
3D4/3D5 drive it. `ega.rasterise(page)` returns 320x200 indexed pixels;
`ega.load_indexed(pixels, page)` is the inverse, for building input pages.
`CallResult.vram_in/vram/vram_changed` hold the planes before and after a call.
`bios_int10` is an opt-in INT 10h stub (mode 0Dh reset).
