/*
 * gfx.c - indexed 320x200 renderer (see gfx.h).
 */
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    GfxPage pages[GFX_PAGE_COUNT];
    GfxPageId draw_page;
    GfxPageId display_page;
    GfxColor palette[GFX_PALETTE_SIZE];
    GfxAdapter adapter;
    uint8_t nibble_to_dac[16];
    GfxRect dirty[2][GFX_MAX_DIRTY];
    int dirty_count[2];
} g_gfx;

/* ---- Pages and flipping ------------------------------------------------------------- */

void gfx_init(void)
{
    memset(&g_gfx, 0, sizeof g_gfx);
    g_gfx.display_page = GFX_PAGE_0;
    g_gfx.draw_page = GFX_PAGE_1;
    g_gfx.adapter = GFX_ADAPTER_VGA;
}

GfxPage *gfx_page(GfxPageId id)
{
    return &g_gfx.pages[id];
}

GfxPageId gfx_draw_page_id(void)
{
    return g_gfx.draw_page;
}

GfxPageId gfx_display_page_id(void)
{
    return g_gfx.display_page;
}

/* game86 1e7b:013f (page flip part) */
void gfx_flip(void)
{
    GfxPageId shown = g_gfx.draw_page;
    g_gfx.draw_page = g_gfx.display_page;
    g_gfx.display_page = shown;
}

const uint8_t *gfx_display_pixels(void)
{
    return &g_gfx.pages[g_gfx.display_page].pixels[0][0];
}

void gfx_clear(GfxPageId page, uint8_t color)
{
    memset(g_gfx.pages[page].pixels, color, sizeof g_gfx.pages[page].pixels);
}

/* game86 1000:62e8 / 62f6 / 6304 */
void gfx_copy_page(GfxPageId dst, GfxPageId src)
{
    if (dst != src)
        memcpy(g_gfx.pages[dst].pixels, g_gfx.pages[src].pixels, sizeof g_gfx.pages[dst].pixels);
}

/* ---- Palette ------------------------------------------------------------------------ */

const GfxColor *gfx_palette(void)
{
    return g_gfx.palette;
}

void gfx_set_palette(int first, int count, const GfxColor *colors)
{
    for (int i = 0; i < count; i++) {
        int index = first + i;
        if (index >= 0 && index < GFX_PALETTE_SIZE)
            g_gfx.palette[index] = colors[i];
    }
}

/* A 6-bit DAC level as the 8-bit value a VGA shows (replicate the top bits). */
static uint8_t dac6_to_8(uint8_t level)
{
    level &= 0x3f;
    return (uint8_t)(level << 2 | level >> 4);
}

void gfx_set_palette_dac(int first, int count, const uint8_t (*rgb6)[3])
{
    for (int i = 0; i < count; i++) {
        GfxColor c = { dac6_to_8(rgb6[i][0]), dac6_to_8(rgb6[i][1]), dac6_to_8(rgb6[i][2]) };
        gfx_set_palette(first + i, 1, &c);
    }
}

void gfx_set_adapter(GfxAdapter adapter, const uint8_t nibble_to_dac[16])
{
    if (!nibble_to_dac) {
        /* A missing table would turn every VGA game palette black (or worse), silently. */
        fprintf(stderr, "gfx_set_adapter: nibble_to_dac is NULL (pass asset_palettes()->nibble_to_dac)\n");
        abort();
    }
    g_gfx.adapter = adapter;
    memcpy(g_gfx.nibble_to_dac, nibble_to_dac, sizeof g_gfx.nibble_to_dac);
}

GfxAdapter gfx_adapter(void)
{
    return g_gfx.adapter;
}

/* game86 1000:1bf2..1c58 */
void gfx_set_palette_st(const uint16_t words[GFX_GAME_COLORS], const uint8_t nibble_to_dac[16])
{
    uint8_t dac[GFX_GAME_COLORS][3];
    for (int i = 0; i < GFX_GAME_COLORS; i++) {
        dac[i][0] = nibble_to_dac[(words[i] >> 8) & 0xf];
        dac[i][1] = nibble_to_dac[(words[i] >> 4) & 0xf];
        dac[i][2] = nibble_to_dac[words[i] & 0xf];
    }
    gfx_set_palette_dac(0, GFX_GAME_COLORS, (const uint8_t (*)[3])dac);
}

