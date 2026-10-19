/*
 * player.h - the players' ships: steering and aim from the controllers (or the droid), the
 * position trails, firing, and the per-player state swap between turns.
 */
#ifndef LLAMATRON_GAME_PLAYER_H
#define LLAMATRON_GAME_PLAYER_H

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-tick ship update, called by the play tick: P2 from the second stick or the droid, P1
 * from the first controller (or copied from the droid in demo mode), the trails, then P1's
 * movement. game86 1000:16c1..1a00 */
void players_tick(GameState *game);

/* Fires a volley from each ship every 9th tick once the wave's spawns are out (3-way spread
 * with a non-zero Player.score_scale, alternating between the ships in two-player mode).
 * game86 1000:0981 */
void player_fire(GameState *game);

/* The droid (P2 with two players on one controller) flies to the first llama, or to the
 * companion, which sits at the end of P1's llama chain (a random spot in demo mode).
 * game86 1000:480e */
void companion_update(GameState *game);

/* Exchanges the two players' blocks and flips OPT_CURRENT_IS_P2. game86 1000:5317 */
void player_swap_state(GameState *game);

/* With OPT_SHARED_START, P2 starts as a copy of the current player. game86 1000:5336 */
void player_copy_state(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
