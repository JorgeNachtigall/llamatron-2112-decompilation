/*
 * draw.h - the game's drawing routines, with the exact visible results of game86's blitters.
 *
 * Everything here draws on the renderer's indexed pages (engine/gfx.h). The original drew in
 * EGA mode 0Dh through the graphics controller; these functions reproduce which pixels end up
 * which colour, including the original's clipping quirks, and are diff-tested against the
 * original code running on the harness's EGA plane model (tests/oracle/test_draw_*.py).
 *
 * Page naming: between frames the original's "back" page (game86 [0x39d5]) is the one on
 * screen, i.e. gfx_display_page_id(), and the next frame is drawn into the other one,
 * gfx_draw_page_id() ([0x39d3], copied to [0x39d7] by the frame loop).
 */
#ifndef LLAMATRON_GAME_DRAW_H
#define LLAMATRON_GAME_DRAW_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Drawing targets (draw_target.c) ------------------------------------------------- */

enum {
    DRAW_CELL_PIXELS = 8,                           /* one planar byte of the original */
    DRAW_CELLS_PER_ROW = GFX_WIDTH / DRAW_CELL_PIXELS,
    DRAW_PAGE_CELLS = DRAW_CELLS_PER_ROW * GFX_HEIGHT,
    DRAW_CANVAS_CELLS = 0x10000,                    /* all of the original's 64 KB VRAM plane */
    DRAW_CANVAS_ROWS = DRAW_CANVAS_CELLS / DRAW_CELLS_PER_ROW,   /* 1638 whole rows */
};

/* Several original routines address the screen in 8-pixel cells (rows of 40 cells) and some
 * of their edge cases move a row's cells into the previous row, or run past the bottom. A
 * DrawTarget is a run of GFX_WIDTH-pixel rows addressed that way:
 *  - a page (draw_target_page): cells past the end are dropped; the original wrote them
 *    into the invisible gap after the page (or, far enough down, the next page: a deviation
 *    no caller is known to trigger);
 *  - the text scroller's canvas: the whole 64 KB plane as one tall picture of
 *    DRAW_CANVAS_CELLS cells whose addresses wrap around (game86 1000:5844). */
typedef struct DrawTarget {
    uint8_t *pixels;        /* cell_count * DRAW_CELL_PIXELS indexed pixels */
    int32_t cell_count;
    bool wraps;
} DrawTarget;

DrawTarget draw_target_page(GfxPageId page);

/* After the scroller's canvas has wrapped once (65536 is not a multiple of 40), the original
 * shifts every text/sprite cell address by -16 cells to stay on its rows (game86
 * [0x442] >= 2 in 1000:e0fa). */
void draw_set_canvas_wrapped(bool wrapped);
bool draw_canvas_wrapped(void);

/* Where the original's blitters start drawing a 16-pixel-wide image at (x, y)
 * (game86 1000:e0e5, rows from tbl_row_offset DS:2D07). For x >= 0: the cell holding x
 * (minus the canvas-wrap shift) and shift = 8 - x % 8, so shift 8 means byte-aligned. For
 * x < 0: the row's first cell, no wrap shift, and shift = -x % 8 (0 = aligned). The
 * blitters move each image byte left by `shift` bits across two cells. */
typedef struct DrawCellPos {
    int32_t cell;
    int shift;
} DrawCellPos;

DrawCellPos draw_cell_pos(int x, int y);
static inline bool draw_cell_pos_aligned(DrawCellPos pos) { return (pos.shift & 7) == 0; }

/* Paint the pixels of `bits` (bit 7 = leftmost) in cell `cell`: each such pixel becomes
 * (pixel & ~clear_planes) | set_planes, the effect of the original's two map-masked writes
 * (clear pass, then colour pass). Cells outside a non-wrapping target are ignored. */
void draw_cell_paint(const DrawTarget *target, int32_t cell, uint8_t bits,
                     uint8_t clear_planes, uint8_t set_planes);

/* ---- Whole pages (draw_page.c) ------------------------------------------------------- */

/* The picture on screen becomes the clean background. game86 1000:62e8 */
void draw_save_screen_as_background(void);

/* Put the clean background back on screen. game86 1000:62f6 */
void draw_show_background(void);

/* Copy the picture on screen into the page drawn next. game86 1000:6304 */
void draw_copy_screen_to_draw_page(void);

/* ---- Rectangles (draw_rect.c) -------------------------------------------------------- */

/* Dotted outline of the rectangle with corners (x0, y0) and (x1, y1) (inclusive): a 2x2-pixel
 * dot every 2 pixels along each edge. A dot at (x, y) covers the pixel pair x&~1, x|1 on rows
 * y and y+1. Each edge is drawn at least once even when x1 < x0 or y1 < y0, as in the
 * original. game86 1000:6112 */
void draw_rect_outline(GfxPageId page, int x0, int y0, int x1, int y1, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_DRAW_H */
