/*
 * title.h - the front screen shown before the game (reimplements fs.exe, see
 * docs/re-notes-fs-install.md §1).
 *
 * fs.exe shows one picture, starts sample 1Bh on the sound driver and waits for the player:
 *   - VGA: NEWTIT over NEWTIT2 as one 320x400 256-colour image, NEWTIT's palette only;
 *   - EGA: TRONEGA, 640x350 in 16 colours through the EGA palette registers.
 * With the keyboard it waits for any key that produces a character (DOS INT 21h/08h), with
 * a joystick for button 1 or 2 of stick A (the keyboard is ignored then). After that it waits
 * two vertical retraces and exits.
 *
 * The pictures come from the extractor (build/generated/data/title_*): raw indices plus the
 * palette values the original programmed into the hardware (6-bit DAC levels for VGA, rgbRGB
 * register values for EGA).
 */
#ifndef LLAMATRON_TITLE_H
#define LLAMATRON_TITLE_H

#include "gfx.h"
#include "input.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TITLE_VGA_WIDTH = 320,
    TITLE_VGA_HEIGHT = 400,
    TITLE_EGA_WIDTH = 640,
    TITLE_EGA_HEIGHT = 350,
    TITLE_EGA_REGISTERS = 17,
    /* fs.exe 1000:008f: driver AH=3 with AL=1Bh, CL=1 (rate index), CH=6 (priority). */
    TITLE_SAMPLE = 0x1b,
    TITLE_SAMPLE_RATE_INDEX = 1,
    TITLE_SAMPLE_PRIORITY = 6,
    /* fs.exe 1000:0142: two vsync waits before leaving. */
    TITLE_EXIT_RETRACES = 2,
};

typedef enum TitlePhase {
    TITLE_WAITING,      /* picture shown, waiting for key / button */
    TITLE_LEAVING,      /* input seen, waiting out the exit retraces */
    TITLE_DONE,
} TitlePhase;

typedef struct TitleScreen {
    GfxAdapter adapter;
    InputDevice device;
    TitlePhase phase;
    int retraces_left;

    int width, height;
    uint8_t *pixels;                    /* width * height palette indices */
    GfxColor palette[GFX_PALETTE_SIZE]; /* as shown on screen */
} TitleScreen;

/* Load the picture for `adapter` from `data_dir` (the extractor's data/ directory).
 * Returns false (with a message on stderr) if a file is missing or has the wrong size. */
bool title_load(TitleScreen *title, GfxAdapter adapter, InputDevice device, const char *data_dir);
void title_free(TitleScreen *title);

/* Keyboard event (PC make code). Only counts in keyboard mode. */
void title_key(TitleScreen *title, uint8_t make_code, bool pressed);

/* Once per displayed frame (= one vertical retrace). `joystick_buttons` is the inverted port
 * 201h value (InputState.joystick_buttons layout: 0x10/0x20 = stick A buttons). */
void title_frame(TitleScreen *title, uint8_t joystick_buttons);

static inline bool title_finished(const TitleScreen *title) { return title->phase == TITLE_DONE; }

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_TITLE_H */
