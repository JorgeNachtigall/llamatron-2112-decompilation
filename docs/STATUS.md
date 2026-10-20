# Project status

Where the port stands against the milestones in `docs/PLAN.md` §6.

**What was checked, and how** (2026-10-20):
- I built commit `61fc59e` from a clean export (`git archive`) and ran:
  - the 100 native tests outside the DOSBox label: 100 passed;
  - the macOS app workflow (`cmake --workflow --preset macos-app`): 4 of 4 passed;
  - all 35 web tests (Emscripten build, headless Chrome): 35 passed;
  - the DOSBox-X reference runs: 35 passed, 1 skipped (the skipped one has no VRAM capture to compare);
  - the whole-game lockstep sample (`tests/oracle/lockstep_check.py --sample`): 12 of 12 match.
- On `af88aab` I also ran the 100-level lockstep sweep with pages (1500 ticks per level): 99 of 100 match (M6).
- On `c3657ab`, the release check (lead's request), from a clean export:
  - all 21 DOSBox-X reference runs in strict mode (`LLAMATRON_GOLDEN_SET=all LLAMATRON_REPLAY_STRICT=1`): 60 passed, 2 skipped, 1 failed. The failure is in the original game, not the port (M6).
  - `tests/oracle/lockstep_check.py --all -j 6`, with its own fresh build: 100 of 100 levels match over 3000 ticks, in 28 minutes.
- Anything else below is quoted from a commit message and marked *(unverified)*.
- Commits after `c3657ab` have not been run here.

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
| M6 Reference runs against the original | done (confirmed by the lead): the port matches every DOSBox-X run apart from timing and explained, pinned differences; lockstep: 100 of 100 levels |
| M7 Polish | done (confirmed by the lead) |

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
- `tests/oracle/whole_game.py` runs the whole game86 (in unicorn) and the port in lockstep, comparing game state and drawn pages after every tick and frame. Results are in M6.

## M4: all enemies and levels: done
- game86 has 381 rows in funcmap: 364 diff-tested, 6 run-verified, 6 ported, 5 n/a, none todo.
  - n/a means the port has no counterpart, and the notes give the reason. For game86 that is three dead routines (`424c`, `5994`, `5eae`) and two pieces of DOS/BIOS glue (`e605` kbd_flush, `e788` mem_shrink).
  - The 6 run-verified rows are `main` (`0010`), `wave_run` (`0455`), `video_init` (`5960`), `screen_reveal_bg` (`61ff`), `wait_vsync` (`e5f3`) and `frame_run` (`1e7b:013f`). Each is checked by the whole-game runs (M6).
- The other programs: fs.exe has 16 diff-tested, 6 ported and 2 n/a; llama.exe has 1 diff-tested, 2 ported and 12 n/a; install.exe has 1 diff-tested and 14 n/a. The n/a rows are the launcher's DOS and sound-card code and the installer's text UI.
- All 41 enemy init handlers and all 46 per-tick (think) handlers are diff-tested: `oracle.enemy_init`, `oracle.think_basic`, `oracle.think_herders`, `oracle.think_llamas`, `oracle.think_orbiters`, `oracle.think_w5a`, `oracle.w5a_types`, `oracle.hit`, `oracle.pickups`, `oracle.anim`, `oracle.entity_animate`, `oracle.entity_loop`, `oracle.spawn_clobber`.
- `oracle.level_sweep` plays each of the 100 levels for 3000-5000 ticks and checks that every live entity uses ported code and stays drawable. All 100 pass.
- Game over and hi-scores: `oracle.game_over`, `oracle.hiscore`, `oracle.hiscore_save`. The scores file survives a restart: `llamatron_scores_{reset,low,play,reload}`.

## M5: sound: done
- Tests: `snd_selftest`, `snd_hash_title` (the title sample), `oracle.snd_driver` (the mixer's driver logic diff-tested against the original Sound Blaster driver), and the sound toggle in `oracle.pause`.
- Web: `web_game_sound` and `web_smoke_title_sound` pass.
- `web_game_sound` fails on the commits from `3a35a42` to `9f47591`. Its expected value was taken from a working tree that already held the change later committed as `379b7ab`, so it matches from `379b7ab` on (Lantern).

## M6: reference runs: done
The original game ran in DOSBox-X for 12 core scripted sessions, and the port replayed the same inputs:

| Run | Original matches its recorded hashes | Port replay |
|---|---|---|
| attract, walk_vga, walk_ega, warp, death_continue, game_over_hiscore, two_player | yes | identical on every compared tick, apart from known one-tick timing shifts (DEVIATIONS #20) |
| level_10, level_25, level_75, level_99 | yes | identical apart from timing and the pinned rows below |
| level_50 | yes | differs from tick 1150, explained and pinned (below) |

- The test pins each explained difference exactly (`EXPECTED` in `tests/dosbox/test_reference_golden.py`), so a new difference still fails it:
  - level_50 at 1150: the port kills an entity because of its one-tick lag after a respawn (DEVIATIONS #20). In lockstep, where both sides follow one schedule, level_50 matches for all 3010 ticks.
  - level_99 at 700 and 2800: a rescued llama's overlay frame count, one frame apart (frame phase, DEVIATIONS #24).
  - level_99 at 2250 and 2400: the follower trail read past P2's ring (DEVIATIONS #14).
- The long set (`LLAMATRON_GOLDEN_SET=long`) adds 9 runs: levels 20, 30, 40, 60, 70, 80 and 90, level 25 on EGA, and level 40 with two players. On `c3657ab` I ran all 21 runs in strict mode:
  - The port replay is identical apart from timing and the pinned rows on every run. The long set's pinned rows are:
    - level_30 at 900 and level_25_ega at 2550: play resumes one tick later after a death (DEVIATIONS #20);
    - level_20 at 1450 and level_40_two_player at 1300 and 1550: overlay frame counts (frame phase).
  - 20 of 21 original runs reproduce their recorded hashes. level_40_two_player does not: at IRQ 2400 the game state matches, but the screen capture lands at IRQ 2402 instead of the recorded 2401. A rerun gives the same result. This is in the test's capture timing, not the game. Reported to Sentinel; the lead reports all 21 green *(not reproduced here)*.
- Since `911295c` the snapshot hash leaves out the blitter's mode bytes DS:39D9..39DD, as it already did the page bookkeeping at 39D3..39D8. They are left over from the last draw call, so they depend on where in the frame the snapshot was taken (Sentinel).
- Whole-game lockstep (`tests/oracle/`, game86 in unicorn against the port, game state and drawn pages after every tick and frame):
  - `lockstep_check.py --sample` (10 levels for 1500 ticks, plus a two-player and an EGA warp; about 5 minutes; ctest `oracle_long.lockstep_sample` with `-DLLAMATRON_ORACLE_LONG=ON`): 12 of 12 match on `61fc59e`.
  - `whole_game.py sweep --levels 0-99 --ticks 1500 --pages` on `af88aab`: 99 of 100 match. Level 99 differs at tick 1408, in `chain_tail` (DEVIATIONS #14), as Prism found.
  - `lockstep_check.py --all` (all 100 levels for 3000 ticks): 100 of 100 match on `c3657ab`, run here with its own fresh build at `-j 6` (28 minutes on this Mac; Thorn's estimate was about 3.5 hours).
  - Prism's lockstep sweeps of other start configurations: EGA 100/100, two players 100/100, two players sharing a start 100/100, joystick 8/8 *(unverified: not run here)*.
- Native and web parity: `web_llamatron_parity` passes.

## M7: polish: done
- Options screen (F2 on the title): `options_selftest`, `llamatron_options_*`.
- Display scaling and full screen: `web_display_{hidpi,integer,sharp}`.
- Gamepads: `controls_selftest`.
- Pause: `oracle.pause`. Quit is implemented but has no test of its own.
- The macOS app is self-contained: SDL is bundled and ad-hoc signed (`554d3c2`, `b152db0`). `macos_app_self_contained` and `macos_app_bundled_sdl` pass, and the icon is drawn from the title picture at build time (`app_icon_from_title`).
- How to play: `docs/PLAYING.md`. How to build: `README.md`.
- Entity records use named per-kind views instead of raw offsets (`docs/entity-fields.md`; Moss and Thorn), and the leftover general aux fields are gone (`da90687`). The tests above pass on that code.
- Every deliberate difference from the original is listed in `docs/DEVIATIONS.md` (26 rows, audited against the original code). Every row whose exact case can be reached has a test:
  - #1 and #2 in `assets_game_data_*`;
  - #8, #9 and #19 in `oracle.w5a_deviations`;
  - #26 in `oracle.attract` (`test_attract_end`);
  - #14, #20 and #24 in the reference runs' `EXPECTED` rows.
- #10 was withdrawn in `3eb91f7`. The value it was about is zeroed at 1000:04ef before anything reads it, so the port behaves the same.

## How to verify
```sh
# native build and all tests (needs your copy of the original game in original/; README.md)
sh tools/harness/setup_venv.sh
cmake --preset macos-native -DLLAMATRON_ORIGINAL_DIR=$PWD/original
cmake --build build
ctest --test-dir build -j8

# web build (needs emsdk and Chrome); run the tests one at a time
emcmake cmake --preset web-emscripten -B build-web -DLLAMATRON_ORIGINAL_DIR=$PWD/original \
  -DLLAMATRON_NATIVE_LLAMATRON=$PWD/build/src/platform/llamatron
cmake --build build-web
ctest --test-dir build-web -j1

# the DOSBox-X reference runs (slow, about 10 minutes; needs dosbox-x and nasm;
# LLAMATRON_GOLDEN_SET=all adds the long runs, about 20 minutes)
LLAMATRON_REPLAY=$PWD/build/tests/llamatron_replay \
  tools/harness/.venv/bin/python -m pytest -s tests/dosbox/test_reference_golden.py

# the macOS app (game files in original/)
cmake --workflow --preset macos-app

# whole-game lockstep, from tests/ (--sample about 5 minutes; --all 28 minutes at -j 6 here)
../tools/harness/.venv/bin/python oracle/lockstep_check.py --sample
```
With `-s`, the reference test prints a per-run report: which game variables differ, from which tick, and who is on it.
