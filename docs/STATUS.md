# Project status

Where the port stands against the milestones in `docs/PLAN.md` §6.

**What was checked, and how** (2026-10-19):
- I built commit `911295c` from a clean export (`git archive`) and ran:
  - the 99 native tests outside the DOSBox label: 99 passed;
  - the DOSBox-X reference runs: 35 passed, 1 skipped (the skipped one has no VRAM capture to compare).
- The web tests were last run on `1fa2f75`: 34 of 34 passed.
- Anything else below is quoted from a commit message and marked *(unverified)*.
- Commits after `911295c` have not been run here.

Words used below:
- **diff-tested**: the port's function is run side by side with the original machine code on the same inputs, and the results must match.
- **golden run**: a scripted session of the original game in DOSBox-X, recorded as hashes.

## Summary

| Milestone | State |
|---|---|
| M0 Repo, extraction, symbols | done |
| M1 Extractor and title screen (native + web) | done |
| M2 Renderer, timing, input, drawing routines | done |
| M3 State machine, scripts, player, first waves | done |
| M4 All enemies, all 100 levels, game over, hi-scores | done |
| M5 Sound | done |
| M6 Reference runs against the original | in progress: all 12 recorded runs reproduce; the port matches 10 of them, 2 warped-level runs have open differences |
| M7 Polish | mostly done: the macOS app bundle is not self-contained yet |

## M0: repo skeleton, extraction, symbols: done
- The extractor verifies the original files by hash, and its unpacker matches the Python reference tools byte for byte. Tests: `extract_verify`, `extract_verify_rejects_mismatch`, `unpack_matches_python_{game86,fs,llama,install}`.
- `docs/funcmap.csv` maps every original routine (435 rows) to its C counterpart. `tools/ghidra/` applies names and comments to the headless Ghidra project. game86 has no unnamed (`unk_`) routines or variables left.

## M1: extractor and title screen: done
- Everything is extracted at build time: sprites, font, levels, texts, samples, pictures, tables. Tests: `assets_formats`, `assets_game_data_files`, `assets_game_data_embedded`, `strings_roundtrip`, `levels_roundtrip`, `extract_lzss_oracle`.
- The title screen is checked by hash on VGA and EGA (`title_hash_vga`, `title_hash_ega` and the `_embedded` variants). The title decoder is diff-tested against fs.exe (`oracle.fs_title`).
- Web: `web_smoke_title_vga`, `web_smoke_title_ega`, `web_llamatron_title`, `web_llamatron_to_game` pass.

## M2: renderer, timing, input, drawing: done
- Tests: `engine_selftest`, `oracle.engine_input`, `oracle.engine_pages`, `oracle.engine_palette`, `oracle.ega`.
- The original's drawing routines are diff-tested: `oracle.draw_basic`, `oracle.draw_wrappers`, `oracle.sprites`, `oracle.text`, `oracle.scroller`, `oracle.hud`, `oracle.palette_pulse`, `oracle.flash`.
- The attract and title screen code is diff-tested too: `oracle.attract`, `oracle.story_skip`. `oracle.attract` also covers `attract_end`.

## M3: game core: done
- The state machine, level scripts, player, shots and waves are diff-tested: `oracle.script`, `oracle.player`, `oracle.shots`, `oracle.wave_core`, `oracle.wave_screens`, `oracle.movement`, `oracle.rng`, `oracle.score`, `oracle.pause`, `oracle.continue`.
- The real executable starts a game: `llamatron_to_game`, `llamatron_title_phase`, `llamatron_visual_walk_vga`.
- `tests/oracle/whole_game.py` runs the whole game86 and the port in lockstep. By its commit messages, the 7 golden scenarios match on every tick and level 10 matches for 1500 ticks *(unverified: not run here)*.

## M4: all enemies and levels: done
- game86 has 381 rows in funcmap: 364 diff-tested, 15 ported, 2 marked todo. The 2 todo rows are:
  - `sprite_draw_all_pages` (`424c`): dead code;
  - `mem_shrink` (`e788`): DOS memory, not ported on purpose.
