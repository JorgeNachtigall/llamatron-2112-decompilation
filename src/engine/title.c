/*
 * title.c - front screen (fs.exe behaviour, see title.h).
 */
#include "title.h"

#include "assets.h"

#include <stdlib.h>
#include <string.h>

enum { JOY_STICK_A_BUTTONS = 0x30 };

/* A 6-bit DAC level as the 8-bit value a VGA shows. */
static uint8_t dac6_to_8(uint8_t level)
{
    level &= 0x3f;
    return (uint8_t)(level << 2 | level >> 4);
}

/* fs.exe 1000:01bf (picture display) */
bool title_load(TitleScreen *title, GfxAdapter adapter, InputDevice device)
{
    memset(title, 0, sizeof *title);
    title->adapter = adapter;
    title->device = device;
    title->phase = TITLE_WAITING;

    bool vga = adapter == GFX_ADAPTER_VGA;
    title->width = vga ? TITLE_VGA_WIDTH : TITLE_EGA_WIDTH;
    title->height = vga ? TITLE_VGA_HEIGHT : TITLE_EGA_HEIGHT;
    size_t pixel_count = (size_t)title->width * (size_t)title->height;
    title->pixels = malloc(pixel_count);
    if (!title->pixels)
        return false;

    if (vga) {
        /* 1000:01c9: NEWTIT rows 0-199, NEWTIT2 rows 200-399, DAC from NEWTIT's CMAP >> 2. */
        const uint8_t *pixels = asset_get_exact("title_vga.idx8", pixel_count);
        const uint8_t *dac = asset_get_exact("title_vga.pal", GFX_PALETTE_SIZE * 3);
        if (!pixels || !dac) {
            title_free(title);
            return false;
        }
        memcpy(title->pixels, pixels, pixel_count);
        for (int i = 0; i < GFX_PALETTE_SIZE; i++) {
            title->palette[i].r = dac6_to_8(dac[i * 3 + 0]);
            title->palette[i].g = dac6_to_8(dac[i * 3 + 1]);
            title->palette[i].b = dac6_to_8(dac[i * 3 + 2]);
        }
    } else {
        /* 1000:0216: TRONEGA, then INT 10h AX=1002h with the 17 registers built from its CMAP. */
        const uint8_t *pixels = asset_get_exact("title_ega.idx4", pixel_count);
        const uint8_t *registers = asset_get_exact("title_ega.pal", TITLE_EGA_REGISTERS);
        if (!pixels || !registers) {
            title_free(title);
            return false;
        }
        for (int i = 0; i < GFX_GAME_COLORS; i++)
            title->palette[i] = gfx_ega_color(registers[i]);
        for (size_t i = 0; i < pixel_count; i++)
            title->pixels[i] = pixels[i] & 0x0f;
    }
    return true;
}

void title_free(TitleScreen *title)
{
    free(title->pixels);
    title->pixels = NULL;
}

/* Keys that do not put a character into the BIOS buffer, so INT 21h/08h keeps waiting:
 * Ctrl, Shift, Alt, Caps/Num/Scroll Lock. */
static bool key_produces_character(uint8_t make_code)
{
    switch (make_code) {
    case 0x1d: case 0x2a: case 0x36: case 0x38: case 0x3a: case 0x45: case 0x46:
        return false;
    default:
        return true;
    }
}

static void start_leaving(TitleScreen *title)
{
    title->phase = TITLE_LEAVING;
    title->retraces_left = TITLE_EXIT_RETRACES;
}

/* fs.exe 1000:009b (keyboard branch: INT 21h/08h) */
void title_key(TitleScreen *title, uint8_t make_code, bool pressed)
{
    if (title->phase != TITLE_WAITING || title->device != INPUT_DEVICE_KEYBOARD)
        return;
    if (pressed && key_produces_character(make_code))
        start_leaving(title);
}

/* fs.exe 1000:009b (joystick branch) and 1000:0142 (exit retraces) */
void title_frame(TitleScreen *title, uint8_t joystick_buttons)
{
    switch (title->phase) {
    case TITLE_WAITING:
        if (title->device == INPUT_DEVICE_JOYSTICK && (joystick_buttons & JOY_STICK_A_BUTTONS))
            start_leaving(title);
        break;
    case TITLE_LEAVING:
        if (--title->retraces_left <= 0)
            title->phase = TITLE_DONE;
        break;
    case TITLE_DONE:
        break;
    }
}
