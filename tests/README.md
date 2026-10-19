# Tests

- `oracle/test_*.py`: differential tests. Each runs original game86 routines through
  `tools/harness` and compares them with our behaviour. Today that is a Python
  reference; once the C functions exist in `src/`, it will be those functions.
- `generators/`: seeded generators of valid random inputs (RNG state, indexed pages,
  rectangles, entity tables). Cases are reproducible from the test name and case number.
  `LLAMATRON_FUZZ_SCALE=N` multiplies the case counts; `LLAMATRON_FUZZ_SEED=S` picks
  another reproducible set.
- `dosbox/test_*.py`: end-to-end runs of the original in DOSBox-X through
  `tools/harness/dosbox`. They are slow, so they are opt-in: configure with
  `-DLLAMATRON_DOSBOX_TESTS=ON` (label `dosbox`; they skip without dosbox-x, nasm or
  the original files).
- `adapters/`: the only place that knows original offsets. These modules convert
  between our state structs and the DGROUP byte layout (`rng.py`, `entities.py`; `dgroup_map.c` for the C `GameState`
  later); `cport.py` loads the port's `llamatron_game_testlib` via ctypes (fixture `cgame`).

Run:

    tools/harness/setup_venv.sh
    cmake -S tests -B build/tests && ctest --test-dir build/tests      # standalone
    # or, from the top-level build once it does add_subdirectory(tests):
    ctest --test-dir build -L oracle

Without a game image (`original/` not extracted), the oracle tests report *Skipped*.

## Note: the RNG seed is not a single number

Start-up builds the gameplay random table (`DS:6AC9`, 512 bytes: two copies of 0..255)
in `1000:007b..00ab` with 4000 swaps. Each swap calls the mixer `1000:e616` twice, and
**every one of those 8000 calls re-reads the live BIOS tick word** at `0000:046C`
(`[DS:0x44D]` = 0, so `ES:[0x46C]`). The tick word becomes the mixer's new `s2` on every
call; when it equals the old `s2` it is first perturbed (`s3 += ticks + 0x2F; s2 = ticks ^ s3`).
Since the state is shifted (`s0 <- s1 <- s2 <- tick`), after the first two calls the
output depends almost entirely on the tick values seen.

Consequences:
- The table depends on the initial tick value (via the seed `[0x453..0x459]` set by
  `1000:5f6b`) and also on **when the 18.2 Hz BIOS tick advances during the shuffle**.
  On a slow machine (8086 at 4.77 MHz) the shuffle takes several tick periods, so the
  tick changes partway through. On a fast machine or emulator it may never change.
- The port must therefore take as input a *tick schedule*: the BIOS tick value seen by
  call n (n = 0..7999). The default is a constant schedule (the start tick), which is
  what the oracle tests use (`test_init_shuffle`).
- DOSBox-X reference runs (PLAN §3.3) must pin this too. Otherwise their RNG table
  differs from the port's even with the "same seed". `PROBE.COM` does it by masking
  IRQ0 and fixing 0040:006C until the mode-0Dh INT 10h that follows the shuffle.
  `dosbox.probe_rng` confirms that the DOSBox-X table equals the oracle's for the same
  constant schedule.

## Whole-game replays against the reference runs

- `tests/adapters/dgroup_map.{c,h}` converts `GameState` to and from the original's DGROUP
  bytes (test-only). `tests/adapters/gamestate.py` (`DgroupMap`) wraps it for the oracle
  tests: `from_dgroup` → C call → `to_dgroup` → `mismatches` against the original.
- `llamatron_replay` (`tests/replay/replay_driver.c`, built with the testlib) runs the port
  headless on a PROBE.KEY script, sequenced like `llamatron --replay`. It writes the state
  as DGROUP images every K ticks (LRPL file, with the adapter's mask).
- `tests/replay/compare.py PORT.lrpl PROBE.SNP` lists, per snapshot tick, the mapped
  fields that differ from the DOSBox-X reference run.
- `tests/dosbox/test_reference_golden.py` (label `dosbox`, opt-in) does both steps:
  - It checks the reference run against `tests/golden/reference_runs.json`, which holds
    only hashes. `LLAMATRON_GOLDEN_UPDATE=1` rewrites the file after a deliberate change.
  - It writes the port's divergence report to `<build>/tests/dosbox/replay-report-*.txt`.
  - `LLAMATRON_REPLAY_STRICT=1` makes any divergence fail. Leave it off until the game
    loop is complete.
- Scenarios (`RUNS` in that file, one-line summary at the top of each report): `walk_vga`,
  `walk_ega`, `attract`, `two_player` (droid), `warp`, `death_continue`,
  `game_over_hiscore`. Keys cannot reach a warp or a quick game over, so a run can also
  poke words into DGROUP (PROBE.POK `{u32 irq, u16 DS offset, u16 word}`, applied before
  that IRQ's keys). `llamatron_replay --pokes` applies the same words through the adapter,
  so only mapped offsets can be poked.
- Golden hashes: `dgroup` leaves out game86's stack (DS:84D0..85E1), whose interrupt frames
  depend on host speed. VGA snapshots also have `vram` (raw planes), `vram_irq`, `pages`
  (sha256 of each page 0000/2000/4000 rasterised to 64000 indexed pixels, as
  `EgaPlanes.rasterise`) and `shown` (the CRTC start address at capture).
- The replay driver uses the EGA monitor's 59.92 Hz frame clock for `--ega` runs and VGA
  mode 0Dh's 70.086 Hz otherwise.
- Dev loops: `tests/replay/window.py SCENARIO FIRST [--count N]` runs a scenario with a
  snapshot after every tick from FIRST (at most 60) and the port over the same ticks, then
  prints the report and every differing byte at the first divergent tick.
  `tools/harness/dosbox/refrun.py pages --snp .../c/PROBE.SNP --vram-irq M --out DIR`
  writes one VRAM snapshot as three pages of indexed pixels (never commit them).
