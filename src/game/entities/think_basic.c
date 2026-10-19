/*
 * think_basic.c - the simple per-tick think handlers (hatching, timers, straight movers,
 * the swarm) and the player-touch rules they share (see entities.h).
 */
#include "entities.h"
#include "game_data.h"
#include "sound.h"

enum {
    TOUCH_RANGE_X = 16,
    TOUCH_RANGE_Y = 10,
    PLAYER_DYING = -2,              /* respawn_timer while the death sequence runs */
    FEW_ENEMIES_LEFT = 10,
    DEATH_SOUND_DELAY = 0x14,
    HATCH_STEP = 2,
    FADE_TICKS = 15,                /* think_02 */
    BONUS_COUNTDOWN_RESET = 2,      /* a death restarts the bonus countdown (1000:441d) */
};

/* Death sounds (1000:4435..4450) */
static const SoundRequest SOUND_DEATH_LAST_ENEMIES = { 0x1d, 3, 0x1e };
static const SoundRequest SOUND_DEATH = { 0x11, 0, 0x1e };

static void play(SoundRequest s)
{
    game_sound_play(s.sample, s.rate_index, s.priority);
}

static int16_t magnitude16(int16_t v)
{
    return v < 0 ? (int16_t)(uint16_t)-v : v;
}

static bool overlaps_ship(const Entity *e, Point16 ship)
{
    return magnitude16((int16_t)(uint16_t)(e->x - ship.x)) < TOUCH_RANGE_X
        && magnitude16((int16_t)(uint16_t)(e->y - ship.y)) < TOUCH_RANGE_Y;
}

void entity_release(GameState *game, Entity *e)
{
    e->think = ENTITY_FREE;
    e->hatch_into = ENTITY_FREE;
    game->free_slots++;
}

/* game86 1000:4b49 */
unsigned entity_hits_player(const GameState *game, const Entity *e)
{
    unsigned touched = 0;
    if (game->respawn_timer[0] == 0 && overlaps_ship(e, game->ship[0]))
        touched |= TOUCH_P1;
    if (game->respawn_timer[1] >= 0 && (game->options & OPT_DOUBLE_CONTROLLER)
        && overlaps_ship(e, game->ship[1]))
        touched |= TOUCH_P2;
    return touched;
}

/* game86 1000:43f7 */
void entity_touch_player(GameState *game, unsigned touched)
{
    if (game->wave_result == WAVE_CLEARED || game->shield_timer != 0 || game->demo_mode)
        return;
    game->flash_list = asset_flash_list(FLASH_LIST_PLAYER_DEATH);
    if (game->smart_bomb)
        return;
    player_kill(game, touched);
}

/* game86 1000:441d */
void player_kill(GameState *game, unsigned touched)
{
    game->player.bonus_countdown = BONUS_COUNTDOWN_RESET;
    if (touched & TOUCH_P2) {
        /* P2 wins when both are touched; outside double-controller mode nobody dies. */
        if (!(game->options & OPT_DOUBLE_CONTROLLER))
            return;
        game->respawn_timer[1] = PLAYER_DYING;
    } else {
        game->respawn_timer[0] = PLAYER_DYING;
    }

    if (game->enemies_alive < FEW_ENEMIES_LEFT) {
        game->death_sound_timer = DEATH_SOUND_DELAY;
        play(SOUND_DEATH_LAST_ENEMIES);
    } else {
        play(SOUND_DEATH);
    }
}

bool entity_check_touch(GameState *game, const Entity *e)
{
    unsigned touched = entity_hits_player(game, e);
    if (touched)
        entity_touch_player(game, touched);
    return touched != 0;
}

/* Spawn marker: counts down in steps of 2, then becomes its enemy type, moves on to the next
 * animation state and turns solid. Also think 04, 07, 0a. game86 1000:43dc */
void think_00(GameState *game, Entity *e)
{
    (void)game;
    e->scale = (int16_t)(uint16_t)(e->scale - HATCH_STEP);
    if (e->scale >= 0)
        return;
    e->think = e->hatch_into;
    e->anim++;
    e->solid = 1;
}

/* Static enemy that only kills on contact. game86 1000:43f1 */
void think_01(GameState *game, Entity *e)
{
    (void)entity_check_touch(game, e);
}

/* Grows for 15 ticks, then disappears. game86 1000:43c9 */
void think_02(GameState *game, Entity *e)
{
    e->scale++;
    if (e->scale >= FADE_TICKS)
        entity_release(game, e);
}

/* Swarm member: every swarm_reaim_period ticks it jumps once towards its target ship, by at
 * most the level's enemy speed, and flips its animation frame. game86 1000:4ac9 */
void think_03(GameState *game, Entity *e)
{
    game->swarm_count++;
    if (game->wave_intro_timer != 0)
        return;
    if (entity_check_touch(game, e))
        return;
    if (--e->timer >= 0)
        return;
    e->timer = game->player.swarm_reaim_period;
    /* The original picks the ship again with the loop's rule (4af0..4b00), which is
     * game->target. */
    int16_t dx = (int16_t)(uint16_t)(game->target.x - e->x);
    int16_t dy = (int16_t)(uint16_t)(game->target.y - e->y);
    vector_shrink_to(&dx, &dy, game->player.enemy_speed_x, game->player.enemy_speed_y);
    e->vx = dx;
    e->vy = dy;
    e->anim_toggle ^= 1;
    entity_move_bounce(game, e);
}

/* Lives for `timer` ticks. game86 1000:4ec8 */
void think_09(GameState *game, Entity *e)
{
    if (--e->timer < 0)
        entity_release(game, e);
}

/* Enemy shot flying straight; it is gone (and no longer counts as an enemy) once it would
 * leave the open playfield. game86 1000:504f */
void think_0d(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(0x41 + ((game->tick_count >> 2) & 3));
    int16_t x = (int16_t)(uint16_t)(e->x + e->vx);
    if (x > BOUNCE_X_MIN && x < BOUNCE_X_MAX) {
        e->x = x;
        int16_t y = (int16_t)(uint16_t)(e->y + e->vy);
        if (y > BOUNCE_Y_MIN && y < 0xc0) {
            e->y = y;
            return;
        }
    }
    entity_release(game, e);
    game->enemies_alive--;
}

/* Animated bouncer that dies at the first wall it hits. Also the tail of think_19.
 * game86 1000:3cea */
void think_0f(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    entity_move_bounce(game, e);
    if (game->bounced) {
        entity_release(game, e);
        game->enemies_alive--;
    }
}

/* Bouncer with a fast 4-frame animation, frozen during the wave intro. game86 1000:25f1 */
void think_14(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    e->sprite = (int16_t)(0x95 + ((game->tick_count >> 1) & 3));
    if (entity_check_touch(game, e))
        return;
    entity_move_bounce(game, e);
}

/* Two-frame variant of think_0f. game86 1000:20f7 */
void think_19(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(0xb5 + ((game->tick_count >> 2) & 1));
    think_0f(game, e);
}

/* Four-frame bouncer. game86 1000:2112 */
void think_1a(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(0xb7 + ((game->tick_count >> 2) & 3));
    entity_move_bounce(game, e);
}

/* Deadly bouncer: touching it kills like the deadly pickup (its flash and sound).
 * game86 1000:148d */
void think_1b(GameState *game, Entity *e)
{
    entity_move_bounce(game, e);
    unsigned touched = entity_hits_player(game, e);
    if (touched)
        deadly_touch(game, touched);
}

/* Does nothing (a bare RET). game86 1000:27e6 */
void think_2b(GameState *game, Entity *e)
{
    (void)game;
    (void)e;
}
