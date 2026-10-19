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

## DOSBox-X reference runs (`dosbox/refrun.py`, PLAN §3.3)

`brew install dosbox-x` (local, no account). Each run copies the original folder to
`build-dosbox/<run>/c` (gitignored; the original is never touched) and writes the
chosen `llama.inf` there: video V/E, keyboard, no sound, flash Y/P/N. It then
generates a config with fixed CPU settings (normal core, 386, fixed cycles, no sound
devices), injects keys with DOSBox-X's `AUTOTYPE`, and runs `-silent` with a time
limit. The batch line after `llama.exe` writes `REFRUN.TXT`, so a clean exit through
the launcher can be told apart from a hang or a kill.

    tools/harness/dosbox/refrun.py smoke [--machine ega]   # space past fs.exe, q quits game86
    tools/harness/dosbox/refrun.py run --keys "space" --time-limit 120 --window

ctest: `dosbox.smoke_vga` and `dosbox.smoke_ega` (label `dosbox`, ~25 s each, exclude
with `-LE dosbox`). These are registered only when dosbox-x and the original files exist.

Findings: COMMAND.COM here leaves `%errorlevel%` empty, so the exit code is not
recorded. Keys typed before game86 installs its INT 09 handler (`1000:e567`) are
lost, so the smoke run repeats `q`.

### PROBE.COM (`dosbox/probe.asm`, our own code; `brew install nasm`)

`refrun.py probe --ticks 0x0012ABCD [--machine ega]` assembles the probe into the run
copy and starts game86 through it, using the launcher's command-tail contract (sound
= none, keyboard). The probe:
- masks IRQ0 and pins the BIOS tick word, so the tick schedule stays constant through
  the seed read (`1000:5f6b`) and the 8000 mixer calls of the shuffle;
- snapshots DGROUP (64 KB) at game86's first INT 10h AX=000Dh (`1000:5960`, right
  after the shuffle), then releases the timer;
- records the game's exit code. After the game exits it writes `PROBE.HDR` (layout in
  probe.asm) and `PROBE.DG`.

The snapshot is a copy of the original's memory. It stays in the gitignored work
directory; never commit it (hashes are fine). `tests/dosbox/test_probe_rng.py`
(ctest `dosbox.probe_rng`) checks the first end-to-end chain: the table and mixer
state from DOSBox-X equal the unicorn oracle's for the same tick, on VGA and EGA.

Next: periodic DGROUP/VRAM snapshots (wrap game86's INT 08 hook, buffer in XMS) and
deterministic input timing for whole-game reference runs.
