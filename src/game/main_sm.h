/*
 * main_sm.h - game86's top-level flow (1000:00c2..0980) as a per-frame state machine.
 *
 * The original is straight-line code with blocking loops: start-up, the title and attract
 * mode, a new game, the level script, a wave, its outcome, the continue prompt and game over.
 * Each loop waits for the vertical retrace, through frame_run (1e7b:013f), wait_vsync
 * (1000:e5f3) or palette_set (1000:1be6, which waits first). Here every such wait ends a
 * main_sm_frame() call, so the host gets control back once per display frame; the code
 * between two waits runs inside one call, in the original's order.
 */
#ifndef LLAMATRON_GAME_MAIN_SM_H
#define LLAMATRON_GAME_MAIN_SM_H

#include "draw.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The flow from video_init on (game86 1000:00c2). Call once, after game_init's setup. */
void main_sm_start(GameState *game);

/* Run the flow up to its next wait for the vertical retrace. */
void main_sm_frame(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
