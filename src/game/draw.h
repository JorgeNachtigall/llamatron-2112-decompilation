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

#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

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
