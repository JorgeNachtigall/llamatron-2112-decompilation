/*
 * draw_page.c - whole-page copies between the shown page, the draw page and the background.
 *
 * The original copies a page plane by plane through the EGA latches; the result is simply the
 * whole page copied.
 */
#include "draw.h"

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