- The 15 ported rows are start-up, video mode, interrupt and wait routines, plus `main` (`0010`), whose parts are diff-tested and which the whole-game oracle runs.
- All 41 enemy init handlers and all 46 per-tick (think) handlers are diff-tested: `oracle.enemy_init`, `oracle.think_basic`, `oracle.think_herders`, `oracle.think_llamas`, `oracle.think_orbiters`, `oracle.think_w5a`, `oracle.w5a_types`, `oracle.hit`, `oracle.pickups`, `oracle.anim`, `oracle.entity_animate`, `oracle.entity_loop`, `oracle.spawn_clobber`.
- `oracle.level_sweep` plays each of the 100 levels for 3000-5000 ticks and checks that every live entity uses ported code and stays drawable. All 100 pass.
- Game over and hi-scores: `oracle.game_over`, `oracle.hiscore`, `oracle.hiscore_save`. The scores file survives a restart: `llamatron_scores_{reset,low,play,reload}`.

## M5: sound: done
- Tests: `snd_selftest`, `snd_hash_title` (the title sample), `oracle.snd_driver` (the mixer's driver logic diff-tested against the original Sound Blaster driver), and the sound toggle in `oracle.pause`.
- Web: `web_game_sound` and `web_smoke_title_sound` pass.
- `web_game_sound` fails on the commits from `3a35a42` to `9f47591`. Its expected value was taken from a working tree that already held the change later committed as `379b7ab`, so it matches from `379b7ab` on (Lantern).

## M6: reference runs: in progress
The original game ran in DOSBox-X for 12 scripted sessions, and the port replayed the same inputs:

| Run | Original matches its recorded hashes | Port replay |
|---|---|---|
| attract, walk_vga, walk_ega, warp, death_continue, game_over_hiscore, two_player | yes | identical on every compared tick, apart from known one-tick timing shifts (DEVIATIONS #20) |
| level_10, level_25, level_75 | yes | identical apart from timing |
| level_50 | yes | differs from tick 1150: the port kills entity 2, which is still alive in the original (Keel, open) |
| level_99 | yes | differs from tick 700: an entity's overlay frame count, 12 in the original (Prism and Keel, open) |

- The 7 original runs: the earlier differences (the continue a tick late, the two-player start, the warp positions, the title llamas' leftovers) are gone since Keel's `379b7ab` and `7b4ed95`.
- The 5 warped-level runs are new in `1fa2f75` (Sentinel and Prism). On `1fa2f75`, level_10's and level_50's recorded hashes did not reproduce here. The cause was the blitter's mode bytes DS:39D9..39DD: they are left over from the last draw call, so their value depends on where in the frame the snapshot was taken. Since `911295c` the hash leaves them out, as it already did for the page bookkeeping at 39D3..39D8 (Sentinel).
- The differences that are still open are listed in `tests/dosbox/test_reference_golden.py` (TRIAGE).
- Native and web parity: `web_llamatron_parity` passes.

## M7: polish: mostly done
- Done:
  - options screen (F2 on the title): `options_selftest`, `llamatron_options_*`;
  - display scaling and full screen: `web_display_{hidpi,integer,sharp}`;
  - gamepads: `controls_selftest`;
  - pause: `oracle.pause`. Quit is implemented but has no test of its own.
- Every deliberate difference from the original is listed in `docs/DEVIATIONS.md`. It has 26 rows, audited against the original code this week. Every row whose exact case can be reached has a test:
  - #1 and #2 in `assets_game_data_*`;
  - #8, #9 and #19 in `oracle.w5a_deviations`;
  - #26 in `oracle.attract` (`test_attract_end`).
- #10 was withdrawn in `3eb91f7`. The value it was about is zeroed at 1000:04ef before anything reads it, so the port behaves the same.
- Open: the macOS app still needs Homebrew's SDL2 (`src/platform/README.md`, TODO).

## How to verify
```sh
# native build and all tests (needs your copy of the original game in original/)
cmake --preset macos-native -DLLAMATRON_ORIGINAL_DIR=$PWD/original
cmake --build build
ctest --test-dir build -j8

# web build (needs emsdk and Chrome); run the tests one at a time
emcmake cmake --preset web-emscripten -B build-web -DLLAMATRON_ORIGINAL_DIR=$PWD/original \
  -DLLAMATRON_NATIVE_LLAMATRON=$PWD/build/src/platform/llamatron
cmake --build build-web
ctest --test-dir build-web -j1

# the DOSBox-X reference runs (slow, about 10 minutes; needs dosbox-x and nasm)
LLAMATRON_REPLAY=$PWD/build/tests/llamatron_replay \
  tools/harness/.venv/bin/python -m pytest -s tests/dosbox/test_reference_golden.py
```
With `-s`, the reference test prints a per-run report: which game variables differ, from which tick, and who is on it.
