/*
 * text.c - the game's text engine (see text.h).
 */
#include "text.h"

#include <stddef.h>

#include "game_data.h"

static const GfxFont *s_font;
static TextSpriteLookup s_sprite_lookup;

enum { INLINE_SPRITE_DROP = 4 };   /* \s sprites are centred 4 px below the pen's y */

enum {
    SCREEN_LAST_X = GFX_WIDTH - 1,
    SCREEN_CENTRE_X = GFX_WIDTH / 2,
    GLYPH_WIDTH = 16,
};

void text_set_font(const GfxFont *font)
{
    s_font = font;
}

const GfxFont *text_font(void)
{
    return s_font;
}

void text_set_sprite_lookup(TextSpriteLookup lookup)
{
    s_sprite_lookup = lookup;
}

static bool game_sprite_lookup(unsigned number, const GfxSprite **sprite, int *nominal_width)
{
    const SpriteBank *bank = asset_sprite_bank(SPRITE_BANK_GAME);
    const GfxSprite *found = asset_sprite_number_gfx(number);
    if (!bank || !found)
        return false;
    *sprite = found;
    *nominal_width = bank->frames[found - bank->sprites].nominal_width;
    return true;
}

void text_use_game_assets(void)
{
    text_set_font(asset_font_gfx());
    text_set_sprite_lookup(game_sprite_lookup);
}

static const GfxGlyph *glyph_for(unsigned char ch)
{
    if (!s_font || ch < s_font->first_char || ch - s_font->first_char >= s_font->glyph_count)
        return NULL;
    return &s_font->glyphs[ch - s_font->first_char];
}

static int glyph_advance(unsigned char ch)
{
    const GfxGlyph *g = glyph_for(ch);
    return g ? g->advance : 0;
}

int text_glyph_advance(unsigned char ch)
{
    return glyph_advance(ch);
}

/* ---- Glyphs ------------------------------------------------------------------------ */

/* The original walks the target cell by cell. Where a row is clipped at the left or right
 * edge it moves that cursor by a different amount than it draws, so the following rows of
 * such a glyph land shifted left (into the previous screen row). Each step below is one of
 * the original's cell tests; `cell` is its cursor. */

typedef struct GlyphPen {
    const DrawTarget *target;
    uint8_t clear_planes, set_planes;
    int32_t cell;
} GlyphPen;

static void pen_paint(const GlyphPen *pen, int32_t cell, uint8_t bits)
{
    draw_cell_paint(pen->target, cell, bits, pen->clear_planes, pen->set_planes);
}

/* A byte-aligned row: the glyph's two bytes go to two cells. game86 1000:5617..56bd */
static void glyph_row_aligned(GlyphPen *pen, int x, uint16_t bits)
{
    uint8_t left = (uint8_t)(bits >> 8), right = (uint8_t)bits;
    if (x < 0) {
        pen->cell--;
    } else if (x >= SCREEN_LAST_X) {
        return;                         /* rest of the row skipped */
    } else {
        pen_paint(pen, pen->cell, left);
    }
    if (x + 8 < 0) {
        pen->cell++;
    } else if (x + 8 > SCREEN_LAST_X) {
        return;
    } else {
        pen->cell++;
        pen_paint(pen, pen->cell, right);
        pen->cell++;
    }
}

/* An unaligned row: each byte is shifted left by `shift` bits into a pair of cells, giving
 * four pieces, each tested against the screen edges on its own. game86 1000:56c3..5818 */
static void glyph_row_shifted(GlyphPen *pen, int x, int shift, uint16_t bits)
{
    uint16_t left = (uint16_t)((bits >> 8) << shift);
    uint16_t right = (uint16_t)((bits & 0xff) << shift);
    const struct {
        int edge;          /* x offset the original tests for this piece */
        uint8_t bits;
        int target;        /* 0: paint at the cursor; 1: paint one cell right, then move there */
    } pieces[4] = {
        { 0, (uint8_t)(left >> 8), 0 },
        { 2, (uint8_t)left, 1 },
        { 8, (uint8_t)(right >> 8), 0 },
        { 16, (uint8_t)right, 1 },
    };
    for (int i = 0; i < 4; i++) {
        int edge_x = x + pieces[i].edge;
        if (edge_x <= 0) {
            pen->cell += pieces[i].target ? 1 : -1;
        } else if (edge_x > SCREEN_LAST_X) {
            return;
        } else if (pieces[i].target) {
            pen_paint(pen, pen->cell + 1, pieces[i].bits);
            pen->cell++;
        } else {
            pen_paint(pen, pen->cell, pieces[i].bits);
        }
    }
}

/* game86 1000:559f */
void text_draw_glyph(const DrawTarget *target, const TextState *state, unsigned char ch,
                     int x, int y)
{
    const GfxGlyph *glyph = glyph_for(ch);
    if (!glyph || glyph->rows <= 0 || !glyph->bits)
        return;
    int rows = glyph->rows;
    const uint16_t *bits = glyph->bits;
    if (x > SCREEN_LAST_X || x + GLYPH_WIDTH < 0 || y + rows - 1 < 0)
        return;
    if (y < 0) {                        /* top clip only: no bottom clip in the original */
        bits += -y;
        rows -= -y;
        y = 0;
    }

    DrawCellPos pos = draw_cell_pos(x, y);
    GlyphPen pen = { target, state->clear_planes, state->color, pos.cell };
    for (int row = 0; row < rows; row++) {
        if (draw_cell_pos_aligned(pos))
            glyph_row_aligned(&pen, x, bits[row]);
        else
            glyph_row_shifted(&pen, x, pos.shift, bits[row]);
        pen.cell += DRAW_CELLS_PER_ROW - 2;   /* a fully drawn row advances 2 cells */
    }
}

