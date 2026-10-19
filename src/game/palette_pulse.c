/*
 * palette_pulse.c - colour 1 pulsing with the timer (see palette_pulse.h).
 */
#include "palette_pulse.h"

#include "game_data.h"
#include "gfx.h"

enum {
    PULSE_COLOR = 1,
    EGA_TICKS_PER_STEP = 4,
};

/* A pulse word: red in bits 8..11, green in bits 0..3, blue in bits 4..7, each a 4-bit
 * level for nibble_to_dac. game86 1000:3e6c..3e90 (DH, CH, CL for INT 10h AX=1010h) */
static void set_vga_pulse(uint16_t word)
{
    const uint8_t *nibble_to_dac = asset_palettes()->nibble_to_dac;
    uint8_t rgb[1][3] = { {
        nibble_to_dac[(word >> 8) & 0x0f],
        nibble_to_dac[word & 0x0f],
        nibble_to_dac[(word >> 4) & 0x0f],
    } };
    gfx_set_palette_dac(PULSE_COLOR, 1, (const uint8_t (*)[3])rgb);
}

void palette_pulse_tick(GameState *game)
{
    if (game->flash_mode == 0 || !game->pulse_enable)
        return;
    const PaletteSet *palettes = asset_palettes();
    if (game->vga) {
        set_vga_pulse(palettes->pulse_vga[game->tick_count % PULSE_VGA_STEPS]);
    } else if (game->tick_count % EGA_TICKS_PER_STEP == 0) {   /* game86 1000:3e9c */
        game->pulse_index = (int16_t)(uint16_t)(game->pulse_index + 1);
        unsigned step = (uint16_t)game->pulse_index % PULSE_EGA_STEPS;
        GfxColor color = gfx_ega_color(palettes->pulse_ega[step]);
        gfx_set_palette(PULSE_COLOR, 1, &color);
    }
}
