/* sound.c - sound requests with a log (see sound.h). */
#include "sound.h"

#include "snd.h"
#include "state.h"

static SoundRequest log_entries[SOUND_LOG_SIZE];
static unsigned log_count;
static bool status_overridden;
static SoundStatus status_override;

void game_sound_play(SampleId sample, uint8_t rate_index, uint8_t priority)
{
    if (log_count < SOUND_LOG_SIZE)
        log_entries[log_count] = (SoundRequest){ (uint8_t)sample, rate_index, priority };
    log_count++;
    snd_play(sample, rate_index, priority);
}

/* game86 1000:1514 */
void game_sound_toggle(GameState *game)
{
    game->sound_on = !game->sound_on;
    snd_set_enabled(game->sound_on);
}

SoundStatus game_sound_status(void)
{
    return status_overridden ? status_override : snd_status();
}

void game_sound_override_status(bool on, SoundStatus status)
{
    status_overridden = on;
    status_override = status;
}

unsigned game_sound_log_count(void)
{
    return log_count;
}

SoundRequest game_sound_log_at(unsigned i)
{
    return i < SOUND_LOG_SIZE ? log_entries[i] : (SoundRequest){ 0, 0, 0 };
}

void game_sound_log_clear(void)
{
    log_count = 0;
}
