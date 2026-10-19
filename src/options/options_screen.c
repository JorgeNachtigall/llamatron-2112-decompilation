/*
 * options_screen.c - see options_screen.h. The wording is the port's own; the values are what
 * install.exe write_config (1000:050f) and llama.exe inf_read (1000:01e7) give each field.
 */
#include "options_screen.h"

#include "draw.h"
#include "game_data.h"
#include "input.h"
#include "text.h"

#include <string.h>

enum {
    KEY_ENTER = 0x1c,
    PROCESSOR_DIGITS = 10,          /* llama.inf[6] = '0' + cpu */

    /* Layout (pixels) and game palette colours. */
    TITLE_Y = 8,
    FIRST_LINE_Y = 42,
    LABEL_X = 16,
    VALUE_X = 136,
    HELP_Y = 158,
    COLOR_TITLE = 2,                /* red (VGA and EGA) */
    COLOR_LABEL = 8,                /* cyan */
    COLOR_VALUE = 7,                /* sea green */
    COLOR_SELECTED = 4,             /* yellow (EGA: lime) */
    COLOR_HELP = 15,                /* grey */
};

static const SettingsSound sound_order[] = {
    SETTINGS_SOUND_BLASTER, SETTINGS_SOUND_ADLIB, SETTINGS_SOUND_ROLAND, SETTINGS_SOUND_NONE,
};
static const SettingsFlash flash_order[] = {
    SETTINGS_FLASH_ALL, SETTINGS_FLASH_PULSE_ONLY, SETTINGS_FLASH_NONE,
};

static const char *const line_labels[OPTIONS_LINE_COUNT] = {
    "Video", "Control", "Sound", "Processor", "Flashes",
};

void options_open(OptionsScreen *screen, const Settings *current)
{
    memset(screen, 0, sizeof *screen);
    screen->state = OPTIONS_EDITING;
    screen->line = OPTIONS_LINE_VIDEO;
    screen->settings = *current;
}

static int sound_index(SettingsSound sound)
{
    for (int i = 0; i < (int)(sizeof sound_order / sizeof *sound_order); i++)
        if (sound_order[i] == sound)
            return i;
    return 0;
}

static int flash_index(SettingsFlash flash)
{
    for (int i = 0; i < (int)(sizeof flash_order / sizeof *flash_order); i++)
        if (flash_order[i] == flash)
            return i;
    return 0;
}

static int wrap(int value, int count)
{
    return (value % count + count) % count;
}

/* Step the selected line's value by `step` (+1 / -1), wrapping around. */
static void change(OptionsScreen *screen, int step)
{
    Settings *s = &screen->settings;
    switch (screen->line) {
    case OPTIONS_LINE_VIDEO:
        s->vga = !s->vga;
        break;
    case OPTIONS_LINE_CONTROL:
        s->joystick = !s->joystick;
        break;
    case OPTIONS_LINE_SOUND:
        s->sound = sound_order[wrap(sound_index(s->sound) + step, (int)(sizeof sound_order / sizeof *sound_order))];
        break;
    case OPTIONS_LINE_PROCESSOR:
        s->cpu = (uint8_t)wrap((s->cpu < PROCESSOR_DIGITS ? s->cpu : 0) + step, PROCESSOR_DIGITS);
        break;
    case OPTIONS_LINE_FLASHES:
        s->flash = flash_order[wrap(flash_index(s->flash) + step, (int)(sizeof flash_order / sizeof *flash_order))];
        break;
    case OPTIONS_LINE_COUNT:
        break;
    }
}

