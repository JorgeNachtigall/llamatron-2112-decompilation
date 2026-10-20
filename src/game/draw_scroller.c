/*
 * draw_scroller.c - the attract-mode text scroller (see scroller.h): the text printed on a
 * 64 KB canvas that wraps, shown one window of it per frame, one row further each frame.
 * The story run prints its late text once the window nears the canvas's end and hands over
 * to the hi-score table after that.
 */
#include "scroller.h"

#include <string.h>

/* game86 checks in the scroll step (1000:58de..592a) */
enum {
    LATE_TEXT_OFFSET = 0xd000,   /* story: window past this -> SCROLLER_MODE_LATE_TEXT */
    TO_HISCORE_OFFSET = 0xe000,  /* story, late text: window past this -> MODE_TO_HISCORE */
    HISCORE_OFFSET = 0x6c00,     /* story, to hi-score: window past this (signed) -> table */
    COUNTER_START = 0xc8,        /* rows on screen */
    PAGE_STRIDE_CELLS = 0x2000,  /* the original's pages lie 8 KB apart in the canvas */
    GAME_OVER_TEXT_Y = 0xc8,
    /* vram_clear_lower 1000:639f */
    CLEAR_BELOW_FIRST_ROW = 198,
    CLEAR_BELOW_CELLS = 0xfc8 * 2,
    CLEAR_BELOW_LINE_ROWS = 2,
    CLEAR_BELOW_LINE_PLANES = 0x0b,
    CELL_ALL_PIXELS = 0xff,
};

static uint8_t s_canvas[DRAW_CANVAS_CELLS * DRAW_CELL_PIXELS];

DrawTarget scroller_canvas(void)
{
    return (DrawTarget){ s_canvas, DRAW_CANVAS_CELLS, true, -1 };
}

static uint8_t *canvas_cell(uint16_t cell)
{
    return s_canvas + (size_t)cell * DRAW_CELL_PIXELS;
}

/* Copy `count` cells starting at canvas cell `first` (wrapping) to `out`, or back. */
static void canvas_copy_out(uint16_t first, uint8_t *out, int32_t count)
{
    for (int32_t i = 0; i < count; i++)
        memcpy(out + (size_t)i * DRAW_CELL_PIXELS, canvas_cell((uint16_t)(first + i)),
               DRAW_CELL_PIXELS);
}

static void canvas_clear(uint16_t first, int32_t count)
{
    for (int32_t i = 0; i < count; i++)
        memset(canvas_cell((uint16_t)(first + i)), 0, DRAW_CELL_PIXELS);
}

static void print_at(Scroller *scroller, const char *text, int16_t y)
{
    DrawTarget canvas = scroller_canvas();
    scroller->text->x = SCROLLER_TEXT_X;
    scroller->text->y = y;
    text_print(&canvas, scroller->text, text);
}

static void canvas_copy_in(uint16_t first, const uint8_t *in, int32_t count)
{
    for (int32_t i = 0; i < count; i++)
        memcpy(canvas_cell((uint16_t)(first + i)), in + (size_t)i * DRAW_CELL_PIXELS,
               DRAW_CELL_PIXELS);
}

/* The three pages where the original keeps them in its VRAM; the gaps between them and
 * the rest of the 64 KB hold nothing visible here and are blank. Page 0 goes on display
 * first, holding the screen (display_page_reset 1000:582f). */
static void canvas_from_vram_pages(void)
{
    if (gfx_display_page_id() != GFX_PAGE_0) {
        draw_copy_screen_to_draw_page();
        gfx_flip();
    }
    memset(s_canvas, 0, sizeof s_canvas);
    static const GfxPageId pages[] = { GFX_PAGE_0, GFX_PAGE_1, GFX_PAGE_BACKGROUND };
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++)
        canvas_copy_in((uint16_t)(i * PAGE_STRIDE_CELLS), &gfx_page(pages[i])->pixels[0][0],
                       DRAW_PAGE_CELLS);
}

/* game86 1000:5844 */
void scroller_start(Scroller *scroller, const ScrollerConfig *config, TextState *text)
{
    *scroller = (Scroller){ .config = *config, .text = text };
    gfx_discard_dirty(GFX_PAGE_0);
    gfx_discard_dirty(GFX_PAGE_1);
    canvas_from_vram_pages();
    draw_set_canvas_wrapped(false);
    /* vram_clear_from_1f40 1000:6383: the screen stays at the canvas top. */
    canvas_clear(DRAW_PAGE_CELLS, DRAW_CANVAS_CELLS - DRAW_PAGE_CELLS);
    print_at(scroller, config->text, SCROLLER_TEXT_Y);
    scroller->counter = COUNTER_START;
}

/* The game-over text run, on whatever the screen shows. game86 1000:094f..095e */
void scroller_start_on_screen(Scroller *scroller, TextState *text)
{
    *scroller = (Scroller){ .text = text, .mode = draw_canvas_wrapped()
                                                      ? SCROLLER_MODE_LATE_TEXT
                                                      : SCROLLER_MODE_TEXT };
    canvas_from_vram_pages();
    text->x = 0;
    text->y = GAME_OVER_TEXT_Y;
}