GfxColor gfx_ega_color(uint8_t v)
{
    /* Bits: 5 r, 4 g, 3 b (secondary, 1/3 intensity); 2 R, 1 G, 0 B (primary, 2/3). */
    GfxColor c;
    c.r = (uint8_t)(((v >> 2) & 1) * 0xaa + ((v >> 5) & 1) * 0x55);
    c.g = (uint8_t)(((v >> 1) & 1) * 0xaa + ((v >> 4) & 1) * 0x55);
    c.b = (uint8_t)((v & 1) * 0xaa + ((v >> 3) & 1) * 0x55);
    return c;
}

/* game86 1000:1c59 (INT 10h AX=1002h); the overscan register only colours the border, which
 * the port does not display. */
void gfx_set_palette_ega(const uint8_t registers[GFX_GAME_COLORS])
{
    for (int i = 0; i < GFX_GAME_COLORS; i++)
        g_gfx.palette[i] = gfx_ega_color(registers[i]);
}

/* game86 1000:1be6 */
void gfx_set_game_palette(const GfxGamePalette *palette)
{
    if (g_gfx.adapter == GFX_ADAPTER_VGA)
        gfx_set_palette_st(palette->st_words, g_gfx.nibble_to_dac);
    else
        gfx_set_palette_ega(palette->ega_registers);
}

/* ---- Drawing ------------------------------------------------------------------------ */

static GfxRect clip_rect(GfxRect r)
{
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GFX_WIDTH) x1 = GFX_WIDTH;
    if (y1 > GFX_HEIGHT) y1 = GFX_HEIGHT;
    if (x1 <= x0 || y1 <= y0)
        return (GfxRect){ 0, 0, 0, 0 };
    return (GfxRect){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0) };
}

void gfx_fill_rect(GfxPageId page, GfxRect rect, uint8_t color)
{
    GfxRect r = clip_rect(rect);
    for (int y = r.y; y < r.y + r.h; y++)
        memset(&g_gfx.pages[page].pixels[y][r.x], color, (size_t)r.w);
}

/* game86 1e7b:0247 and the restore routines at 1e7b:031c: per row, one linear span of w
 * pixels in page memory (may run on into the next row), clamped at the end of the page. */
void gfx_restore_rect(GfxPageId page, GfxRect rect)
{
    if (page == GFX_PAGE_BACKGROUND || rect.w <= 0 || rect.h <= 0)
        return;
    const long page_size = (long)GFX_WIDTH * GFX_HEIGHT;
    uint8_t *dst = &g_gfx.pages[page].pixels[0][0];
    const uint8_t *src = &g_gfx.pages[GFX_PAGE_BACKGROUND].pixels[0][0];
    for (int row = 0; row < rect.h; row++) {
        long start = (long)(rect.y + row) * GFX_WIDTH + rect.x;
        long end = start + rect.w;
        if (start < 0)
            start = 0;
        if (end > page_size)
            end = page_size;
        if (end > start)
            memcpy(dst + start, src + start, (size_t)(end - start));
    }
}

GfxRect gfx_draw_sprite(GfxPageId page, const GfxSprite *sprite, int x, int y, unsigned flags)
{
    return gfx_draw_sprite_scaled(page, sprite, x, y, 256, flags);
}

/* Generic helper (not the game's blitter, see gfx.h): nearest-neighbour scaling, mirroring,
 * mask or GFX_TRANSPARENT keying. */
