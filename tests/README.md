# Tests

- `oracle/test_*.py`: differential tests. Each runs original game86 routines through
  `tools/harness` and compares them with our behaviour. Today that is a Python
  reference; once the C functions exist in `src/`, it will be those functions.
- `adapters/`: the only place that knows original offsets. These modules convert
  between our state structs and the DGROUP byte layout (`rng.py` now;
  `dgroup_map.c` for the C `GameState` later).

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
- DOSBox-X reference runs (PLAN §3.3) must pin this too, either by freezing the tick
  (setting 0040:006C and stopping the timer during start-up) or by recording the tick
  at every `e616` call with a breakpoint. Otherwise their RNG table differs from the
  port's even with the "same seed".
