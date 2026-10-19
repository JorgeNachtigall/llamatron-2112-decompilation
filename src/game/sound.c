/* sound.c - sound requests with a log (see sound.h). */
#include "sound.h"

#include "snd.h"

static SoundRequest log_entries[SOUND_LOG_SIZE];
static unsigned log_count;

void game_sound_play(uint8_t sample, uint8_t rate_index, uint8_t priority)
{
    if (log_count < SOUND_LOG_SIZE)
        log_entries[log_count] = (SoundRequest){ sample, rate_index, priority };
    log_count++;
    snd_play(sample, rate_index, priority);
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
