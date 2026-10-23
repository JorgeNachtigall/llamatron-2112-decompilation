/*
 * options_screen.h - the port's options screen (PLAN M7): edits exactly the fields of llama.inf
 * that install.exe's option menus set (video, control, sound, the processor digit, flashes),
 * drawn with the game's own font and text engine. Host-side only: not part of the original
 * (docs/DEVIATIONS.md #23), and the game's code paths never see it.
 *
 * The host opens it from the title (OPTIONS_KEY), feeds it PC make codes, draws it once per
 * frame into a 320x200 indexed picture and, when it is finished, stores and applies the
 * settings if the player saved them (plat_settings_save -> settings_format_inf).
 *
 * Keys: Up/Down choose a line, Left/Right (or Space) change it, Enter saves, Esc or the
 * options key (O) again leaves without saving.
 */
#ifndef LLAMATRON_OPTIONS_SCREEN_H
#define LLAMATRON_OPTIONS_SCREEN_H

#include "gfx.h"
#include "settings.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OPTIONS_KEY = 0x18,             /* O (make code): opens the screen from the title */
};

typedef enum OptionsLine {
    OPTIONS_LINE_VIDEO,             /* llama.inf[0] V / E */
    OPTIONS_LINE_CONTROL,           /* llama.inf[2] K / J */
    OPTIONS_LINE_SOUND,             /* llama.inf[4] S / A / R / N */
    OPTIONS_LINE_PROCESSOR,         /* llama.inf[6] digit */
    OPTIONS_LINE_FLASHES,           /* llama.inf[8] Y / P / N */
    OPTIONS_LINE_COUNT
} OptionsLine;

typedef enum OptionsState {
    OPTIONS_EDITING,
    OPTIONS_SAVED,                  /* Enter: the host stores and applies settings */
    OPTIONS_CANCELLED,              /* Esc / OPTIONS_KEY: nothing changes */
} OptionsState;

typedef struct OptionsScreen {
    OptionsState state;
    OptionsLine line;               /* the selected line */
    Settings settings;              /* as edited */
} OptionsScreen;

void options_open(OptionsScreen *screen, const Settings *current);

/* A key moved (PC make code, without the break bit). Only presses count. */
void options_key(OptionsScreen *screen, uint8_t make_code, bool pressed);

static inline bool options_finished(const OptionsScreen *screen) { return screen->state != OPTIONS_EDITING; }

/* The text of a line's current value (static storage). */
const char *options_value_text(const OptionsScreen *screen, OptionsLine line);

/* Draw the screen into `pixels` (320x200 palette indices) and its colours into `palette`, in the
 * game's font and palette for the edited video mode. Needs game_data_load(); changes the gfx
 * palette state (the game sets its own when it starts). Returns false without game data. */
bool options_render(const OptionsScreen *screen, uint8_t pixels[GFX_HEIGHT][GFX_WIDTH],
                    GfxColor palette[GFX_PALETTE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_OPTIONS_SCREEN_H */
