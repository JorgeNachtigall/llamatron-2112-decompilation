/*
 * script.h - level scripts: per-level parameters and the wave's spawn list.
 *
 * A level script (levels.dat, docs: tools/extract/FORMATS.md) is a list of parameter ops,
 * the wave type and a list of (enemy type, count - 1) pairs. Starting a level runs all ops at
 * once (game86 1000:020b..0243); the spawn list is then consumed one pair at a time while
 * the wave runs (1000:3d09).
 */
#ifndef LLAMATRON_GAME_SCRIPT_H
#define LLAMATRON_GAME_SCRIPT_H

#include <stdbool.h>

#include "formats.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the parameter ops of the current level (wrapping to level 0 past the last script),
 * sets the wave type and points the spawn cursor at the level's list.
 * game86 1000:020b..0243, 0455..045d */
void script_start_level(GameState *game, const LevelSet *levels);

/* One parameter op. game86 1000:0392..0454 (the CS:0245 handlers) */
void script_run_op(GameState *game, const LevelOp *op);

/* Takes the next (type, count) pair from the spawn cursor into spawn_type/spawn_remaining;
 * false (and spawns_done set) at the end of the list. game86 1000:3d09 */
bool script_next_spawn_pair(GameState *game, const LevelSet *levels);

/* The warp-in sprite of this wave's spawn effects, by level & 3. game86 1000:5350 */
void level_select_variant(GameState *game, const LevelSet *levels);

/* Quirk: enemy_shot_init clears the "parent's" words +8/+0A through SI, but on the think_13
 * path (1000:10b0) SI still holds think_13's code offset, so the original zeroes two spawn
 * pairs of the level script that happens to sit at that DS offset (levels.dat
 * clobber_10b0). From then on, for the rest of the process, those pairs read as
 * (type 0, count 0). game86 1000:123c..1241 via 1000:10ea */
void script_clobber_spawns_10b0(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
