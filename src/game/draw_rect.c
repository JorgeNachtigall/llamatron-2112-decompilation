/*
 * draw_rect.c - the dotted rectangle outlines (the playfield frame, the title's border) and
 * the beaming enemies' beams.
 */
#include "draw.h"

/* One 2x2 dot: the pixel pair containing x, on rows y and y+1. game86 1000:619c
 * Dots below the last row land in the original's invisible gap after the page; they are
 * dropped here. */
static void draw_dot(GfxPage *page, int x, int y, uint8_t color)
{
    int left = x & ~1;
    if (left < 0 || left + 1 >= GFX_WIDTH)
        return;
    for (int row = y; row <= y + 1; row++) {
        if (row < 0 || row >= GFX_HEIGHT)
            continue;
        page->pixels[row][left] = color;
        page->pixels[row][left + 1] = color;
    }
}

/* game86 1000:6112. Each edge loop tests its end after drawing, so it always draws a dot. */
void draw_rect_outline(GfxPageId page_id, int x0, int y0, int x1, int y1, uint8_t color)
{
    GfxPage *page = gfx_page(page_id);
    int x = x0;
    do {
        draw_dot(page, x, y0, color);
        x += 2;
    } while (x <= x1);
    x = x0;
    do {
        draw_dot(page, x, y1, color);
        x += 2;
    } while (x <= x1);
    int y = y0;
    do {
        draw_dot(page, x0, y, color);
        y += 2;
    } while (y <= y1);
    y = y0;
    do {
        draw_dot(page, x1, y, color);
        y += 2;
    } while (y <= y1);
}

/* ---- Beams ------------------------------------------------------------------------- */

enum {
    BEAM_V_FIRST_ROW = 10,
    BEAM_V_ROWS = 186,
    BEAM_H_FIRST_CELL = 1,      /* 19 words from the row's second cell */
    BEAM_H_CELLS = 38,
    BEAM_H_RECORDED_WORDS = 20, /* the whole row */
    CELL_ALL_PIXELS = 0xff,
    ALL_PLANES = 0x0f,
};

/* A one-pixel beam down the playfield at x, in the beam colour. game86 1000:46cd..4746 */
void draw_beam_vertical(const DrawTarget *target, int x)
{
    int32_t cell = BEAM_V_FIRST_ROW * DRAW_CELLS_PER_ROW + ((int16_t)x >> 3);
    draw_record_dirty(target, cell, 1, BEAM_V_ROWS);
    uint8_t bit = (uint8_t)(0x80u >> (x & 7));
    for (int row = 0; row < BEAM_V_ROWS; row++, cell += DRAW_CELLS_PER_ROW)
        draw_cell_paint(target, cell, bit, ALL_PLANES, DRAW_BEAM_COLOR);
}

/* A beam across the playfield on row y, leaving the outermost cell at each end.
 * game86 1000:4795..4800 */
void draw_beam_horizontal(const DrawTarget *target, int y)
{
    int32_t row = (int32_t)y * DRAW_CELLS_PER_ROW;
    draw_record_dirty(target, row, BEAM_H_RECORDED_WORDS, 1);
    for (int i = 0; i < BEAM_H_CELLS; i++)
        draw_cell_paint(target, row + BEAM_H_FIRST_CELL + i, CELL_ALL_PIXELS, ALL_PLANES,
                        DRAW_BEAM_COLOR);
}
