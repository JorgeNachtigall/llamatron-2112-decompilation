/*
 * draw_play.h - what the game draws each frame while a wave is played, and the frame's
 * screen-flash step.
 */
#ifndef LLAMATRON_GAME_DRAW_PLAY_H
#define LLAMATRON_GAME_DRAW_PLAY_H

#include "draw.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The play frame (game86 draw hook 1000:1a3b), drawn into `target`, the page being drawn,
 * with dirty recording as the frame loop left it: the control labels while the wave is
 * starting, the shots, the entities, the pause picture, then each ship with its shield and
 * rescue marker (scaled in and out while it respawns), or in one-stick two-player games the
 * companion llama instead of the second ship. Sets frame_drawn. */
void play_draw_hook(GameState *game, const DrawTarget *target);

/* The frame loop's flash step, only with flash_mode 2: every 30th frame the game palette is
 * set again, and while a flash list runs colour 0 shows its next colour (black at its end,
 * which also clears flash_list). Whoever starts a list sets flash_pos to 0.
 * game86 frame_run 1e7b:0196..0212 */
void draw_flash_step(GameState *game);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_DRAW_PLAY_H */
