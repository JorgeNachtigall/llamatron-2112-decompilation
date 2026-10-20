/*
 * pickups.c - the effects of the ten pickup kinds (PICKUP_*, tbl_pickup_effects, CS:0F7E),
 * applied when think_1c finds its pickup touching a ship. The kind is pickup.pickup_kind, set
 * by become_pickup in hit.c.
 */
#include "entities.h"

#include "game_data.h"
#include "score.h"
#include "sound.h"

enum { PICKUP_EFFECT_COUNT = 10 };

/* Ticks the llama power-up lasts (the play tick counts player.powerup down). */
enum { LLAMA_POWERUP_TICKS = 0x320 };
/* PICKUP_POWER_SHOTS: level_flags goes positive (shots fly on through what they hit, in
 * their power-up sprites) and stays so for the rest of the level. */
enum { POWER_SHOTS_LEVEL_FLAGS = 0x32 };
enum { SHIELD_TICKS = 500 };
/* PICKUP_POINTS: 2,000 points, two steps at the thousands digit of score_add. */
enum { PLACE_THOUSANDS = 4, POINTS_STEPS = 2 };

typedef void (*PickupEffectFn)(GameState *game, unsigned touched);

/* game86 1000:100f: the llama power-up (llamas come to the ships while it lasts). */
static void pickup_effect_00(GameState *game, unsigned touched)
{
    (void)touched;
    game->player.powerup = LLAMA_POWERUP_TICKS;
    game_sound_play(SAMPLE_ID_PICKUP, 2, 8);
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
    game_sound_play(SAMPLE_ID_EXTRA_LIFE, 2, 8);
}

/* game86 1000:0fce: power shots for the rest of the level. */
static void pickup_effect_03(GameState *game, unsigned touched)
{
    (void)touched;
    game->level_flags = POWER_SHOTS_LEVEL_FLAGS;
    game->shot_power_kept = 1;
}

/* game86 1000:0fdb: one player only. With enemies left, multi-shot for the rest of the wave
 * (3-way spread, and every score counts double); on an empty field it queues itself
 * again for the next kill. */
static void pickup_effect_04(GameState *game, unsigned touched)
{
    (void)touched;
    if (game->options & OPT_TWO_PLAYERS)
        return;
    if (game->enemies_alive != 0)
        game->player.multi_shot = 1;
    else
        game->player.pickup_pending = pickup_pending_for(PICKUP_MULTI_SHOT);
}

/* game86 1000:0fb5: smart bomb. */
static void pickup_effect_05(GameState *game, unsigned touched)
{
    (void)touched;
    game->smart_bomb = true;
    entity_start_flash(game, FLASH_LIST_SMART_BOMB);
    game_sound_play(SAMPLE_ID_SMART_BOMB, 1, 0x0a);
}

/* game86 1000:0fa2: shield for 500 ticks. */
static void pickup_effect_06(GameState *game, unsigned touched)
{
    (void)touched;
    game->shield_timer = SHIELD_TICKS;
    game_sound_play(SAMPLE_ID_EXTRA_LIFE, 2, 0x0a);
}

/* game86 1000:0f92: shots bounce off the edges. */
static void pickup_effect_07(GameState *game, unsigned touched)
{
    (void)touched;
    game->shots_bounce = true;
}

/* game86 1000:0f99: 2,000 points. */
static void pickup_effect_08(GameState *game, unsigned touched)
{
    (void)touched;
    score_add(game, PLACE_THOUSANDS, POINTS_STEPS);
}

/* game86 1000:37c2: a deadly "pickup" (also entered from think_11/12/1b). The same guards as
 * the player-touch tail, with its own flash and sound, then the player dies. */
void deadly_touch(GameState *game, unsigned touched)
{
    if (game->wave_result == WAVE_CLEARED || game->shield_timer != 0 || game->demo_mode != 0)
        return;
    entity_start_flash(game, FLASH_LIST_DEADLY_TOUCH);
    if (game->smart_bomb)
        return;
    game_sound_play(SAMPLE_ID_DEADLY_TOUCH, 3, 0x1e);
    game->death_sound_timer = 0x14;
    player_kill(game, touched);
}

/* game86 1000:37c2 (the table entry is deadly_touch itself) */
static void pickup_effect_09(GameState *game, unsigned touched)
{
    deadly_touch(game, touched);
}

/* CS:0F7E */
static const PickupEffectFn pickup_effects[PICKUP_EFFECT_COUNT] = {
    [PICKUP_LLAMA_POWERUP] = pickup_effect_00,
    [PICKUP_WARP] = pickup_effect_01,
    [PICKUP_EXTRA_LIFE] = pickup_effect_02,
    [PICKUP_POWER_SHOTS] = pickup_effect_03,
    [PICKUP_MULTI_SHOT] = pickup_effect_04,
    [PICKUP_SMART_BOMB] = pickup_effect_05,
    [PICKUP_SHIELD] = pickup_effect_06,
    [PICKUP_BOUNCE] = pickup_effect_07,
    [PICKUP_POINTS] = pickup_effect_08,
    [PICKUP_DEADLY] = pickup_effect_09,
};

/* game86 1000:0f64..0f6e */
void pickup_collect(GameState *game, Entity *e, unsigned touched)
{
    /* Kinds outside the table jump into whatever code follows it in the original; pickups
     * only ever get kinds 0..9 (become_pickup). */
    if ((uint16_t)e->pickup.pickup_kind < PICKUP_EFFECT_COUNT)
        pickup_effects[e->pickup.pickup_kind](game, touched);
}
