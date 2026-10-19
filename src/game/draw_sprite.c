/*
 * draw_sprite.c - the sprite blitters (see draw.h).
 *
 * A sprite row is a sequence of 8-pixel cells (two per 16-pixel column). Drawn at an x that
 * is a multiple of 8, each cell lands on one screen cell; otherwise each cell is split into a
 * "high" part (its left pixels, ending in the cursor cell) and a "low" part (the rest,
 * starting the next cell). Near the screen edges the original tests every part on its own
 * and moves its cell cursor in its own way; the functions below follow those tests exactly,
 * which is what makes partly visible sprites come out like the original's.
 */
#include "draw.h"

#include <stddef.h>

enum {
    SCREEN_LAST_X = GFX_WIDTH - 1,
    SCREEN_LAST_Y = GFX_HEIGHT - 1,
    COLUMN_PIXELS = 16,
    NARROW_FAST_MAX_X = 311,          /* aligned one-column draws up to here need no clipping */
    NARROW_SHIFTED_FAST_MAX_X = 303,  /* same for unaligned ones */
};

/* game86 cs_dirty_enable */
static bool s_dirty_recording;

void draw_set_dirty_recording(bool on)
{
    s_dirty_recording = on;
}

bool draw_dirty_recording(void)
{
    return s_dirty_recording;
}

/* ---- Cells ------------------------------------------------------------------------- */

typedef struct SpriteCell {
    uint8_t mask;            /* bit 7 = leftmost pixel; 1 = drawn */
    uint8_t colors[8];
} SpriteCell;

static SpriteCell sprite_cell(const GfxSprite *sprite, int row, int cell_index)
{
    SpriteCell cell = { 0, { 0 } };
    size_t base = (size_t)row * (size_t)sprite->width + (size_t)cell_index * DRAW_CELL_PIXELS;
    for (int i = 0; i < DRAW_CELL_PIXELS; i++) {
        uint8_t color = sprite->pixels[base + (size_t)i];
        bool opaque = sprite->mask ? sprite->mask[base + (size_t)i] != 0 : color != GFX_TRANSPARENT;
        if (opaque) {
            cell.mask |= (uint8_t)(0x80u >> i);
            cell.colors[i] = color;
        }
    }
    return cell;
}

/* The part of `src` that a left shift by `shift` (1..7) bits moves into the cursor cell
 * (high) or into the next cell (low). */
static SpriteCell shifted_part(const SpriteCell *src, int shift, bool high)
{
    SpriteCell out = { 0, { 0 } };
    for (int i = 0; i < DRAW_CELL_PIXELS; i++) {
        int j = high ? i - DRAW_CELL_PIXELS + shift : i + shift;
        if (j < 0 || j >= DRAW_CELL_PIXELS || !(src->mask & (0x80u >> j)))
            continue;
        out.mask |= (uint8_t)(0x80u >> i);
        out.colors[i] = src->colors[j];
    }
    return out;
}

static void blit(const DrawTarget *target, int32_t cell, const SpriteCell *c)
{
    draw_cell_blit(target, cell, c->mask, c->colors);
}

/* One 16-pixel column of a row, unaligned, near the screen edges: four parts, each tested
 * at x + 0, 8, 8, 16. Returns false where the original stops drawing the row (a part past
 * the right edge). game86 1000:aa29 (one column), 6568..672d (per column of a wide sprite),
 * e2e8 (one row of a sheared sprite) */
static bool column_shifted_clipped(const DrawTarget *target, int32_t *cursor, int x, int shift,
                                   const SpriteCell *left, const SpriteCell *right)
{
    const struct {
        int edge;
        SpriteCell part;
        bool into_next;      /* paint at cursor + 1 and move there (low parts) */
    } parts[4] = {
        { 0, shifted_part(left, shift, true), false },
        { 8, shifted_part(left, shift, false), true },
        { 8, shifted_part(right, shift, true), false },
        { 16, shifted_part(right, shift, false), true },
    };
    for (int i = 0; i < 4; i++) {
        int edge_x = x + parts[i].edge;
        if (edge_x <= 0) {
            *cursor += parts[i].into_next ? 1 : -1;
        } else if (edge_x > SCREEN_LAST_X) {
            return false;
        } else if (parts[i].into_next) {
            blit(target, *cursor + 1, &parts[i].part);
            (*cursor)++;
        } else {
            blit(target, *cursor, &parts[i].part);
        }
    }
    return true;
}

