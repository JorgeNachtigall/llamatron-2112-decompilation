/*
 * hud.c - the in-game status line (see hud.h).
 */
#include "hud.h"

#include "game_data.h"

#include <stddef.h>

#include "score.h"

static const HudGlyphs *s_glyphs;

static const GfxPageId HUD_PAGES[] = { GFX_PAGE_0, GFX_PAGE_1, GFX_PAGE_BACKGROUND };

enum {
    LIVES_SHOWN_MAX = 5,
    LEVEL_DIGIT_BASE = 10,
};

/* g3 title shapes (table offset / 2) of the control labels. game86 1000:59ab */
enum {
    SHAPE_ONE_PLAYER = 7,               /* 30EB:000E */
    SHAPE_TWO_PLAYERS_DOUBLE = 8,       /* 30EB:0010 OPT_DOUBLE_CONTROLLER */
    SHAPE_TWO_PLAYERS_SHARED = 9,       /* 30EB:0012 */
    SHAPE_TURN_P1 = 10,                 /* 30EB:0014 */
    SHAPE_TURN_P2 = 11,                 /* 30EB:0016 */
};
static const struct { int x, y; } CONTROL_LABEL_POS = { 0x9c, 0x23 };
static const struct { int x, y; } TURN_LABEL_POS = { 0x9b, 0x0f };

void hud_set_glyphs(const HudGlyphs *glyphs)
{
    s_glyphs = glyphs;
}

void hud_use_game_assets(void)
{
    hud_set_glyphs(asset_hud_glyphs());
}

static int current_player(const GameState *game)
{
    return (game->options & OPT_CURRENT_IS_P2) ? 1 : 0;
}

/* game86 1000:50ef..51c5: clear the cell on all pages, then set the glyph's pixels. */
static void draw_glyph(int cell, const uint8_t rows[HUD_GLYPH_ROWS], uint8_t color)
{
    for (size_t p = 0; p < sizeof HUD_PAGES / sizeof HUD_PAGES[0]; p++) {
        DrawTarget target = draw_target_page(HUD_PAGES[p]);
        for (int row = 0; row < HUD_GLYPH_ROWS; row++) {
            int32_t row_cell = cell + row * DRAW_CELLS_PER_ROW;
            draw_cell_paint(&target, row_cell, 0xff, 0x0f, 0);
            draw_cell_paint(&target, row_cell, rows[row], 0x0f, color);
        }
    }
}

/* game86 1000:520f */
void hud_clear_cell(int cell)
{
    static const uint8_t blank[HUD_GLYPH_ROWS];
    draw_glyph(cell, blank, 0);
}

/* game86 1000:50dc */
void hud_draw_digit(int cell, unsigned digit)
{
    if (s_glyphs && digit < HUD_DIGIT_COUNT)
        draw_glyph(cell, s_glyphs->digits[digit], HUD_DIGIT_COLOR);
}

/* game86 1000:51c6 */
void hud_draw_player_label(int player, int cell)
{
    if (s_glyphs && player >= 0 && player < HUD_PLAYER_LABEL_COUNT)
        draw_glyph(cell, s_glyphs->player_label[player], HUD_LABEL_COLOR);
}

/* game86 1000:51dc */
void hud_draw_lives(int player, int lives)
{
    int first = player ? HUD_LIVES_CELL_P2 : HUD_LIVES_CELL_P1;
    for (int i = 0; i < HUD_LIVES_CELLS; i++)
        hud_clear_cell(first + i);
    int shown = lives - 1;   /* the life in play has no label */
    if (shown > LIVES_SHOWN_MAX)
        shown = LIVES_SHOWN_MAX;
    for (int i = 0; i < shown; i++)
        hud_draw_player_label(player, first + i);
}

/* game86 1000:52b0 */
void hud_draw_level(int player, int level)
{
    int cell = player ? HUD_LEVEL_CELL_P2 : HUD_LEVEL_CELL_P1;
    unsigned shown = (uint16_t)(level + 1);
    unsigned tens = shown / LEVEL_DIGIT_BASE, ones = shown % LEVEL_DIGIT_BASE;
    if (tens >= LEVEL_DIGIT_BASE) {
        hud_draw_digit(cell - 1, 1);
        tens = 0;
    }
    hud_draw_digit(cell, tens);
    hud_draw_digit(cell + 1, ones);
}

/* game86 1000:52e7 */
void hud_draw_score(const uint8_t digits[SCORE_DIGITS], int cell)
{
    int first = 0;
    while (first < SCORE_DIGITS && digits[first] == 0)
        first++;
    if (first == SCORE_DIGITS) {
        hud_draw_digit(cell + SCORE_DIGITS - 2, 0);
        hud_draw_digit(cell + SCORE_DIGITS - 1, 0);
        return;
    }
    for (int i = first; i < SCORE_DIGITS; i++)
        hud_draw_digit(cell + i, digits[i]);
}

/* game86 1000:0309 */
void hud_refresh(const GameState *game)
{
    int player = current_player(game);
    hud_draw_level(player, game->player.level);
    hud_draw_lives(player, game->player.lives);
    uint8_t digits[SCORE_DIGITS];
    score_to_digits(game->score[player], digits);
    hud_draw_score(digits, player ? HUD_SCORE_CELL_P2 : HUD_SCORE_CELL_P1);
}

/* game86 1000:59ab; both through g3_draw 1000:42da with g_draw_flags 0xff (plain draw). */
void hud_draw_control_labels(const DrawTarget *target, uint8_t options, HudShapeLookup shape)
{
    if (!shape)
        return;
    unsigned control = SHAPE_ONE_PLAYER;
    if (options & OPT_TWO_PLAYERS)
        control = (options & OPT_DOUBLE_CONTROLLER) ? SHAPE_TWO_PLAYERS_DOUBLE
                                                    : SHAPE_TWO_PLAYERS_SHARED;
    const GfxSprite *sprite = shape(control);
    if (sprite)
        draw_sprite(target, sprite, CONTROL_LABEL_POS.x, CONTROL_LABEL_POS.y);
    sprite = shape((options & OPT_CURRENT_IS_P2) ? SHAPE_TURN_P2 : SHAPE_TURN_P1);
    if (sprite)
        draw_sprite(target, sprite, TURN_LABEL_POS.x, TURN_LABEL_POS.y);
}
