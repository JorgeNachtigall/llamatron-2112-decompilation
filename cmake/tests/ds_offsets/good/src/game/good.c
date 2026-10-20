/* Fixture for NoDsOffsets.cmake: nothing here may be reported.
 * Comments may cite the original: asset_palette_at(0x2175), game_sprite_at(0x245A). */
#include "game_data.h"

void good(const DrawTarget *target, int y)
{
    const GfxGamePalette *black = asset_palette(PALETTE_ID_BLACK);   // not asset_palette_at(0x2175)
    const char *quoted = "asset_text_at(0x09B5)";
    draw_char_at(target, 8, y);         /* a coordinate, not an offset */
    print_at(0x10, y);                  /* two hex digits: a coordinate */
    my_asset_text_at_cache(0x09B5);     /* not an *_at( call */
    (void)black; (void)quoted;
}
