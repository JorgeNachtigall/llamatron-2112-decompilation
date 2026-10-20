/* Fixture for NoDsOffsets.cmake: every call below must be reported. */
#include "game_data.h"

static const GfxSprite *game_sprite_at(uint16_t bank_offset);

void bad(void)
{
    const GfxGamePalette *black = asset_palette_at(0x2175);
    const GameText *text = asset_text_at(id_from_somewhere());
    const GfxSprite *mark = game_sprite_at( 0x245a );
    (void)black; (void)text; (void)mark;
}
