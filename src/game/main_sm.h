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

/* ---- For the oracle tests only ---- */

/* The flow points a test can start from or stop at. */
typedef enum MainSmTestState {
    MAIN_SM_TEST_OTHER,
    MAIN_SM_TEST_GAME_OVER,         /* 1000:08ee */
    MAIN_SM_TEST_NAME_ENTRY,        /* 5e47: a score made the table, the name entry is next */
    MAIN_SM_TEST_GAME_OVER_TABLE,   /* 094f: the checks are done, the table is next */
} MainSmTestState;

/* Restart the flow at `state` (only MAIN_SM_TEST_GAME_OVER; anything else starts at boot). */
void main_sm_enter(GameState *game, MainSmTestState state);

/* One state's code: true when the flow goes straight on to the next state, false when it
 * waits for the retrace (where main_sm_frame would return). */
bool main_sm_step(GameState *game);

/* Where the flow is, as far as the test points tell. */
MainSmTestState main_sm_state(void);

/* The hi-score name entry's hooks, on the entry main_sm keeps. game86 1000:5b06, 5ac4 */
void main_sm_name_entry_tick(GameState *game);
void main_sm_name_entry_draw(GameState *game, const DrawTarget *frame);

#ifdef __cplusplus
}
#endif

#endif