/* An unaligned column fully on screen: three cells. game86 1000:ac10 and the unrolled
 * versions (table 1000:abd1) */
static void column_shifted(const DrawTarget *target, int32_t cell, int shift,
                           const SpriteCell *left, const SpriteCell *right)
{
    SpriteCell parts[4] = {
        shifted_part(left, shift, true), shifted_part(left, shift, false),
        shifted_part(right, shift, true), shifted_part(right, shift, false),
    };
    blit(target, cell, &parts[0]);
    blit(target, cell + 1, &parts[1]);
    blit(target, cell + 1, &parts[2]);
    blit(target, cell + 2, &parts[3]);
}

/* ---- Dirty rectangles -------------------------------------------------------------- */

/* The original records {16-pixel words, rows, first cell}; an unaligned draw covers one
 * word more. */
void draw_record_dirty(const DrawTarget *target, int32_t cell, int words, int rows)
{
    if (!s_dirty_recording || target->page < 0)
        return;
    int32_t row = cell >= 0 ? cell / DRAW_CELLS_PER_ROW : -((-cell + DRAW_CELLS_PER_ROW - 1) / DRAW_CELLS_PER_ROW);
    int32_t column = cell - row * DRAW_CELLS_PER_ROW;
    gfx_mark_dirty((GfxPageId)target->page,
                   (GfxRect){ (int16_t)(column * DRAW_CELL_PIXELS), (int16_t)row,
                              (int16_t)(words * COLUMN_PIXELS), (int16_t)rows });
}

/* Clip a draw of `rows` rows of `width` pixels at (x, y) to the screen as the original does
 * while recording dirty rectangles. Returns false if nothing is drawn; otherwise *first_row
 * is the first sprite row drawn. game86 1000:8cfb..8d3f, 63e7..6439 */
static bool clip_to_screen(int x, int *y, int *rows, int width, int *first_row)
{
    *first_row = 0;
    if (x > SCREEN_LAST_X || *y > SCREEN_LAST_Y || x + width < 0 || *y + *rows - 1 < 0)
        return false;
    int bottom = *y + *rows - 1;
    if (bottom > SCREEN_LAST_Y)
        *rows -= bottom - SCREEN_LAST_Y;
    if (*y < 0) {
        *first_row = -*y;
        *rows -= -*y;
        *y = 0;
    }
    return true;
}

/* ---- One column -------------------------------------------------------------------- */

/* game86 1000:8cbb (one-column part) */
static void draw_narrow(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    int rows = sprite->height, first_row = 0;
    if (s_dirty_recording && !clip_to_screen(x, &y, &rows, COLUMN_PIXELS, &first_row))
        return;
    DrawCellPos pos = draw_cell_pos(x, y);
    bool aligned = draw_cell_pos_aligned(pos);
    draw_record_dirty(target, pos.cell, aligned ? 1 : 2, rows);

    for (int r = 0; r < rows; r++) {
        int32_t cell = pos.cell + r * DRAW_CELLS_PER_ROW;
        SpriteCell left = sprite_cell(sprite, first_row + r, 0);
        SpriteCell right = sprite_cell(sprite, first_row + r, 1);
        if (aligned && x >= 0 && x <= NARROW_FAST_MAX_X) {
            blit(target, cell, &left);                  /* tables at 1000:8e71 */
            blit(target, cell + 1, &right);
        } else if (aligned) {
            /* game86 1000:8dc9: the right half is only tested against the right edge, so
             * at x = -16 it still lands in the row's first cell. */
            if (x >= 0)
                blit(target, cell, &left);
            else
                cell--;
            if (x + 8 <= SCREEN_LAST_X)
                blit(target, cell + 1, &right);
        } else if (x > 0 && x <= NARROW_SHIFTED_FAST_MAX_X) {
            column_shifted(target, cell, pos.shift, &left, &right);
        } else {
            column_shifted_clipped(target, &cell, x, pos.shift, &left, &right);
        }
    }
}

/* ---- Several columns --------------------------------------------------------------- */

/* game86 1000:6495..6564: one aligned column of a wide sprite. Returns false where the
 * rest of the row is right of the screen. */
