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
  depend on host speed, and the page bookkeeping DS:39D3..39D8 (front/back/draw page), which
  moved run to run under heavy host load; pages are checked through `shown_page()` and its
  flip rule. VGA snapshots also have `vram` (raw planes), `vram_irq`, `pages`
  (sha256 of each page 0000/2000/4000 rasterised to 64000 indexed pixels, as
  `EgaPlanes.rasterise`) and `shown` (the CRTC start address at capture).
- Run-to-run stability: DGROUP and the background page (4000) reproduce exactly. The hidden
  page can be caught mid-draw, and how far the draw got varies with host load, so its hash
  and the raw `vram` hash are stored but only reported (`settled()`). A capture that lands
  exactly on a page flip sees the old or the new page from run to run: when the shown start
  address differs from the golden's, the shown page is only reported; at the same address
  its pixels must match (`shown_page()`). A probe change, even to start-up code, moves game86's frame timing: run the
  golden from a fresh workdir after one.
- The replay driver uses the EGA monitor's 59.92 Hz frame clock for `--ega` runs and VGA
  mode 0Dh's 70.086 Hz otherwise.
- Clock calibration: each run's `tick_phase` (fraction of a timer tick elapsed at game_init)
  and `start_delay` (retraces of timer ticks before the first frame) go to
  `llamatron_replay --tick-phase F --start-delay N`, the same flags as `llamatron --replay`.
  They are stored in the golden `config`, which visual_check.py and web_parity.py read. The
  values are fitted per run (the comment above `RUNS` says how). A config edit needs
  `LLAMATRON_GOLDEN_UPDATE=1`.
- Timing shifts: the report lists a byte as timing, not as a divergence (DEVIATIONS #20),
  when it equals the port's state one tick later or earlier at a transition. A transition is
  a tick or draw hook switch, or a jump of the tick counter (DS:39E6). Also timing: a frozen
  tune.countdown one step from the port's (the tune hook ran a tick longer), and the tick
  counter one apart right after a jump (the original set it a tick later). While a shift
  holds, the whole state is compared with the port's tick t + shift, and every byte that
  differs there is real, even one equal at the port's own tick (input reaches both runs at
  one IRQ). Only real bytes decide the first divergence. In play, title llama bytes left in
  the original's used slots (unchanged since the first play snapshot, 0 in the port) are
  skipped, only in the fields `tests/dosbox/llama_residue.py` showed never read before
  written in these runs (it pokes them with 0 and their inverse at the attract -> play switch
  and compares with a control run; the report's header names them). +20 is left out: it is
  read in other play (DEVIATIONS #26). Input shift: `input_shift` segments in RUNS deliver the
  port's keys a tick later where it is a tick behind after a timing-classed switch (input at
  the same game phase); each segment must match the timing shift at its start, and the report
  has a line per segment. `TRIAGE` in
  test_reference_golden.py lists each real divergence with its owner and status, printed
  under the run's report.
- Dev loops: `tests/replay/window.py SCENARIO FIRST [--count N]` runs a scenario with a
  snapshot after every tick from FIRST (at most 60) and the port over the same ticks, then
  prints the report and every differing byte at the first divergent tick. `--frames` adds
  each of those bytes' values after every port frame from tick t-1 to t+1
  (`llamatron_replay --frames FILE`, LRPF), to tell frame phase from a real difference.
  `tools/harness/dosbox/refrun.py pages --snp .../c/PROBE.SNP --vram-irq M --out DIR`
  writes one VRAM snapshot as three pages of indexed pixels (never commit them).