/* Clears the canvas below the window and draws a two-row line across its top (under the
 * hi-score table). game86 1000:639f */
void scroller_clear_below(const Scroller *scroller)
{
    uint16_t first = (uint16_t)(scroller->offset + CLEAR_BELOW_FIRST_ROW * SCROLLER_ROW_CELLS);
    canvas_clear(first, CLEAR_BELOW_CELLS);
    DrawTarget canvas = scroller_canvas();
    for (int32_t i = 0; i < CLEAR_BELOW_LINE_ROWS * SCROLLER_ROW_CELLS; i++)
        draw_cell_paint(&canvas, (uint16_t)(first + i), CELL_ALL_PIXELS, 0,
                        CLEAR_BELOW_LINE_PLANES);
}

/* The table is drawn from the row just below the window. game86 1000:5941 */
static ScrollerStatus hand_over_to_hiscore(Scroller *scroller)
{
    scroller->text->y = scroller->counter;
    return SCROLLER_SHOW_HISCORE;
}

/* The scroll step; returns false when the run leaves the loop with `*status`.
 * game86 1000:58b8..592a */
static bool scroll_step(Scroller *scroller, ScrollerStatus *status)
{
    uint16_t before = scroller->offset;
    scroller->offset = (uint16_t)(scroller->offset + SCROLLER_ROW_CELLS);
    if (scroller->offset < before) {               /* the window wrapped */
        scroller->counter = COUNTER_START;
        scroller->skip_locked = false;
    }
    scroller->counter++;
    uint16_t offset = scroller->offset;

    if (!scroller->config.late_text) {
        if (offset > scroller->config.end_offset) {
            *status = SCROLLER_DONE;
            return false;
        }
    } else if (scroller->mode == SCROLLER_MODE_TEXT) {
        if (offset > LATE_TEXT_OFFSET) {
            scroller->mode = SCROLLER_MODE_LATE_TEXT;
            draw_set_canvas_wrapped(true);
            print_at(scroller, scroller->config.late_text, SCROLLER_LATE_TEXT_Y);
            scroller->skip_locked = true;
        }
    } else if (scroller->mode == SCROLLER_MODE_LATE_TEXT) {
        if (offset > TO_HISCORE_OFFSET) {
            scroller->skip_locked = true;
            scroller->mode = SCROLLER_MODE_TO_HISCORE;
        }
    } else if ((int16_t)offset > HISCORE_OFFSET) {
        *status = hand_over_to_hiscore(scroller);
        return false;
    }
    return true;
}

/* One frame of a run: skip (Space or fire) ends it, or hands a story over to the table unless the
 * skip is locked; pause holds it; otherwise the window moves down a row. game86
 * 1000:5883..593e */
ScrollerStatus scroller_frame(Scroller *scroller, ScrollerInput input)
{
    if (scroller->stepped) {
        if (input.skip) {
            if (!scroller->config.late_text)
                return SCROLLER_DONE;
            if (!scroller->skip_locked)
                return hand_over_to_hiscore(scroller);
        }
        if (input.pause)
            return SCROLLER_RUNNING;
    }
    ScrollerStatus status = SCROLLER_RUNNING;
    if (!scroll_step(scroller, &status))
        return status;
    scroller->stepped = true;
    scroller_show(scroller);                       /* crtc_set_start 1000:581f */
    /* vram_clear_line 1000:638f: the row that just left the window */
    canvas_clear((uint16_t)(scroller->offset - SCROLLER_ROW_CELLS), SCROLLER_ROW_CELLS);
    return SCROLLER_RUNNING;
}

/* The window one row further, shown (the hi-score table's own scroll). game86 1000:5d19..5d22 */
void scroller_scroll_row(Scroller *scroller)
{
    scroller->offset = (uint16_t)(scroller->offset + SCROLLER_ROW_CELLS);
    scroller_show(scroller);
}

void scroller_show(const Scroller *scroller)
{
    canvas_copy_out(scroller->offset, &gfx_page(GFX_PAGE_0)->pixels[0][0], DRAW_PAGE_CELLS);
    if (gfx_display_page_id() != GFX_PAGE_0)
        gfx_flip();
}

/* The end of a run: pages 1 and the background back from the canvas, page 0 cleared and
 * shown. game86 1000:594a */
void scroller_finish(Scroller *scroller)
{
    canvas_copy_out(PAGE_STRIDE_CELLS, &gfx_page(GFX_PAGE_1)->pixels[0][0], DRAW_PAGE_CELLS);
    canvas_copy_out(2 * PAGE_STRIDE_CELLS, &gfx_page(GFX_PAGE_BACKGROUND)->pixels[0][0],
                    DRAW_PAGE_CELLS);
    gfx_clear(GFX_PAGE_0, 0);
    canvas_clear(0, DRAW_PAGE_CELLS);
    if (gfx_display_page_id() != GFX_PAGE_0)
        gfx_flip();
    scroller->offset = 0;
    if (scroller->config.late_text) {
        scroller->mode = SCROLLER_MODE_TEXT;
        draw_set_canvas_wrapped(false);
    }
}
