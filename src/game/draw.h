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
    int page;               /* GfxPageId whose dirty list records sprite draws, or -1 */
} DrawTarget;

DrawTarget draw_target_page(GfxPageId page);

/* Cells dropped because they fell outside a page target (where the original wrote into the
 * gap after the page, or into another page: docs/DEVIATIONS.md #3, #17), counted since the
 * last reset, and the last one dropped. For tests (tests/oracle/level_sweep.py). */
typedef struct DrawDropStats {
    uint32_t cells;
    int page;               /* the last dropped cell's GfxPageId */
    int32_t cell;           /* and its cell number on that page */
} DrawDropStats;

DrawDropStats draw_drop_stats(void);
void draw_reset_drop_stats(void);

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
/* The same without the canvas-wrap shift (game86 1000:e48c, used by the sheared draw). */
DrawCellPos draw_cell_pos_unshifted(int x, int y);
static inline bool draw_cell_pos_aligned(DrawCellPos pos) { return (pos.shift & 7) == 0; }

/* Paint the pixels of `bits` (bit 7 = leftmost) in cell `cell`: each such pixel becomes
 * (pixel & ~clear_planes) | set_planes, the effect of the original's two map-masked writes
 * (clear pass, then colour pass). Cells outside a non-wrapping target are ignored. */
void draw_cell_paint(const DrawTarget *target, int32_t cell, uint8_t bits,
                     uint8_t clear_planes, uint8_t set_planes);

/* Copy the pixels of `mask` (bit 7 = leftmost) from colors[0..7] into cell `cell`: the
 * original's bit-masked write of the four plane bytes of a sprite. */
void draw_cell_blit(const DrawTarget *target, int32_t cell, uint8_t mask, const uint8_t colors[8]);

/* ---- Sprites (draw_sprite.c) --------------------------------------------------------- */

/* Sprites are GfxSprite frames from the sprite bank: width = 16 x columns, transparent
 * pixels GFX_TRANSPARENT (or mask 0), hotspot (nominal width / 2, (height - 1) / 2).
 *
 * While dirty recording is on (game86 cs_dirty_enable CS:672f, switched on by the frame loop
 * every frame) a draw is clipped to the 320x200 screen first and the rectangle it may touch
 * is added to the target page's dirty list, in whole 16-pixel words like the original. While
 * it is off nothing is clipped vertically and nothing is recorded (used for the background
 * page, the HUD and inline text sprites). */
void draw_set_dirty_recording(bool on);
bool draw_dirty_recording(void);

/* While recording is on, add to the target page's dirty list the area the original records
 * as {16-pixel words, rows, first cell}: `words` x 16 pixels by `rows` rows from `cell`. */
void draw_record_dirty(const DrawTarget *target, int32_t cell, int words, int rows);

/* Draw `sprite` with its hotspot at (x, y). One-column sprites have at most 24 rows (the
 * original dispatches on the row count through 25-entry tables; real frames have <= 23). game86 1000:8cbb (one column: 8dc9 and the
 * tables at 8e71 / abd1, shifted 1000:aa29, ac10), 1000:63d2 / 6566 (several columns) */
void draw_sprite(const DrawTarget *target, const GfxSprite *sprite, int x, int y);

/* Switch dirty recording off and draw. Recording stays off afterwards, as in the original
 * (the frame loop switches it on again for the next frame). game86 1000:42bc, 42cb */
void draw_sprite_unrecorded(const DrawTarget *target, const GfxSprite *sprite, int x, int y);

/* Draw into page 0, page 1 and the background page with recording off, then restore the
 * recording state: a picture that stays on screen whichever page is shown. game86 1000:4284
 * (g3 shapes; 424c, the unreferenced sprite-bank twin) */
void draw_sprite_all_pages(const GfxSprite *sprite, int x, int y);

/* Draw into the background page with recording off, then into `target` as draw_sprite does
 * (recorded if recording is on), so the sprite survives the target's dirty-list restore.
 * The original draws into the page being drawn ([0x39d3]): `target` should be that page.
 * Both draws are at (x, y); the original's are at x = 0x4000 and x = [0x39d3]
 * (docs/DEVIATIONS.md #18). game86 1000:4bd3 (unreferenced) */
void draw_sprite_with_background(const DrawTarget *target, const GfxSprite *sprite, int x, int y);

/* The original's "scaled" draw (game86 1000:e135, selected by g_draw_flags >= 0): a
 * one-column sprite is drawn row by row, row r at (x0 + r * shear, y0 + r * row_step),
 * so row_step spreads (> 1) or squashes (0) it vertically and shear slants it; drawing
 * stops below the screen. shear is row_step with the sign of g_draw_mirror, or 0. Wider
 * sprites are drawn unsheared, as draw_sprite does, at the shifted origin. */
void draw_sprite_sheared(const DrawTarget *target, const GfxSprite *sprite, int x, int y,
                         int row_step, int shear);

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

/* The beams of the gate enemies (anim 02 / 03): a one-pixel line that turns its pixels
 * colour DRAW_BEAM_COLOR whatever they were (the original's set/reset write). Neither is
 * clipped; while dirty recording is on the line's area is recorded, as the original's
 * {words, rows, cell} entry. */
enum { DRAW_BEAM_COLOR = 1 };
/* Column x, rows 10..195 (from the cell x >> 3 of row 10, so x outside the screen lands
 * in a neighbouring row). game86 1000:46cd..4746 */
void draw_beam_vertical(const DrawTarget *target, int x);
/* Row y, pixels 8..311. y must be 0..299 (the original's row table); cells past the page
 * are dropped as for any page target. game86 1000:4795..4800 */
void draw_beam_horizontal(const DrawTarget *target, int y);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_DRAW_H */
