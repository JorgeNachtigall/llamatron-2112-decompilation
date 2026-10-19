/*
 * pause.h - the pause: keys while the game is paused (P pauses it, in the play tick hook,
 * which installs TICK_HOOK_PAUSE at 1000:159c).
 */
#ifndef LLAMATRON_GAME_PAUSE_H
#define LLAMATRON_GAME_PAUSE_H

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tick hook while paused. G puts play_draw_hook back as the draw hook; O resumes (play draw
 * and tick hooks, pause_message cleared); S toggles the sound once per press; any other last
 * key keeps playing as a sample (its make code & 0x1f, at most 0x1b) whenever the driver is
 * idle. game86 1000:14d0, incl. pause_key_sample 1000:1538 */
void pause_tick_hook(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