GfxRect gfx_draw_sprite_scaled(GfxPageId page, const GfxSprite *sprite, int x, int y,
                               int scale_q8, unsigned flags)
{
    if (!sprite || sprite->width <= 0 || sprite->height <= 0 || scale_q8 <= 0)
        return (GfxRect){ 0, 0, 0, 0 };

    int out_w = sprite->width * scale_q8 / 256;
    int out_h = sprite->height * scale_q8 / 256;
    if (out_w <= 0 || out_h <= 0)
        return (GfxRect){ 0, 0, 0, 0 };
    if (!(flags & GFX_DRAW_TOP_LEFT)) {
        x -= sprite->hotspot_x * scale_q8 / 256;
        y -= sprite->hotspot_y * scale_q8 / 256;
    }

    GfxRect dst = clip_rect((GfxRect){ (int16_t)x, (int16_t)y, (int16_t)out_w, (int16_t)out_h });
    GfxPage *p = &g_gfx.pages[page];
    for (int dy = dst.y; dy < dst.y + dst.h; dy++) {
        int sy = (dy - y) * 256 / scale_q8;
        if (flags & GFX_DRAW_MIRROR_Y)
            sy = sprite->height - 1 - sy;
        const uint8_t *src_row = sprite->pixels + (size_t)sy * (size_t)sprite->width;
        const uint8_t *mask_row = sprite->mask ? sprite->mask + (size_t)sy * (size_t)sprite->width : NULL;
        for (int dx = dst.x; dx < dst.x + dst.w; dx++) {
            int sx = (dx - x) * 256 / scale_q8;
            if (flags & GFX_DRAW_MIRROR_X)
                sx = sprite->width - 1 - sx;
            uint8_t v = src_row[sx];
            if (mask_row ? mask_row[sx] != 0 : v != GFX_TRANSPARENT)
                p->pixels[dy][dx] = v;
        }
    }
    return dst;
}

static const GfxGlyph *find_glyph(const GfxFont *font, unsigned char ch)
{
    if (ch < font->first_char || ch - font->first_char >= font->glyph_count)
        return NULL;
    return &font->glyphs[ch - font->first_char];
}

int gfx_text_width(const GfxFont *font, const char *text)
{
    int width = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        const GfxGlyph *g = find_glyph(font, *s);
        if (g)
            width += g->advance;
    }
    return width;
}

/* Generic text helper (the game's text engine is text_print() in src/game/text.c). */
int gfx_print(GfxPageId page, const GfxFont *font, int x, int y, const char *text, uint8_t color)
{
    GfxPage *p = &g_gfx.pages[page];
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        const GfxGlyph *g = find_glyph(font, *s);
        if (!g)
            continue;
        for (int row = 0; row < g->rows; row++) {
            int py = y + row;
            if (py < 0 || py >= GFX_HEIGHT)
                continue;
            uint16_t bits = g->bits[row];
            for (int col = 0; col < 16 && bits; col++, bits = (uint16_t)(bits << 1)) {
                int px = x + col;
                if ((bits & 0x8000u) && px >= 0 && px < GFX_WIDTH)
                    p->pixels[py][px] = color;
            }
        }
        x += g->advance;
    }
    return x;
}

/* ---- Dirty rectangles --------------------------------------------------------------- */

void gfx_mark_dirty(GfxPageId page, GfxRect rect)
{
    if (page == GFX_PAGE_BACKGROUND || rect.w <= 0 || rect.h <= 0)
        return;
    int *count = &g_gfx.dirty_count[page];
    if (*count < GFX_MAX_DIRTY) {
        g_gfx.dirty[page][(*count)++] = rect;
    } else {
        /* List full: fall back to restoring the whole page next time. */
        g_gfx.dirty[page][0] = (GfxRect){ 0, 0, GFX_WIDTH, GFX_HEIGHT };
        *count = 1;
    }
}

/* game86 1e7b:013f (erase pass over the dirty list) */
void gfx_restore_dirty(GfxPageId page)
{
    if (page == GFX_PAGE_BACKGROUND)
        return;
    for (int i = 0; i < g_gfx.dirty_count[page]; i++)
        gfx_restore_rect(page, g_gfx.dirty[page][i]);
    g_gfx.dirty_count[page] = 0;
}

/* game86 1000:5844 (list end pointers reset to the start, nothing erased) */
void gfx_discard_dirty(GfxPageId page)
{
    if (page != GFX_PAGE_BACKGROUND)
        g_gfx.dirty_count[page] = 0;
}