static bool column_aligned_clipped(const DrawTarget *target, int32_t *cursor, int x,
                                   const SpriteCell *left, const SpriteCell *right)
{
    if (x < 0)
        (*cursor)--;
    else if (x >= SCREEN_LAST_X)
        return false;
    else
        blit(target, *cursor, left);
    if (x + 8 < 0) {
        (*cursor)++;
    } else if (x + 8 > SCREEN_LAST_X) {
        return false;
    } else {
        (*cursor)++;
        blit(target, *cursor, right);
        (*cursor)++;
    }
    return true;
}

/* game86 1000:63d2 / 6566 */
static void draw_wide(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    int columns = sprite->width / COLUMN_PIXELS;
    int rows = sprite->height, first_row = 0;
    if (s_dirty_recording && !clip_to_screen(x, &y, &rows, columns * COLUMN_PIXELS, &first_row))
        return;
    DrawCellPos pos = draw_cell_pos(x, y);
    bool aligned = draw_cell_pos_aligned(pos);
    draw_record_dirty(target, pos.cell, aligned ? columns : columns + 1, rows);

    for (int r = 0; r < rows; r++) {
        int32_t cursor = pos.cell + r * DRAW_CELLS_PER_ROW;
        for (int c = 0; c < columns; c++) {
            SpriteCell left = sprite_cell(sprite, first_row + r, 2 * c);
            SpriteCell right = sprite_cell(sprite, first_row + r, 2 * c + 1);
            int column_x = x + c * COLUMN_PIXELS;
            bool more = aligned
                ? column_aligned_clipped(target, &cursor, column_x, &left, &right)
                : column_shifted_clipped(target, &cursor, column_x, pos.shift, &left, &right);
            if (!more)
                break;
        }
    }
}

void draw_sprite(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    if (!sprite || sprite->width <= 0 || sprite->height <= 0)
        return;
    x -= sprite->hotspot_x;
    y -= sprite->hotspot_y;
    if (sprite->width > COLUMN_PIXELS)
        draw_wide(target, sprite, x, y);
    else
        draw_narrow(target, sprite, x, y);
}

/* ---- Sheared ----------------------------------------------------------------------- */

/* game86 1000:e240: one aligned row of a sheared sprite. */
static void row_aligned_clipped(const DrawTarget *target, int32_t cursor, int x,
                                const SpriteCell *left, const SpriteCell *right)
{
    if (x < 0)
        cursor--;
    else if (x >= SCREEN_LAST_X)
        return;
    else
        blit(target, cursor, left);
    if (x + 8 < 0 || x + 8 > SCREEN_LAST_X)
        return;
    blit(target, cursor + 1, right);
}

/* game86 1000:e135 */
void draw_sprite_sheared(const DrawTarget *target, const GfxSprite *sprite, int x, int y,
                         int row_step, int shear)
{
    if (!sprite || sprite->width <= 0 || sprite->height <= 0)
        return;
    /* The origin uses the original's byte multiplies: (h-1)/2 times the low byte of the
     * shear (signed) and of the step (unsigned). */
    int half_rows = sprite->hotspot_y;
    int16_t x0 = (int16_t)(x - sprite->hotspot_x - (int8_t)half_rows * (int8_t)shear);
    int16_t y0 = (int16_t)(y - (uint8_t)half_rows * (uint8_t)row_step);
    if (sprite->width > COLUMN_PIXELS) {
        draw_wide(target, sprite, x0, y0);
        return;
    }

    int16_t row_x = x0, row_y = y0;
    for (int r = 0; r < sprite->height; r++) {
        if (row_x >= -COLUMN_PIXELS && row_x < SCREEN_LAST_X && row_y >= 0) {
            DrawCellPos pos = draw_cell_pos_unshifted(row_x, row_y);
            bool aligned = draw_cell_pos_aligned(pos);
            draw_record_dirty(target, pos.cell, aligned ? 1 : 2, 1);
            SpriteCell left = sprite_cell(sprite, r, 0);
            SpriteCell right = sprite_cell(sprite, r, 1);
            int32_t cursor = pos.cell;
            if (aligned)
                row_aligned_clipped(target, cursor, row_x, &left, &right);
            else
                column_shifted_clipped(target, &cursor, row_x, pos.shift, &left, &right);
        }
        row_x = (int16_t)(row_x + shear);
        row_y = (int16_t)(row_y + row_step);
        if (row_y > SCREEN_LAST_Y)
            break;
    }
}
