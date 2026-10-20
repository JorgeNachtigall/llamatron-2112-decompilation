# Project status

Where the port stands against the milestones in `docs/PLAN.md` §6.

**What was checked, and how** (2026-10-19):
- I built commit `65acb2b` from a clean export and ran:
  - all 92 native tests: 92 passed;
  - the DOSBox-X reference runs: 20 passed, 1 skipped (the skipped one has no VRAM capture to compare).
- Anything else below is quoted from a commit message and marked *(unverified)*.
- Commits after `65acb2b` have not been run here. Up to `39eb341` they are `b124a3d`, `9cc9522`, `438b760`, `ccf2264`, `0a8b244`, `4815af6`, `de4c000` and `39eb341`.

Words used below:
- **diff-tested**: the port's function is run side by side with the original machine code on the same inputs, and the results must match.
- **golden run**: a scripted session of the original game in DOSBox-X, recorded as hashes.

## Summary

| Milestone | State |
|---|---|
| M0 Repo, extraction, symbols | done |
| M1 Extractor and title screen (native + web) | done (web part unverified) |
| M2 Renderer, timing, input, drawing routines | done |
| M3 State machine, scripts, player, first waves | done |
| M4 All enemies, all 100 levels, game over, hi-scores | done |
| M5 Sound | done (web part unverified) |
| M6 Reference runs against the original | in progress: 4 of 7 golden runs match, 3 have open differences |
| M7 Polish | mostly done: the macOS app bundle is not self-contained yet |

## M0: repo skeleton, extraction, symbols: done
- The extractor verifies the original files by hash, and its unpacker matches the Python reference tools byte for byte. Tests: `extract_verify`, `extract_verify_rejects_mismatch`, `unpack_matches_python_{game86,fs,llama,install}`.
- `docs/funcmap.csv` maps every original routine (435 rows) to its C counterpart. `tools/ghidra/` applies names and comments to the headless Ghidra project. game86 has no unnamed (`unk_`) routines or variables left.

## M1: extractor and title screen: done
- Everything is extracted at build time: sprites, font, levels, texts, samples, pictures, tables. Tests: `assets_formats`, `assets_game_data_files`, `assets_game_data_embedded`, `strings_roundtrip`, `levels_roundtrip`, `extract_lzss_oracle`.
- The title screen is checked by hash on VGA and EGA (`title_hash_vga`, `title_hash_ega` and the `_embedded` variants). The title decoder is diff-tested against fs.exe (`oracle.fs_title`).
- Web: `web_smoke_title_vga`, `web_smoke_title_ega` *(unverified)*.

## M2: renderer, timing, input, drawing: done
- Tests: `engine_selftest`, `oracle.engine_input`, `oracle.engine_pages`, `oracle.engine_palette`, `oracle.ega`.
- The original's drawing routines are diff-tested: `oracle.draw_basic`, `oracle.draw_wrappers`, `oracle.sprites`, `oracle.text`, `oracle.scroller`, `oracle.hud`, `oracle.palette_pulse`, `oracle.flash`.
- The attract and title screen code is diff-tested too: `oracle.attract`, `oracle.story_skip`.

## M3: game core: done
- The state machine, level scripts, player, shots and waves are diff-tested: `oracle.script`, `oracle.player`, `oracle.shots`, `oracle.wave_core`, `oracle.wave_screens`, `oracle.movement`, `oracle.rng`, `oracle.score`, `oracle.pause`, `oracle.continue`.
- The real executable starts a game: `llamatron_to_game`, `llamatron_title_phase`, `llamatron_visual_walk_vga`.

## M4: all enemies and levels: done
- game86 has 381 rows in funcmap: 362 diff-tested, 14 ported, 5 marked todo. Of the 5:
  - `main` (`0010`): its parts are tested.
  - `sound_toggle` (`1514`): the row is stale, since the port has `game_sound_toggle`.
  - `line_walk` (`32fe`): diff-tested in `438b760`, after the run here.
  - `sprite_draw_all_pages` (`424c`): dead code.
  - `mem_shrink` (`e788`): DOS memory, not ported on purpose.
- All 41 enemy init handlers and all 46 per-tick (think) handlers are diff-tested: `oracle.enemy_init`, `oracle.think_basic`, `oracle.think_herders`, `oracle.think_llamas`, `oracle.think_orbiters`, `oracle.think_w5a`, `oracle.w5a_types`, `oracle.hit`, `oracle.pickups`, `oracle.anim`, `oracle.entity_animate`, `oracle.entity_loop`, `oracle.spawn_clobber`.
- `oracle.level_sweep` plays each of the 100 levels for 3000-5000 ticks and checks that every live entity uses ported code and stays drawable. All 100 pass.
- Game over and hi-scores: `oracle.game_over`, `oracle.hiscore`, `oracle.hiscore_save`. The scores file survives a restart: `llamatron_scores_{reset,low,play,reload}`.

## M5: sound: done
- Tests: `snd_selftest` and `snd_hash_title` (the title sample).
- In-game sound through the mixer is in `3a35a42`. Web: `web_game_sound`, `web_smoke_title_sound` *(unverified)*.

## M6: reference runs: in progress
The original game ran in DOSBox-X for seven scripted sessions, and their recorded hashes all still match. The port replayed the same inputs:

| Run | Result |
|---|---|
| attract, game_over_hiscore, walk_vga, walk_ega | identical on every compared tick, apart from known one-tick timing shifts (DEVIATIONS #20) |
| death_continue | differs from tick 800: after the continue, the port's first play tick lands one timer tick later, so a llama moves on the other tick parity (owner Keel) |
| two_player | differs from tick 1150: an input question in the test harness (the lead), plus shot and entity positions at 1450 (Keel) |
| warp | differs from tick 400: the same input question (the lead), plus entity positions at 450 (Keel) |

- These differences are tracked in `tests/dosbox/test_reference_golden.py` (TRIAGE).
- Commit `de4c000` adds an input-shift fix on the test side. It came after the run here, so its effect is unverified.
- Native and web parity: `web_llamatron_parity` *(unverified)*.

## M7: polish: mostly done
- Done:
  - options screen (F2 on the title): `options_selftest`, `llamatron_options_*`;
  - display scaling and full screen (`7b77cdb`, web tests unverified);
  - gamepads: `controls_selftest`;
  - pause: `oracle.pause`. Quit is implemented but has no test of its own.
- Every deliberate difference from the original is listed in `docs/DEVIATIONS.md` (26 rows, audited against the original code this week).
- Open:
  - the macOS app still needs Homebrew's SDL2 (`src/platform/README.md`, TODO);
  - four DEVIATIONS rows (#8, #9, #10, #19) still lack a test of the exact case where the port differs; asked of Thorn and Keel.

## How to verify
```sh
# native build and all tests (needs your copy of the original game in original/)
cmake --preset macos-native -DLLAMATRON_ORIGINAL_DIR=$PWD/original
cmake --build build
ctest --test-dir build -j8

# the DOSBox-X reference runs (slow, about 5 minutes; needs dosbox-x and nasm)
LLAMATRON_REPLAY=$PWD/build/tests/llamatron_replay \
  tools/harness/.venv/bin/python -m pytest -s tests/dosbox/test_reference_golden.py
```
With `-s`, the reference test prints a per-run report: which game variables differ, from which tick, and who is on it.
