/*
 * tick.h - the play tick: what every 50 Hz timer tick does while a wave runs.
 */
#ifndef LLAMATRON_GAME_TICK_H
#define LLAMATRON_GAME_TICK_H

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The play tick hook. game86 1000:1562..1a38 */
void play_tick_hook(GameState *game);

/* The spawn step that ends every play tick: first the warp-in effects of a new wave, then
 * one enemy of the current spawn pair per tick, then the next pair. game86 1000:1d40..1dcb */
void wave_spawn_tick(GameState *game);

/* After a death: rebuild the current player's roster (kind, count - 1) from the live
 * enemies so the wave restarts with what was left. game86 1000:1ced */
void wave_spawn_enemies(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
