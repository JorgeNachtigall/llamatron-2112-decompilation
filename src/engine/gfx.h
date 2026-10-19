/*
 * gfx.h - the game's renderer API (PLAN.md §2.3).
 *
 * The original draws in EGA/VGA mode 0Dh (320x200, 16 colours, 4 planes) into three VRAM
 * pages: 0x0000 and 0x2000 are shown alternately via the CRTC start address, 0x4000 holds the
 * clean background that erases sprites (game86 1000:62e8/62f6/6304, 1e7b:013f). Here those are
 * three 320x200 pages of 8-bit palette indices. The game never touches hardware registers; the
 * visible results of the original's blitters are reproduced by these functions (exactness is
 * proven by the differential tests in tests/, which convert pages <-> EGA planes).
 *
 * Pages hold indices 0..255 so the same API can show the 256-colour title pictures and later
 * extensions; the game itself only uses 0..15.
 *
 * This module is pure C (no SDL); present the display page with plat_present().
 */
#ifndef LLAMATRON_GFX_H
#define LLAMATRON_GFX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GFX_WIDTH = 320,
    GFX_HEIGHT = 200,
    GFX_GAME_COLORS = 16,
    GFX_PALETTE_SIZE = 256,
};

typedef enum GfxPageId {
    GFX_PAGE_0 = 0,          /* original VRAM offset 0x0000 */
    GFX_PAGE_1 = 1,          /* original VRAM offset 0x2000 */
    GFX_PAGE_BACKGROUND = 2, /* original VRAM offset 0x4000: clean background */
    GFX_PAGE_COUNT
} GfxPageId;

typedef struct GfxPage {
    uint8_t pixels[GFX_HEIGHT][GFX_WIDTH];
} GfxPage;

typedef struct GfxColor {
    uint8_t r, g, b;         /* 8 bits per channel */
} GfxColor;

typedef struct GfxRect {
    int16_t x, y, w, h;
} GfxRect;

/* ---- Pages and flipping ------------------------------------------------------------- */

/* Clears all pages to colour 0, palette to black, page 0 displayed, page 1 drawn. */
void gfx_init(void);

GfxPage *gfx_page(GfxPageId id);
GfxPageId gfx_draw_page_id(void);     /* page being drawn (game86 [0x39d7]) */
GfxPageId gfx_display_page_id(void);  /* page on screen */
static inline GfxPage *gfx_draw_page(void) { return gfx_page(gfx_draw_page_id()); }

/* Show the page just drawn and start drawing into the other one (game86 1e7b:013f: CRTC start
 * address write + swap of [0x39d3]/[0x39d5]). The page swap takes effect at the next present,
 * which corresponds to the original's wait for vertical retrace. */
void gfx_flip(void);

/* Pixels of the displayed page, row-major, pitch GFX_WIDTH. */
const uint8_t *gfx_display_pixels(void);

void gfx_clear(GfxPageId page, uint8_t color);
void gfx_copy_page(GfxPageId dst, GfxPageId src);   /* game86 1000:62e8/62f6/6304 */

/* ---- Palette ------------------------------------------------------------------------ */

/* Current 256-entry RGB palette as shown on screen. */
const GfxColor *gfx_palette(void);
void gfx_set_palette(int first, int count, const GfxColor *colors);

/* VGA DAC values, 6 bits per channel (INT 10h AX=1012h semantics). */
void gfx_set_palette_dac(int first, int count, const uint8_t (*rgb6)[3]);

/* Which palette path the game uses (llama.inf[0] 'V'/'E' -> game86 [0x58a]). */
typedef enum GfxAdapter {
    GFX_ADAPTER_EGA = 0,
    GFX_ADAPTER_VGA = 1,
} GfxAdapter;

/* One of the game's palettes, typed by the extractor. The original stores the two forms
 * back to back (VGA words at DS:p, EGA registers at DS:p+0x20, e.g. DS:2144/DS:2164). */
typedef struct GfxGamePalette {
    uint16_t st_words[GFX_GAME_COLORS];   /* $0RGB, one nibble per channel */
    uint8_t ega_registers[GFX_GAME_COLORS + 1]; /* rgbRGB per colour + overscan (border) */
} GfxGamePalette;

/* `nibble_to_dac` (16 entries, extracted from DS:2134) maps an ST nibble to a 6-bit DAC level;
 * only used on VGA. */