/* game86 1000:54f6 */
void text_draw_char(const DrawTarget *target, TextState *state, unsigned char ch)
{
    text_draw_glyph(target, state, ch, state->x, state->y);
    state->x = (int16_t)(state->x + glyph_advance(ch));
}

/* game86 1000:54d4 */
void text_set_color(TextState *state, uint8_t color)
{
    state->color = color & 0x0f;
    state->clear_planes = state->opaque ? (uint8_t)(~color & 0x0f) : 0;
}

/* game86 1000:5537 */
void text_draw_centred(const DrawTarget *target, const TextState *state, const char *text,
                       int y)
{
    int16_t width = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; s++)
        width = (int16_t)(width + glyph_advance(*s));
    int16_t x = (int16_t)(SCREEN_CENTRE_X - (width >> 1));
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        text_draw_glyph(target, state, *s, x, y);
        x = (int16_t)(x + glyph_advance(*s));
    }
}

/* ---- Strings ----------------------------------------------------------------------- */

/* game86 1000:5432..5478. The original switches dirty recording off here and leaves it off
 * (the frame loop turns it back on for the next frame), so sprites drawn after a \s in the
 * same frame are not recorded or clipped either. Returns the pen x after the sprite. */
static int16_t draw_inline_sprite(const DrawTarget *target, unsigned number, int16_t x, int16_t y)
{
    const GfxSprite *sprite = NULL;
    int nominal_width = 0;
    if (!s_sprite_lookup || !s_sprite_lookup(number, &sprite, &nominal_width))
        return x;
    draw_set_dirty_recording(false);
    draw_sprite(target, sprite, x + (nominal_width >> 1), y + INLINE_SPRITE_DROP);
    return (int16_t)(x + nominal_width);
}

static bool reaches_wrap(int16_t x)
{
    return (int16_t)(x - TEXT_WRAP_X) >= 0;   /* the original's signed compare */
}

/* Does the word starting with `ch` (rest at `rest`) fit on the line from x? The word ends
 * at a space, an escape or a 0 byte. game86 1000:5394..53b9 */
static bool word_fits(int16_t x, unsigned char ch, const unsigned char *rest)
{
    for (;;) {
        x = (int16_t)(x + glyph_advance(ch));
        if (reaches_wrap(x))
            return false;
        ch = *rest++;
        if (ch == 0 || ch == ' ' || ch == TEXT_ESCAPE)
            return true;
    }
}

/* game86 1000:536f */
const char *text_print(const DrawTarget *target, TextState *state, const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    int16_t x = state->x, y = state->y;
    state->margin = x;
    for (;;) {
        unsigned char ch = *s++;
        if (ch == 0)
            continue;
        if (ch != TEXT_ESCAPE && !word_fits(x, ch, s)) {
            /* Wrap before the word; spaces at the start of the new line are dropped. */
            x = state->margin;
            y = (int16_t)(y + TEXT_LINE_STEP);
            while (ch == ' ')
                ch = *s++;
        }
        if (ch != TEXT_ESCAPE) {
            text_draw_glyph(target, state, ch, x, y);
            x = (int16_t)(x + glyph_advance(ch));
            if (reaches_wrap(x)) {
                y = (int16_t)(y + TEXT_LINE_STEP);
                x = state->margin;
            }
            continue;
        }

        unsigned char code = *s++;
        switch (code) {
        case 'q':
            text_draw_centred(target, state, (const char *)s, y);
            while (*s++)
                ;
            break;
        case 'r':
            x = state->margin;
            y = (int16_t)(y + TEXT_LINE_STEP);
            break;
        case 'e':
            state->x = x;
            state->y = y;
            return (const char *)s;
        case 's':
            x = draw_inline_sprite(target, (unsigned)(s[0] << 8 | s[1]), x, y);
            s += 2;
            break;
        case 'u':
            y = (int16_t)(y - TEXT_LINE_STEP);
            break;
        case 'd':
            y = (int16_t)(y + TEXT_LINE_STEP);
            break;
        case 'l':
            x = (int16_t)(x - TEXT_LINE_STEP);
            break;
        case 'i':
            x = (int16_t)(x + TEXT_LINE_STEP);
            break;
        case 'g':
            state->margin = x = s[0];
            y = (int16_t)(y + (int8_t)s[1]);
            s += 2;
            break;
        case 'c':
            text_set_color(state, *s++);
            break;
        default:
            break;
        }
    }
}

/* game86 1000:5363 */
const char *text_print_home(const DrawTarget *target, TextState *state, const char *text)
{
    state->x = 0;
    state->y = 0;
    return text_print(target, state, text);
}
