/*
 * hud.h - the in-game status line: score, lives and level in 8x8 glyphs along the top rows.
 *
 * HUD cells are 8-pixel cells of screen row 0..7 (cell n covers x = 8n..8n+7). They are drawn
 * on all three pages at once, so neither the restore pass nor the background copies erase
 * them. A cell is cleared to colour 0 and the glyph's pixels set to the glyph colour (1 for
 * digits, 4 for the player-life labels).
 *
 * EGA note: the original clears a cell with plain writes of 0 under whatever bit mask the
 * last draw left, so the result is colour 0 only when the latches hold 0 or the bit mask is
 * 0xff. That is the case for every refresh in practice (after the restore pass or a page
 * copy the bit mask is 0xff, and later cells reuse the zero latches of the cell before), and
 * it is the only case reproduced here.
 */
#ifndef LLAMATRON_GAME_HUD_H
#define LLAMATRON_GAME_HUD_H

#include <stdint.h>

#include "draw.h"
#include "formats.h"
#include "gfx.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HUD_SCORE_CELL_P1 = 0x00,
    HUD_SCORE_CELL_P2 = 0x20,
    HUD_LIVES_CELL_P1 = 0x09,
    HUD_LIVES_CELL_P2 = 0x1a,
    HUD_LIVES_CELLS = 5,
    HUD_LEVEL_CELL_P1 = 0x10,
    HUD_LEVEL_CELL_P2 = 0x16,
    HUD_DIGIT_COLOR = 1,
    HUD_LABEL_COLOR = 4,
};

void hud_set_glyphs(const HudGlyphs *glyphs);
/* The game's glyphs (asset_hud_glyphs) from the loaded game data. */
void hud_use_game_assets(void);

/* Digit `digit` (0..9) in HUD cell `cell`. game86 1000:50dc */
void hud_draw_digit(int cell, unsigned digit);
/* The life label of `player` (0 = P1, the one whose turn it is). game86 1000:51c6 */
void hud_draw_player_label(int player, int cell);
/* Clear a HUD cell. game86 1000:520f */
void hud_clear_cell(int cell);

/* One label per spare life (lives - 1, at most 5) of `player`. game86 1000:51dc */
void hud_draw_lives(int player, int lives);
/* level + 1 in two digits; 100 and over shows a leading 1 one cell to the left and 0 for the
 * tens. game86 1000:52b0 */
void hud_draw_level(int player, int level);
/* 8 score digits (0..9, most significant first) from `cell`; leading zeros are not drawn
 * (their cells keep what they had) and a zero score is "00" in the last two cells.
 * game86 1000:52e7 */
void hud_draw_score(const uint8_t digits[SCORE_DIGITS], int cell);
/* Level, lives and score of the player whose turn it is (OPT_CURRENT_IS_P2). game86 1000:0309 (the caller tests and
 * clears g_hud_dirty) */
void hud_refresh(const GameState *game);

/* The control-method and current-player labels (g3 title shapes) drawn by the play frame and
 * the name-entry screen. `shape(id)` returns the TitleShapeId `id` (asset_title_shape, or a
 * test's own pictures). game86 1000:59ab */
typedef const GfxSprite *(*HudShapeLookup)(unsigned g3_index);
void hud_draw_control_labels(const DrawTarget *target, uint8_t options,
                             HudShapeLookup shape);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_HUD_H */
