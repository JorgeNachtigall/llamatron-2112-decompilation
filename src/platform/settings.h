/*
 * settings.h - the player's configuration, i.e. what llama.inf held (install.exe writes it,
 * llama.exe parses it at 1000:0219 and passes it to the game on the command tail).
 *
 * Stored by the platform in the user's data directory (native: SDL_GetPrefPath, or
 * $LLAMATRON_PREF_DIR if set; web: IDBFS at /persist) as a llama.inf-format text file, so the
 * file can be exchanged with the original. Defaults: the saved file, else the user's own llama.inf imported by the extractor
 * (config.dat), else the shipped llama.inf "V K N 0 N". No game logic here.
 */
#ifndef LLAMATRON_SETTINGS_H
#define LLAMATRON_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sound type as passed to game86 (PSP:89h). */
typedef enum SettingsSound {
    SETTINGS_SOUND_ADLIB = 0x00,       /* 'A' */
    SETTINGS_SOUND_BLASTER = 0x0c,     /* 'S' */
    SETTINGS_SOUND_ROLAND = 0x18,      /* 'R' */
    SETTINGS_SOUND_NONE = 0xff,        /* 'N' */
} SettingsSound;

/* Flash option as game86 sees it ([0x589]): llama.exe maps 'P' -> 1, 'Y' -> 2, else 0. */
typedef enum SettingsFlash {
    SETTINGS_FLASH_NONE = 0,           /* 'N': no flashes or pulsing */
    SETTINGS_FLASH_PULSE_ONLY = 1,     /* 'P': no screen flashes */
    SETTINGS_FLASH_ALL = 2,            /* 'Y': normal */
} SettingsFlash;

typedef struct Settings {
    bool vga;                          /* llama.inf[0] 'V' (else 'E') */
    bool joystick;                     /* llama.inf[2] 'J' (else 'K') */
    SettingsSound sound;               /* llama.inf[4] */
    uint8_t cpu;                       /* llama.inf[6] - '0' (0 = 8086 build) */
    SettingsFlash flash;               /* llama.inf[8] */
} Settings;

enum { SETTINGS_INF_SIZE = 10 };       /* 9 characters + NUL, as install.exe writes it */

/* The shipped llama.inf: "V K N 0 N". */
Settings settings_shipped_defaults(void);

/* Parse llama.inf text; false if it is too short (out keeps its previous values then). */
bool settings_parse_inf(const uint8_t *data, size_t size, Settings *out);
/* Write llama.inf text into out[SETTINGS_INF_SIZE]. */
void settings_format_inf(const Settings *s, uint8_t out[SETTINGS_INF_SIZE]);

/* Defaults when nothing was saved: config.dat (via the asset store) if it holds a valid
 * llama.inf, else the shipped values. */
Settings settings_defaults(void);

/* Persistent storage (platform_sdl.c): load returns the saved settings or settings_defaults();
 * save writes them and, on the web, flushes IDBFS to the browser's storage. */
Settings plat_settings_load(void);
bool plat_settings_save(const Settings *settings);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_SETTINGS_H */
