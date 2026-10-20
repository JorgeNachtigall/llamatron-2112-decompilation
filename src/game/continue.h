/*
 * continue.h - the continue prompt after the last life: a label, the continues left and a
 * seconds countdown; fire continues the game.
 *
 * game86 runs it as hooks: main_sm calls continue_begin, then installs continue_tick_hook /
 * continue_draw_hook (TICK_HOOK_CONTINUE / DRAW_HOOK_CONTINUE, 1000:07e2).
 */
#ifndef LLAMATRON_GAME_CONTINUE_H
#define LLAMATRON_GAME_CONTINUE_H

#include "draw.h"
#include "formats.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the prompt's pictures come from: sprite refs by index (the digits) and the g3 title
 * shapes by name (continue_art_default() uses the game data). */
typedef const GfxSprite *(*ContinueSpriteLookup)(unsigned index);
typedef const GfxSprite *(*ContinueShapeLookup)(TitleShapeId id);
typedef struct ContinueArt {
    ContinueSpriteLookup sprite_ref;    /* game86 218C:0000 + 2n */
    ContinueShapeLookup shape;          /* game86 g3 segment 30EB:0000 + 2n */
} ContinueArt;

const ContinueArt *continue_art_default(void);

/* Start the countdown at 9 seconds. game86 1000:07d6..07dc */
void continue_begin(GameState *game);

/* Tick hook: counts the seconds down; fire sets continue_ticks to -1 (continue) and the tick
 * hook to TICK_HOOK_NONE. game86 1000:0816 */
void continue_tick_hook(GameState *game);

/* Draw hook, into `frame`: the label, the continues left and, while it is >= 0, the seconds.
 * game86 1000:0894 */
void continue_draw_hook(GameState *game, const DrawTarget *frame, const ContinueArt *art);

#ifdef __cplusplus
}
#endif

#endif
