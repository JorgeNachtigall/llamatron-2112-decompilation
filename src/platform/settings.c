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

enum { INF_READ_SIZE = 9, ASCII_UPPER = 0xDF };

/* llama.inf: [0] V/E, [2] J/K, [4] S/A/R/N, [6] cpu digit, [8] Y/P/N, as llama.exe inf_read
 * (1000:01e7) takes them: a shorter file reads as zeros after its end; [0], [2], [4] and [8]
 * are upper-cased with AND 0DFh (so 'v' and also 0xD6 count as 'V'); [0], [2] and [4] must
 * then be one of their letters, or the launcher stops ("No or Invalid Game Configuration").
 * [6] - '0' and [8] are not checked; [8] other than P or Y is no flashing. */
bool settings_parse_inf(const uint8_t *data, size_t size, Settings *out)
{
    uint8_t inf[INF_READ_SIZE] = { 0 };
    if (size > 0)
        memcpy(inf, data, size < sizeof inf ? size : sizeof inf);
    uint8_t adapter = inf[0] & ASCII_UPPER, control = inf[2] & ASCII_UPPER;
    uint8_t sound = inf[4] & ASCII_UPPER, flash = inf[8] & ASCII_UPPER;
    if ((adapter != 'V' && adapter != 'E') || (control != 'J' && control != 'K') ||
        (sound != 'N' && sound_from_char(sound) == SETTINGS_SOUND_NONE))
        return false;
    out->vga = adapter == 'V';
    out->joystick = control == 'J';
    out->sound = sound_from_char(sound);
    out->cpu = (uint8_t)(inf[6] - '0');
    out->flash = flash == 'Y' ? SETTINGS_FLASH_ALL : flash == 'P' ? SETTINGS_FLASH_PULSE_ONLY : SETTINGS_FLASH_NONE;
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
