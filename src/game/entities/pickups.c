/*
 * pickups.c - the effects of the ten pickup kinds (tbl_pickup_effects, CS:0F7E), applied when
 * think_1c finds its pickup touching a ship. The kind is the pickup's timer field, set by
 * become_pickup in hit.c.
 */
#include "entities.h"

#include "game_data.h"
#include "score.h"
#include "sound.h"

enum { PICKUP_EFFECT_COUNT = 10 };

typedef void (*PickupEffectFn)(GameState *game, unsigned touched);

/* game86 1000:100f */
static void pickup_effect_00(GameState *game, unsigned touched)
{
    (void)touched;
    game->player.powerup = 0x320;
    game_sound_play(0x1c, 2, 8);
}

/* game86 1000:0ff7: warp ahead. */
static void pickup_effect_01(GameState *game, unsigned touched)
{
    (void)touched;
    game->wave_result = WAVE_WARP;
}

/* game86 1000:0ffe: extra life. */
static void pickup_effect_02(GameState *game, unsigned touched)
{
    (void)touched;
    game->player.lives++;
    game_sound_play(0x19, 2, 8);
}

/* game86 1000:0fce */
static void pickup_effect_03(GameState *game, unsigned touched)
{
    (void)touched;
    game->level_flags = 0x32;
    game->powerup_0652 = 1;
}

/* game86 1000:0fdb: one player only. With enemies left, the rest of the wave scores double
 * (score_scale 1); on an empty field it queues pickup 4 instead. */
static void pickup_effect_04(GameState *game, unsigned touched)
{
    (void)touched;
    if (game->options & OPT_TWO_PLAYERS)
        return;
    if (game->enemies_alive != 0)
        game->player.score_scale = 1;
    else
        game->player.pickup_pending = 5;
}

/* game86 1000:0fb5: smart bomb. */
static void pickup_effect_05(GameState *game, unsigned touched)
{
    (void)touched;
    game->smart_bomb = true;
    game->flash_list = asset_flash_list(FLASH_LIST_06FD);
    game_sound_play(0x10, 1, 0x0a);
}

/* game86 1000:0fa2: shield for 500 ticks. */
static void pickup_effect_06(GameState *game, unsigned touched)
{
    (void)touched;
    game->shield_timer = 500;
    game_sound_play(0x19, 2, 0x0a);
}

/* game86 1000:0f92: shots bounce off the edges. */
static void pickup_effect_07(GameState *game, unsigned touched)
{
    (void)touched;
    game->shots_bounce = true;
}

/* game86 1000:0f99: 2,000 points (two steps at the thousands digit). */
static void pickup_effect_08(GameState *game, unsigned touched)
{
    (void)touched;
    score_add(game, 4, 2);
}

/* game86 1000:37c2: a deadly "pickup" (also entered from think_11/12/1b). The same guards as
 * the player-touch tail, with its own flash and sound, then the player dies. */
void deadly_touch(GameState *game, unsigned touched)
{
    if (game->wave_result == WAVE_CLEARED || game->shield_timer != 0 || game->demo_mode != 0)
        return;
    game->flash_list = asset_flash_list(FLASH_LIST_06CD);
    if (game->smart_bomb)
        return;
    game_sound_play(0x1d, 3, 0x1e);
    game->death_sound_timer = 0x14;
    player_kill(game, touched);
}

static void pickup_effect_09(GameState *game, unsigned touched)
{
    deadly_touch(game, touched);
}

static const PickupEffectFn pickup_effects[PICKUP_EFFECT_COUNT] = {
    pickup_effect_00, pickup_effect_01, pickup_effect_02, pickup_effect_03, pickup_effect_04,
    pickup_effect_05, pickup_effect_06, pickup_effect_07, pickup_effect_08, pickup_effect_09,
};

/* game86 1000:0f64..0f6e */
void pickup_collect(GameState *game, Entity *e, unsigned touched)
{
    /* Kinds outside the table jump into whatever code follows it in the original; pickups
     * only ever get kinds 0..9 (become_pickup). */
    if ((uint16_t)e->timer < PICKUP_EFFECT_COUNT)
        pickup_effects[e->timer](game, touched);
}
