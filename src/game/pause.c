/*
 * pause.c - keys while paused (see pause.h).
 */
#include "pause.h"

#include "input.h"
#include "sound.h"

/* make codes */
enum { KEY_O = 0x18, KEY_S = 0x1f, KEY_G = 0x22 };

enum {
    KEY_SAMPLE_MASK = 0x1f,
    KEY_SAMPLE_LAST = 0x1c,         /* samples past it play as KEY_SAMPLE_FALLBACK */
    KEY_SAMPLE_FALLBACK = 0x1b,
    KEY_SAMPLE_RATE = 1, KEY_SAMPLE_PRIORITY = 2,
};

/* The last key pressed sounds as a sample, unless one is playing. game86 1000:153e */
static void play_key_sample(void)
{
    if (game_sound_status().playing)
        return;
    int sample = input_state()->last_make_code_copy & KEY_SAMPLE_MASK;
    if (sample > KEY_SAMPLE_LAST)
        sample = KEY_SAMPLE_FALLBACK;
    game_sound_play((uint8_t)sample, KEY_SAMPLE_RATE, KEY_SAMPLE_PRIORITY);
}

void pause_tick_hook(GameState *game)
{
    switch (input_state()->last_make_code) {
    case KEY_G:
        game->draw_hook = DRAW_HOOK_PLAY;
        return;
    case KEY_O:
        game->draw_hook = DRAW_HOOK_PLAY;
        game->tick_hook = TICK_HOOK_PLAY;
        game->pause_message = 0;
        return;
    case KEY_S:
        if (!game->sound_key_latch) {
            game->sound_key_latch = true;
            game_sound_toggle(game);    /* game86 1000:1514, falling through */
            return;
        }
        break;
    default:
        game->sound_key_latch = false;  /* game86 1000:1538 */
        break;
    }
    play_key_sample();
}
