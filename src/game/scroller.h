/*
 * scroller.h - the attract-mode text scroller (game86 text_scroller 1000:5844).
 *
 * The original treats the whole 64 KB of EGA VRAM as one tall picture (the canvas, 1638.4
 * rows of 40 cells): it keeps the screen on display, prints a long text below it and moves
 * the CRTC start address down one row (40 cells) per frame, clearing each row as it scrolls
 * out at the top so the canvas can wrap around. Here the canvas is a buffer of its own
 * (scroller_canvas(), a wrapping DrawTarget) and each frame the visible 320x200 window of it
 * is copied into page 0, which stays on display.
 *
 * Two kinds of run (the original's g_scroll_flag DS:0443):
 *  - plain (late_text NULL): scroll until the window passes end_offset (smc CS:582d) or skip;
 *  - story: after the window passes 0xd000 the late text is printed at the top of the canvas
 *    (which comes into view once the window wraps), and after wrapping the scroller hands
 *    over to the hi-score table (SCROLLER_SHOW_HISCORE). Skip also jumps to the table, but
 *    not between the late text and the wrap.
 *
 * The original is one blocking loop; this is the same loop one frame (vertical retrace) per
 * scroller_frame() call. After SCROLLER_DONE or the hi-score part, call scroller_finish().
 */
#ifndef LLAMATRON_GAME_SCROLLER_H
#define LLAMATRON_GAME_SCROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "draw.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SCROLLER_ROW_CELLS = DRAW_CELLS_PER_ROW,  /* the window moves this many cells per frame */
    SCROLLER_TEXT_X = 0x10,
    SCROLLER_TEXT_Y = 0xce,                   /* just below the first screen */
    SCROLLER_LATE_TEXT_Y = 0x02,
};

typedef enum ScrollerStatus {
    SCROLLER_RUNNING,
    SCROLLER_SHOW_HISCORE,    /* story: draw the table; the text pen's y is set to the row
                                 just below the window, scroller->counter (1000:5941) */
    SCROLLER_DONE,
} ScrollerStatus;

/* What the original reads between frames: g_scroll_abort (the keyboard handler's last scan
 * code, cleared at each scroll step) == 0x39 (space) or joystick button 1 -> skip;
 * == 0x19 ('P') or button 2 -> pause (no scrolling while it holds). */
typedef struct ScrollerInput {
    bool skip;
    bool pause;
} ScrollerInput;

typedef struct ScrollerConfig {
    const char *text;        /* escaped string, printed at (SCROLLER_TEXT_X, SCROLLER_TEXT_Y) */
    uint16_t end_offset;     /* plain run: last window offset shown */
    const char *late_text;   /* story run: printed at (SCROLLER_TEXT_X, SCROLLER_LATE_TEXT_Y) */
} ScrollerConfig;

/* How far a story run has got (a plain run stays in SCROLLER_MODE_TEXT). */
typedef enum ScrollerMode {
    SCROLLER_MODE_TEXT = 0,          /* the first text */
    SCROLLER_MODE_LATE_TEXT = 2,     /* the late text printed, the canvas wrapped */
    SCROLLER_MODE_TO_HISCORE = 3,    /* skip locked until the hand-over to the table */
} ScrollerMode;

typedef struct Scroller {
    ScrollerConfig config;
    TextState *text;         /* the game's text pen and colour, used for both prints */
    uint16_t offset;         /* g_scroll_vram_off DS:043d: canvas cell at the window's top left */
    int16_t counter;         /* g_scroll_counter DS:0440: canvas row just below the window */
    uint8_t mode;            /* ScrollerMode (g_scroll_mode DS:0442) */
    bool skip_locked;        /* g_scroll_state DS:043f: skip ignored */
    bool stepped;            /* a frame has been shown, so input is read before the next one */
} Scroller;

/* The canvas: DRAW_CANVAS_CELLS cells, wrapping. */
DrawTarget scroller_canvas(void);

/* Start a run: page 0 goes on display holding the current screen, the canvas becomes that
 * screen followed by blank rows, and config->text is printed below it. game86 1000:5844..5881 */
void scroller_start(Scroller *scroller, const ScrollerConfig *config, TextState *text);

/* Use the screen on display as the top of the canvas without a text, keeping the rest of
 * the original's VRAM (pages 1 and background at 0x2000 / 0x4000) below it, window at the
 * top and the text pen at (0, 200): the game-over path to the hi-score table.
 * game86 1000:094f..095e */
void scroller_start_on_screen(Scroller *scroller, TextState *text);

/* One frame: read the input left from the last frame, then scroll one row and show the
 * window. game86 1000:5883..593e */
ScrollerStatus scroller_frame(Scroller *scroller, ScrollerInput input);

/* Move the window one row down and show it, without the scroll step's bookkeeping or row
 * clearing (the hi-score table's own scroll, game86 1000:5d19). */
void scroller_scroll_row(Scroller *scroller);

/* Blank the canvas from the window's row 198 on for 202 rows, with rows 198..199 in colour
 * 11 (a line along the bottom of the window): room for the hi-score table.
 * game86 vram_clear_lower 1000:639f */
void scroller_clear_below(const Scroller *scroller);

/* Copy the window at scroller->offset into page 0. */
void scroller_show(const Scroller *scroller);

/* End a run: blank page 0 on display (and the canvas-wrap address shift off again, as the
 * caller of a story run does at 1000:02ee), pages 1 and background left holding the canvas parts
 * the original kept there (its VRAM at 0x2000 / 0x4000), page 1 drawn next.
 * game86 1000:594a..595d */
void scroller_finish(Scroller *scroller);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_SCROLLER_H */
