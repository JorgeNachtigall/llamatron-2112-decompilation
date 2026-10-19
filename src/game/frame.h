/*
 * frame.h - one frame of the game's frame loop (game86 frame_run 1e7b:013f).
 */
#ifndef LLAMATRON_GAME_FRAME_H
#define LLAMATRON_GAME_FRAME_H

#include <stdbool.h>

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read the controls, quit on 'Q', run the draw hook into the page being drawn (dirty
 * recording on), wait for the retrace, the flash step, then show the page and erase what
 * the hook drew on the other one. True when the flash step reloaded the game palette, which
 * in the original costs one more retrace (palette_set 1000:1be6 waits first).
 * game86 1e7b:013f */
bool frame_run(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
