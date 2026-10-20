/*
 * draw_page.c - whole-page copies between the shown page, the draw page and the background.
 *
 * The original copies a page plane by plane through the EGA latches; the result is simply the
 * whole page copied. Also the frame's screen-flash step.
 */
#include "draw.h"
#include "draw_play.h"

#include "game_data.h"

/* game86 1000:62e8 (used when the "continue?" screen freezes the playfield) */
void draw_save_screen_as_background(void)
{
    gfx_copy_page(GFX_PAGE_BACKGROUND, gfx_display_page_id());
}

/* game86 1000:62f6 */
void draw_show_background(void)
{
    gfx_copy_page(gfx_display_page_id(), GFX_PAGE_BACKGROUND);
}

/* game86 1000:6304 (display_page_reset 1000:582f uses it before swapping pages) */
void draw_copy_screen_to_draw_page(void)
{
    gfx_copy_page(gfx_draw_page_id(), gfx_display_page_id());
}

/* ---- Screen flashes ------------------------------------------------------------------- */

enum {
    FLASH_MODE_FLASHES = 2,          /* llama.inf 'Y': pulsing and flashes */
    FLASH_PALETTE_PERIOD = 0x1e,     /* frames between game palette reloads */
    FLASH_COLOR = 0,                 /* the colour a flash list changes */
    FLASH_EGA_REGISTER = 0x17,       /* EGA: one fixed colour while a list runs */
};

/* Once a frame, with flashes on: the game palette reloaded every FLASH_PALETTE_PERIOD
 * frames, and while a flash list runs, colour 0 set to its next word (0RGB in nibbles).
 * game86 frame_run 1e7b:0196..0212 */
void draw_flash_step(GameState *game)
{
    if (game->flash_mode != FLASH_MODE_FLASHES)
        return;
    if (--game->flash_frame_timer == 0) {
        gfx_set_game_palette(asset_palette(PALETTE_ID_GAME));
        game->flash_frame_timer = FLASH_PALETTE_PERIOD;
    }
    const FlashList *list = game->flash_list;
    if (!list)
        return;

    /* The list's 0 terminator ends it with colour 0 black. */
    uint16_t word = 0;
    if (game->flash_pos < list->word_count) {
        word = list->words[game->flash_pos++];
    } else {
        game->flash_list = NULL;
        game->flash_pos = 0;
    }

    if (game->vga) {
        /* Kept: the original hands the word's green nibble to the palette call as blue and
         * its blue as green, so the two come out swapped. */
        const uint8_t *nibble_to_dac = asset_palettes()->nibble_to_dac;
        uint8_t rgb[1][3] = { { 0, 0, 0 } };
        if (word != 0) {
            rgb[0][0] = nibble_to_dac[(word >> 8) & 0x0f];
            rgb[0][1] = nibble_to_dac[word & 0x0f];
            rgb[0][2] = nibble_to_dac[(word >> 4) & 0x0f];
        }
        gfx_set_palette_dac(FLASH_COLOR, 1, (const uint8_t (*)[3])rgb);
    } else {
        GfxColor color = gfx_ega_color(game->flash_list ? FLASH_EGA_REGISTER : 0);
        gfx_set_palette(FLASH_COLOR, 1, &color);
    }
}
