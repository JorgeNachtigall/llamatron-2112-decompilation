/* score.c - score awards (see score.h). */
#include "score.h"

#include "snd.h"

enum {
    EXTRA_LIFE_PLACE = 4,           /* a carry out of this digit (thousands) earns a life */
    EXTRA_LIFE_SAMPLE = 0x19,
    EXTRA_LIFE_RATE = 1,
    EXTRA_LIFE_PRIORITY = 6,
};

static uint32_t place_value(int place)
{
    uint32_t value = 1;
    for (int d = place; d < SCORE_DIGITS - 1; d++)
        value *= 10u;
    return value;
}

/* game86 1000:0335 */
void score_add(GameState *game, int place, int increments)
{
    if (game->demo_mode)
        return;

    int player = (game->options & OPT_CURRENT_IS_P2) ? 1 : 0;
    uint32_t unit = place_value(place);
    uint32_t carry_limit = place_value(EXTRA_LIFE_PLACE - 1);    /* 10,000 */
    /* Both loops in the original run at least once (count down to -1). */
    int repeats = game->player.score_scale > 0 ? game->player.score_scale + 1 : 1;
    int steps = increments > 1 ? increments : 1;

    for (int r = 0; r < repeats; r++) {
        for (int s = 0; s < steps; s++) {
            uint32_t before = game->score[player];
            if (place >= EXTRA_LIFE_PLACE && before % carry_limit + unit >= carry_limit) {
                game->player.extra_life_flag = 3;
                snd_play(EXTRA_LIFE_SAMPLE, EXTRA_LIFE_RATE, EXTRA_LIFE_PRIORITY);
            }
            game->score[player] = (before + unit) % SCORE_MODULUS;
        }
    }
    game->hud_dirty = true;
}