void gfx_set_adapter(GfxAdapter adapter, const uint8_t nibble_to_dac[16]);
GfxAdapter gfx_adapter(void);

/* game86 1000:1be6: set colours 0..15 from a game palette using the current adapter's path.
 * VGA: red = bits 8..11, green = bits 4..7, blue = bits 0..3 of each word, each through
 * nibble_to_dac (the original writes DAC 0..7 and 16..23, which mode 0Dh's default attribute
 * palette shows as colours 0..15). EGA: INT 10h AX=1002h with the 16 registers + overscan. */
void gfx_set_game_palette(const GfxGamePalette *palette);

/* Single steps of the above, also usable on their own. */
void gfx_set_palette_st(const uint16_t words[GFX_GAME_COLORS], const uint8_t nibble_to_dac[16]);
void gfx_set_palette_ega(const uint8_t registers[GFX_GAME_COLORS]);
/* Standard EGA monitor colour of a 6-bit rgbRGB value (primary = 2/3, secondary = 1/3). */
GfxColor gfx_ega_color(uint8_t ega_rgbRGB);

/* ---- Drawing ------------------------------------------------------------------------ */

/* Everything below clips to the 320x200 page. */
void gfx_fill_rect(GfxPageId page, GfxRect rect, uint8_t color);           /* game86 1000:6112 */

/* Copy `rect` from the background page into `page` (erasing sprites). */
void gfx_restore_rect(GfxPageId page, GfxRect rect);

/* An image in our own format, produced by the extractor from the sprite bank (segment 218C).
 * `pixels` is width*height colour indices; pixels whose `mask` byte is 0 are transparent
 * (mask may be NULL: opaque). Hotspot = offset of the drawing position inside the image
 * (the original draws most sprites centred: hotspot = (width/2, height/2)). */
typedef struct GfxSprite {
    int16_t width, height;
    int16_t hotspot_x, hotspot_y;
    const uint8_t *pixels;
    const uint8_t *mask;
} GfxSprite;

typedef enum GfxDrawFlags {
    GFX_DRAW_NORMAL = 0,
    GFX_DRAW_MIRROR_X = 1u << 0,
    GFX_DRAW_MIRROR_Y = 1u << 1,
    GFX_DRAW_TOP_LEFT = 1u << 2,   /* ignore the hotspot: (x,y) is the top-left corner */
} GfxDrawFlags;

/* Draw a sprite; returns the clipped screen rectangle touched (w == 0 if fully off-screen), so
 * the caller can record it for erasing. game86 1000:8cbb (centred), 1000:aa29/ac03 (unaligned) */
GfxRect gfx_draw_sprite(GfxPageId page, const GfxSprite *sprite, int x, int y, unsigned flags);

/* Scaled draw; scale_q8 = 256 is 1:1. game86 1000:63d2. STUB: nearest-neighbour until the
 * original's stepping is RE'd and diff-tested. */
GfxRect gfx_draw_sprite_scaled(GfxPageId page, const GfxSprite *sprite, int x, int y,
                               int scale_q8, unsigned flags);

/* Proportional bitmap font in our own format (extracted from f.cmp): each glyph is up to 16
 * pixels wide, one uint16 per row, bit 15 = leftmost pixel. */
typedef struct GfxGlyph {
    uint8_t advance;     /* pen advance in pixels */
    uint8_t rows;        /* number of rows stored (<= font height) */
    const uint16_t *bits;
} GfxGlyph;

typedef struct GfxFont {
    uint8_t height;
    uint8_t first_char;
    uint16_t glyph_count;
    const GfxGlyph *glyphs;
} GfxFont;

/* Print `text` with the top-left at (x, y) in `color`; returns the pen x after the last glyph.
 * STUB semantics: characters outside the font are skipped; the original text engine's rules
 * (game86 range E) will replace this once diff-tested. */
int gfx_print(GfxPageId page, const GfxFont *font, int x, int y, const char *text, uint8_t color);
int gfx_text_width(const GfxFont *font, const char *text);

/* ---- Dirty rectangles --------------------------------------------------------------- */

/* The original keeps one list of drawn rectangles per displayed page (CS:[0x6733]/[0x6735])
 * and erases them from the background page before drawing the next frame into that page. */
enum { GFX_MAX_DIRTY = 256 };

void gfx_mark_dirty(GfxPageId page, GfxRect rect);
/* Restore every rectangle recorded for `page` from the background, then clear its list. */
void gfx_restore_dirty(GfxPageId page);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GFX_H */
