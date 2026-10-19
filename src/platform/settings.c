/*
 * settings.c - llama.inf text format and defaults (see settings.h). Pure C, no SDL.
 */
#include "settings.h"

#include "assets.h"

#include <string.h>

enum { CONFIG_DAT_SIZE = 12 };

Settings settings_shipped_defaults(void)
{
    Settings s = { true, false, SETTINGS_SOUND_NONE, 0, SETTINGS_FLASH_NONE };
    return s;
}

static SettingsSound sound_from_char(uint8_t c)
{
    switch (c) {
    case 'A': return SETTINGS_SOUND_ADLIB;
    case 'S': return SETTINGS_SOUND_BLASTER;
    case 'R': return SETTINGS_SOUND_ROLAND;
    default: return SETTINGS_SOUND_NONE;
    }
}

static uint8_t sound_to_char(SettingsSound sound)
{
    switch (sound) {
    case SETTINGS_SOUND_ADLIB: return 'A';
    case SETTINGS_SOUND_BLASTER: return 'S';
    case SETTINGS_SOUND_ROLAND: return 'R';
    default: return 'N';
    }
}

/* llama.inf: [0] V/E, [2] J/K, [4] S/A/R/N, [6] cpu digit, [8] Y/P/N (llama.exe 1000:0219). */
bool settings_parse_inf(const uint8_t *data, size_t size, Settings *out)
{
    if (size < 9)
        return false;
    out->vga = data[0] == 'V';
    out->joystick = data[2] == 'J';
    out->sound = sound_from_char(data[4]);
    out->cpu = (uint8_t)(data[6] - '0');
    out->flash = data[8] == 'Y' ? SETTINGS_FLASH_ALL : data[8] == 'P' ? SETTINGS_FLASH_PULSE_ONLY : SETTINGS_FLASH_NONE;
    return true;
}

void settings_format_inf(const Settings *s, uint8_t out[SETTINGS_INF_SIZE])
{
    out[0] = s->vga ? 'V' : 'E';
    out[1] = ' ';
    out[2] = s->joystick ? 'J' : 'K';
    out[3] = ' ';
    out[4] = sound_to_char(s->sound);
    out[5] = ' ';
    out[6] = (uint8_t)('0' + s->cpu);
    out[7] = ' ';
    out[8] = s->flash == SETTINGS_FLASH_ALL ? 'Y' : s->flash == SETTINGS_FLASH_PULSE_ONLY ? 'P' : 'N';
    out[9] = 0;
}

/* config.dat (tools/extract/FORMATS.md): "LCFG", u16 version 1, present, vga, joystick, sound,
 * cpu, flash_mode. */
Settings settings_defaults(void)
{
    Settings s = settings_shipped_defaults();
    size_t size = 0;
    const uint8_t *cfg = asset_get("config.dat", &size);
    if (cfg && size >= CONFIG_DAT_SIZE && memcmp(cfg, "LCFG", 4) == 0 && cfg[4] == 1 && cfg[5] == 0 &&
        cfg[6] != 0) {
        s.vga = cfg[7] != 0;
        s.joystick = cfg[8] != 0;
        s.sound = (SettingsSound)cfg[9];
        s.cpu = cfg[10];
        s.flash = (SettingsFlash)cfg[11];
    }
    return s;
}
