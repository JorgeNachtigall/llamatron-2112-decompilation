/*
 * draw_rect.c - rectangle outlines.
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
