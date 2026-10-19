/*
 * text.h - the game's text engine: proportional font, word wrap and escape codes.
 *
 * Text is drawn with the font extracted from f.cmp (GfxFont, first_char 0, 128 glyphs, each
 * 16 pixels wide, the first stored row is blank). A glyph pixel normally ORs the text colour
 * into the screen; in opaque mode (only used by the name-entry screen) it replaces it.
 *
 * Strings are byte strings with escapes introduced by '\' (the byte 0x5c):
 *   \c<n>   colour n & 15                  \r      new line
 *   \e      end of the string              \u \d   pen up / down one line (21 px)
 *   \l \i   pen left / right 21 px         \g<x><dy> left margin = x (0..255), pen to it, y += dy (signed)
 *   \q<text>NUL   draw <text> centred on the screen at the pen's y, then continue after the NUL
 *   \s<hi><lo>    draw sprite number hi*256+lo inline, then move the pen past it
 * Bytes 0 are skipped; a string ends only at \e. Words that would reach x = 304 wrap to the
 * left margin, 21 px down.
 */
#ifndef LLAMATRON_GAME_TEXT_H
#define LLAMATRON_GAME_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "draw.h"
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TEXT_LINE_STEP = 21,     /* pixels between lines and for \l \i */
    TEXT_WRAP_X = 304,       /* a glyph whose pen end reaches this x starts a new line */
    TEXT_ESCAPE = '\\',
};

typedef struct TextState {
    int16_t x, y;            /* pen (game86 g_text_x/g_text_y DS:0b74/0b76) */
    int16_t margin;          /* x after a line break (DS:0b78) */
    uint8_t color;           /* planes set by a glyph pixel (CS:559c) */
    uint8_t clear_planes;    /* planes cleared first: ~color in opaque mode, else 0 (CS:559e) */
    bool opaque;             /* name-entry mode: \c makes glyphs replace pixels (CS:5362) */
} TextState;

void text_set_font(const GfxFont *font);
const GfxFont *text_font(void);

/* Sprites for the \s escape: sprite number `number` (an index into the sprite bank's table
 * from 218C:001C, i.e. sprite ref 14 + number) and its nominal width. Returns false if
 * there is no such sprite. */
typedef bool (*TextSpriteLookup)(unsigned number, const GfxSprite **sprite, int *nominal_width);
void text_set_sprite_lookup(TextSpriteLookup lookup);

/* Draw one glyph with its top-left at (x, y). game86 1000:559f */
void text_draw_glyph(const DrawTarget *target, const TextState *state, unsigned char ch,
                     int x, int y);

/* Advance of `ch` in the current font (0 if the font has no such glyph). */
int text_glyph_advance(unsigned char ch);

/* Draw `ch` at the pen and advance it. game86 1000:54f6 (which takes the position in AX/BX
 * and returns AX + advance: text_draw_glyph + text_glyph_advance) */
void text_draw_char(const DrawTarget *target, TextState *state, unsigned char ch);

/* Print an escaped string from the pen; the pen ends where \e left it. game86 1000:536f
 * Returns a pointer just past the \e. */
const char *text_print(const DrawTarget *target, TextState *state, const char *text);

/* Same with the pen at (0, 0) first. game86 1000:5363 */
const char *text_print_home(const DrawTarget *target, TextState *state, const char *text);

/* Draw a NUL-terminated plain string centred on the screen at row y. game86 1000:5537 */
void text_draw_centred(const DrawTarget *target, const TextState *state, const char *text,
                       int y);

/* Set the colour as \c does. game86 1000:54d4 */
void text_set_color(TextState *state, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_TEXT_H */