void options_key(OptionsScreen *screen, uint8_t make_code, bool pressed)
{
    if (!pressed || screen->state != OPTIONS_EDITING)
        return;
    switch (make_code) {
    case PC_KEY_UP:
        screen->line = (OptionsLine)wrap((int)screen->line - 1, OPTIONS_LINE_COUNT);
        break;
    case PC_KEY_DOWN:
        screen->line = (OptionsLine)wrap((int)screen->line + 1, OPTIONS_LINE_COUNT);
        break;
    case PC_KEY_LEFT:
        change(screen, -1);
        break;
    case PC_KEY_RIGHT:
    case PC_KEY_SPACE:
        change(screen, +1);
        break;
    case KEY_ENTER:
        screen->state = OPTIONS_SAVED;
        break;
    case PC_KEY_ESCAPE:
    case OPTIONS_KEY:
        screen->state = OPTIONS_CANCELLED;
        break;
    default:
        break;
    }
}

const char *options_value_text(const OptionsScreen *screen, OptionsLine line)
{
    static char digit[2];
    const Settings *s = &screen->settings;
    switch (line) {
    case OPTIONS_LINE_VIDEO:
        return s->vga ? "VGA 256 colours" : "EGA 16 colours";
    case OPTIONS_LINE_CONTROL:
        return s->joystick ? "Joystick" : "Keyboard";
    case OPTIONS_LINE_SOUND:
        switch (s->sound) {
        case SETTINGS_SOUND_BLASTER: return "Sound Blaster";
        case SETTINGS_SOUND_ADLIB: return "AdLib";
        case SETTINGS_SOUND_ROLAND: return "Roland";
        default: return "None";
        }
    case OPTIONS_LINE_PROCESSOR:
        digit[0] = (char)('0' + s->cpu % PROCESSOR_DIGITS);
        digit[1] = 0;
        return digit;
    case OPTIONS_LINE_FLASHES:
        switch (s->flash) {
        case SETTINGS_FLASH_ALL: return "Flashes and pulsing";
        case SETTINGS_FLASH_PULSE_ONLY: return "Pulsing only";
        default: return "Neither";
        }
    case OPTIONS_LINE_COUNT:
        break;
    }
    return "";
}

static void draw_string(const DrawTarget *target, TextState *text, const char *s, int x, int y, uint8_t color)
{
    text_set_color(text, color);
    text->x = (int16_t)x;
    text->y = (int16_t)y;
    while (*s)
        text_draw_char(target, text, (unsigned char)*s++);
}

bool options_render(const OptionsScreen *screen, uint8_t pixels[GFX_HEIGHT][GFX_WIDTH],
                    GfxColor palette[GFX_PALETTE_SIZE])
{
    const PaletteSet *palettes = asset_palettes();
    const GfxGamePalette *colors = asset_palette(PALETTE_ID_GAME);
    if (!palettes || !colors || !asset_font_gfx())
        return false;
    text_use_game_assets();
    gfx_set_adapter(screen->settings.vga ? GFX_ADAPTER_VGA : GFX_ADAPTER_EGA, palettes->nibble_to_dac);
    gfx_set_game_palette(colors);
    memcpy(palette, gfx_palette(), sizeof(GfxColor) * GFX_PALETTE_SIZE);

    memset(pixels, 0, (size_t)GFX_HEIGHT * GFX_WIDTH);
    DrawTarget target = { &pixels[0][0], GFX_HEIGHT * DRAW_CELLS_PER_ROW, false, -1 };
    TextState text = { 0 };
    text_set_color(&text, COLOR_TITLE);
    text_draw_centred(&target, &text, "Options", TITLE_Y);
    for (int line = 0; line < OPTIONS_LINE_COUNT; line++) {
        int y = FIRST_LINE_Y + line * TEXT_LINE_STEP;
        bool selected = line == (int)screen->line;
        draw_string(&target, &text, line_labels[line], LABEL_X, y, selected ? COLOR_SELECTED : COLOR_LABEL);
        draw_string(&target, &text, options_value_text(screen, (OptionsLine)line), VALUE_X, y,
                    selected ? COLOR_SELECTED : COLOR_VALUE);
    }
    text_set_color(&text, COLOR_HELP);
    text_draw_centred(&target, &text, "Enter saves   Esc leaves", HELP_Y);
    text_draw_centred(&target, &text, "Used from the next game", HELP_Y + TEXT_LINE_STEP);
    return true;
}
