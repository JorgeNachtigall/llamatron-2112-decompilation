/*
 * attract.h - the title screen: the letters dropping in, the logo, the option menu (players,
 * control mode, shared start), the bouncing llamas, and the title tune.
 *
 * game86 runs it as hooks: main_sm installs title_tick_hook / title_draw_hook (and
 * title_tune_tick while the tune plays) where the original writes its tick and draw hook
 * pointers.
 */
#ifndef LLAMATRON_GAME_ATTRACT_H
#define LLAMATRON_GAME_ATTRACT_H

#include <stdint.h>

#include "draw.h"
#include "formats.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the title's pictures come from: sprite-bank sprites by sprite number and the g3
 * title shapes by name (attract_art_default() uses the game data). */
typedef const GfxSprite *(*AttractSpriteLookup)(unsigned index);
typedef const GfxSprite *(*AttractShapeLookup)(TitleShapeId id);
typedef struct AttractArt {
    AttractSpriteLookup sprite;     /* game86 218C:001C + 2n */
    AttractShapeLookup shape;       /* game86 g3 segment 30EB:0000 + 2n */
} AttractArt;

const AttractArt *attract_art_default(void);

/* ---- The title tune ------------------------------------------------------------------- */

/* Silence the tune, forget a fire press and install title_tune_tick as the tick hook.
 * game86 1000:083b */
void title_tune_start(GameState *game);

/* title_tune_start, then play the tune from its start, a note every 0x24 ticks.
 * game86 1000:5c3f..5c54 */
void title_tune_begin(GameState *game);

/* Tick hook while the tune plays: notes fire_pressed, and every `period` ticks plays the
 * next {rate, sample} of the list, starting over at `restart` after the end mark.
 * game86 1000:084e */
void title_tune_tick(GameState *game);

/* ---- The title screen ----------------------------------------------------------------- */

/* Build the title's background (outlined, with the two logo halves) and start the letters'
 * countdowns; the caller then reveals it (screen_reveal_bg). game86 1000:3ee0..3f6b */
void title_draw(GameState *game, const AttractArt *art);

/* Line the 32 title llamas up along a sine wave, each with its own speed. game86 1000:44d7 */
void attract_llamas_init(void);

/* Tick hook: letters dropping (with their landing sound), then the logo's countdown, then
 * the llamas bouncing. game86 1000:3f6f */
void title_tick_hook(GameState *game);

/* Draw hook, into the frame's page `frame` (and, where the original does, the other pages):
 * the letters, the option menu read from controller 1, then by title_timer the logo zooming
 * in, the finished logo, or the menu labels and the llamas. game86 1000:402c */
void title_draw_hook(GameState *game, const DrawTarget *frame, const AttractArt *art);

/* The title llamas (in game86 they live in the entity table's first bytes, 1000:44d7..4645).
 * Positions and speeds are 16.16 fixed point; height counts up from the ground (y 0xc0). */
enum { ATTRACT_LLAMAS = 32 };
typedef struct AttractLlama {
    int32_t x, height;
    int32_t vx, vy;                 /* vy is subtracted from height, then gravity adds to it */
    int16_t sprite_base;            /* + walk frame, + 6 when moving right */
} AttractLlama;

/* For tests: the llamas' state. */
AttractLlama *attract_llamas(void);

/* Leaving the title (new_game 018f, from the title or the demo): the title llamas stay in
 * entity slots 0..8 as the original's 18-byte llama records leave those bytes, so fields
 * that a later spawn does not set keep that residue as in the original. */
void attract_end(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
