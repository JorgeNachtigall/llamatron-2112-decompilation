/*
 * draw_target.c - cell-addressed drawing targets (see draw.h).
 */
#include "draw.h"

#include <stddef.h>

/* game86 [0x442] g_scroll_mode >= 2 */
static bool s_canvas_wrapped;

/* Cells the original moves back once the scroller canvas has wrapped (1000:e0fa). */
enum { CANVAS_WRAP_SHIFT_CELLS = DRAW_CANVAS_CELLS % DRAW_CELLS_PER_ROW };

DrawTarget draw_target_page(GfxPageId page)
{
    return (DrawTarget){ &gfx_page(page)->pixels[0][0], DRAW_PAGE_CELLS, false, (int)page };
}

void draw_set_canvas_wrapped(bool wrapped)
{
    s_canvas_wrapped = wrapped;
}

bool draw_canvas_wrapped(void)
{
    return s_canvas_wrapped;
}

/* game86 1000:e0e5 */
DrawCellPos draw_cell_pos(int x, int y)
{
    int32_t row = (int32_t)y * DRAW_CELLS_PER_ROW;
    if (x < 0)
        return (DrawCellPos){ row, -x % DRAW_CELL_PIXELS };
    int32_t cell = row + x / DRAW_CELL_PIXELS;
    if (s_canvas_wrapped)
        cell -= CANVAS_WRAP_SHIFT_CELLS;
    return (DrawCellPos){ cell, DRAW_CELL_PIXELS - x % DRAW_CELL_PIXELS };
}

/* game86 1000:e48c */
DrawCellPos draw_cell_pos_unshifted(int x, int y)
{
    int32_t row = (int32_t)y * DRAW_CELLS_PER_ROW;
    if (x < 0)
        return (DrawCellPos){ row, -x % DRAW_CELL_PIXELS };
    return (DrawCellPos){ row + x / DRAW_CELL_PIXELS, DRAW_CELL_PIXELS - x % DRAW_CELL_PIXELS };
}

/* The cell's pixels, or NULL outside a non-wrapping target. */
static uint8_t *cell_pixels(const DrawTarget *target, int32_t cell)
{
    if (target->wraps) {
        cell %= target->cell_count;
        if (cell < 0)
            cell += target->cell_count;
    } else if (cell < 0 || cell >= target->cell_count) {
        return NULL;
    }
    return target->pixels + (size_t)cell * DRAW_CELL_PIXELS;
}

void draw_cell_blit(const DrawTarget *target, int32_t cell, uint8_t mask, const uint8_t colors[8])
{
    uint8_t *px = mask ? cell_pixels(target, cell) : NULL;
    if (!px)
        return;
    for (int i = 0; i < DRAW_CELL_PIXELS; i++) {
        if (mask & (0x80u >> i))
            px[i] = colors[i];
    }
}

void draw_cell_paint(const DrawTarget *target, int32_t cell, uint8_t bits,
                     uint8_t clear_planes, uint8_t set_planes)
{
    uint8_t *px = bits ? cell_pixels(target, cell) : NULL;
    if (!px)
        return;
    for (int i = 0; i < DRAW_CELL_PIXELS; i++) {
        if (bits & (0x80u >> i))
            px[i] = (uint8_t)((px[i] & ~clear_planes) | set_planes);
    }
}
