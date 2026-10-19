/*
 * bullets.h - the players' shots (GameState.shots, 15 records at DS:68C9): clearing, moving
 * and hitting entities each play tick, drawing each frame.
 */
#ifndef LLAMATRON_GAME_BULLETS_H
#define LLAMATRON_GAME_BULLETS_H

#include "draw.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Free all shot slots. game86 1000:07b7 */
void shots_clear(GameState *game);

/* Move every live shot one step; at the playfield edge it dies or, with shots_bounce, is
 * reflected. A shot that moved checks the entities for a hit on every other tick and calls
 * the hit handler of the first one it touches (every solid one while smart_bomb is set).
 * game86 1000:0c28 */
void shots_update(GameState *game);

/* Draw every live shot into `target`. game86 1000:1499 */
void shots_draw(const GameState *game, const DrawTarget *target);

#ifdef __cplusplus
}
#endif

#endif
