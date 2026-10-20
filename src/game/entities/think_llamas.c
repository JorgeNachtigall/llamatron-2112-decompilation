/*
 * think_llamas.c - the llamas to rescue (think_05), rescued llamas trotting along their
 * ship's trail (think_06) and the hunter that goes after the llamas (think_08).
 *
 * A llama is rescued by flying over it: it scores, may arm a pickup and becomes a follower
 * placed follower_lag entries further back on the rescuing ship's trail (game->trail).
 * Herders (think_0e) and hunters claim a llama (llama.claimed) so that no other goes for it.
 */
#include "entities.h"
#include "game_data.h"
#include "score.h"
#include "sound.h"

enum {
    THINK_LLAMA = 0x05,
    THINK_FOLLOWER = 0x06,
    THINK_TIMER = 0x09,
    RESCUE_RANGE_X = 0x10, RESCUE_RANGE_Y = 0x0a,
    RESCUE_PICKUP_CHANCE = 0x28,        /* out of 256 */
    FOLLOWER_SPACING = 5,               /* trail entries per rescued llama */
    RESCUE_MARKER_ABOVE = 10,
    RESCUE_OVERLAY_SPRITE = 0x45, RESCUE_OVERLAY_FRAMES = 0x14,
    WANDER_MIN_TICKS = 0x0a,
    HUNTER_SPRITE = 0x2b, HUNTER_FLASH_OFFSET = 9,
    HUNTER_REACH = 8,
    HUNTER_STEP = 3,
    CAUGHT_SPRITE = 0x33, CAUGHT_TICKS = 0x14,
    QUARRY_NONE = -2,                   /* herder_claim_llama found none: go to the companion */
    RESCUE_BONUS_RATE = 0x50,           /* level_flags after a rescue, before the halvings */
    TRAIL_POINT_BYTES = (int)sizeof(TrailPoint),    /* llama.trail_lag is in trail bytes */
};

/* Sprite offsets of the four walking directions in a walker's sprite set. */
enum { FACE_LEFT = 0, FACE_UP = 2, FACE_DOWN = 4, FACE_RIGHT = 6 };

/* score_add digit places (0 = ten millions .. 7 = units). */
enum { PLACE_THOUSANDS = 4, PLACE_HUNDREDS = 5, PLACE_TENS = 6 };

/* Pickup kinds a rescue arms (Player.pickup_pending = kind + 1). */
enum {
    PENDING_RANDOM_RESCUE = 6,
    PENDING_LAST_RESCUE = 5,
    PENDING_LAST_RESCUE_2P = 7,
    PENDING_LAST_RESCUE_2P_ALT = 8,
};

/* Which ship's trail a follower walks (llama.ship). The original stores the trail's address
 * there; followers are never solid, so the field's other use (the kill action) never runs. */
enum { FOLLOW_P1 = 0, FOLLOW_P2 = 1 };

static const SoundRequest SOUND_LAST_RESCUE = { SAMPLE_ID_ALERT, 1, 6 };   /* 1000:4ce3 */
static const SoundRequest SOUND_RESCUE = { SAMPLE_ID_RESCUE, 1, 3 };        /* 1000:4de6 */
static const SoundRequest SOUND_CAUGHT = { SAMPLE_ID_CAUGHT, 2, 6 };        /* 1000:4a8d */

static void play(SoundRequest s)
{
    game_sound_play(s.sample, s.rate_index, s.priority);
}

static int16_t magnitude16(int16_t v)
{
    return v < 0 ? (int16_t)(uint16_t)-v : v;
}

static int16_t wrap16(int v) { return (int16_t)(uint16_t)v; }

/* Walk-cycle frame: alternates every 4 pixels along the axis it moves on. */
static int16_t step_frame(int16_t coordinate)
{
    return (int16_t)((coordinate >> 2) & 1);
}

static bool ship_over(const Point16 *ship, const Entity *e)
{
    return magnitude16(wrap16(e->x - ship->x)) < RESCUE_RANGE_X
        && magnitude16(wrap16(e->y - ship->y)) < RESCUE_RANGE_Y;
}

/* The wave's bonus rate for rescues: halved in double-controller mode, and once more per loop
 * through the levels (DEVIATIONS #13: the halvings are counted mod 32). */
static int16_t rescue_level_flags(const GameState *game)
{
    int16_t base = (game->options & OPT_DOUBLE_CONTROLLER) ? RESCUE_BONUS_RATE / 2
                                                           : RESCUE_BONUS_RATE;
    return (int16_t)(base >> (game->loop_count & 0x1f));
}

/* ---- think_05 ------------------------------------------------------------------------- */

/* Rescued by `player`: the llama falls in behind that ship. game86 1000:4d04..4d97 */
static void llama_follow(GameState *game, Entity *e, int player)
{
    if (game->rescue_marker_pending)
        game->rescue_marker_y[player] = wrap16(game->ship[player].y - RESCUE_MARKER_ABOVE);
    game->follower_lag[player] = wrap16(game->follower_lag[player] + FOLLOWER_SPACING);
    e->llama.trail_lag = wrap16(game->follower_lag[player] * TRAIL_POINT_BYTES);
    e->llama.ship = player == 1 ? FOLLOW_P2 : FOLLOW_P1;
}

/* A ship flew over the llama: it may arm a pickup (always for the wave's last llama, which
 * also scores 1,000), scores 500 (250 for P2 with two controllers) and becomes a follower of
 * the rescuing ship with a "rescued" sprite over it for a while. game86 1000:4ca3..4df2 */
static void llama_rescue(GameState *game, Entity *e, unsigned touched)
{
    bool two_players = game->options & OPT_TWO_PLAYERS;
    if (rng_next_byte(&game->rng) < RESCUE_PICKUP_CHANCE)
        game->player.pickup_pending = PENDING_RANDOM_RESCUE;
    if (--game->unhatched_left == 0) {
        game->player.pickup_pending = PENDING_LAST_RESCUE;
        if (two_players) {
            game->player.pickup_pending = PENDING_LAST_RESCUE_2P;
            if (rng_next_byte(&game->rng) > RESCUE_PICKUP_CHANCE)
                game->player.pickup_pending = PENDING_LAST_RESCUE_2P_ALT;
        }
        play(SOUND_LAST_RESCUE);
        score_add(game, PLACE_THOUSANDS, 1);
        game->rescue_marker_pending = true;
    }

    if (two_players && (touched & TOUCH_P2)) {
        llama_follow(game, e, 1);
        if (game->options & OPT_DOUBLE_CONTROLLER)
            score_add(game, PLACE_TENS, 25);
        else
            score_add(game, PLACE_HUNDREDS, 5);
    } else {
        score_add(game, PLACE_HUNDREDS, 5);
        game->level_flags = rescue_level_flags(game);
        llama_follow(game, e, 0);
    }

    e->kind = -1;
    game->rescue_marker_pending = false;
    e->overlay.sprite = RESCUE_OVERLAY_SPRITE;
    e->overlay.frames = RESCUE_OVERLAY_FRAMES;
    e->overlay.x = e->x;
    e->overlay.y = e->y;
    game->unhatched--;
    game->level_flags = rescue_level_flags(game);
    entity_set_type(e, THINK_FOLLOWER, 0);
    play(SOUND_RESCUE);
}

/* Llama. Wanders at random while the power-up state is negative, otherwise walks towards its
 * target ship; moves every other tick. game86 1000:4c46 */
void think_05(GameState *game, Entity *e)
{
    e->solid = 0;
    unsigned touched = ship_over(&game->ship[0], e) ? TOUCH_P1 : 0;
    if ((game->options & OPT_TWO_PLAYERS) && ship_over(&game->ship[1], e))
        touched |= TOUCH_P2;
    if (touched) {
        llama_rescue(game, e, touched);
        return;
    }

    if (game->player.powerup < 0) {
        if (--e->llama.turn_timer < 0) {
            const EnemyTables *tables = asset_enemy_tables();
            e->llama.turn_timer =
                (int16_t)((rng_next_byte(&game->rng) & 0x0f) + WANDER_MIN_TICKS);
            unsigned dir = rng_next_byte(&game->rng) & 7;
            e->vx = tables->wander_vx[dir];
            e->vy = tables->wander_vy[dir];
            e->llama.facing = tables->wander_sprite[dir];
        }
    } else {
        bool right = wrap16(e->x - game->target.x) < 0;
        e->vx = right ? 1 : -1;
        e->llama.facing = right ? FACE_RIGHT : FACE_LEFT;
        e->vy = wrap16(e->y - game->target.y) < 0 ? 1 : -1;
    }
    if ((game->tick_count & 1) == 0)
        entity_move_bounce(game, e);
    int16_t frame = step_frame(e->vx == 0 ? e->y : e->x);
    e->sprite = wrap16(frame + e->llama.facing + e->llama.sprite_base);
}

/* ---- think_06 ------------------------------------------------------------------------- */

/* Rescued llama: sits llama.trail_lag (in trail bytes) behind the newest point of its
 * ship's trail and faces the way the ship went there. game86 1000:4bfb */
void think_06(GameState *game, Entity *e)
{
    int player = e->llama.ship == FOLLOW_P2 ? 1 : 0;
    uint16_t offset = (uint16_t)(game->trail_head[player] * TRAIL_POINT_BYTES
                                 - (uint16_t)e->llama.trail_lag);
    const TrailPoint *point = &game->trail[player][offset / TRAIL_POINT_BYTES % TRAIL_LENGTH];
    e->x = point->x;
    e->y = point->y;
    int16_t direction = point->flag;
    bool horizontal = direction == FACE_LEFT || direction == FACE_RIGHT;
    e->sprite = wrap16(direction + step_frame(horizontal ? e->x : e->y) + e->llama.sprite_base);
}

/* ---- think_08 ------------------------------------------------------------------------- */

/* The llama the hunter goes for, NULL if it is gone. The original adds the slot number, not
 * the slot's offset, to the table address, so only a llama in slot 0 is ever found; for any
 * other slot it reads a stray byte, takes the slot as lost and searches again. (When that
 * stray byte happens to be 5 the original instead homes in on the misaligned record; the
 * port does not reproduce that.) game86 1000:4a22..4a36 */
static Entity *hunter_quarry(GameState *game, const Entity *e)
{
    if (e->hunter.quarry == QUARRY_NONE)
        return &game->companion;
    if (e->hunter.quarry == 0 && game->entities[0].think == THINK_LLAMA)
        return &game->entities[0];
    return NULL;
}

/* Hunter. Every 8th tick it claims a llama (quarry = its slot) and walks to it at up to 3
 * pixels per axis; when it gets there the llama is turned into a timer. With no llama left
 * (quarry = -2) it walks to the companion, which follows its target ship. game86 1000:495f */
void think_08(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0 || (game->tick_count & 7) != 0)
        return;
    if (e->hunter.quarry < 0) {
        if (e->hunter.quarry != QUARRY_NONE) {
            herder_claim_llama(game, e);
            return;
        }
        game->companion.x = game->target.x;
        game->companion.y = game->target.y;
    }

    int16_t frame;
    if (magnitude16(e->vy) > magnitude16(e->vx))
        frame = (int16_t)((e->vy >= 0 ? FACE_DOWN : FACE_UP) + step_frame(e->y));
    else
        frame = (int16_t)((e->vx >= 0 ? FACE_RIGHT : FACE_LEFT) + step_frame(e->x));
    if (e->hunter.hit_frames != 0) {
        e->hunter.hit_frames--;
        frame += HUNTER_FLASH_OFFSET;
    }
    e->sprite = (int16_t)(frame + HUNTER_SPRITE);

    Entity *quarry = hunter_quarry(game, e);
    if (!quarry) {
        herder_claim_llama(game, e);
        return;
    }
    if (entity_check_touch(game, e))
        return;

    int16_t dx = wrap16(quarry->x - e->x);
    int16_t dy = wrap16(quarry->y - e->y);
    if (magnitude16(dx) < HUNTER_REACH && magnitude16(dy) < HUNTER_REACH) {
        if (e->hunter.quarry != QUARRY_NONE) {
            entity_set_type(quarry, THINK_TIMER, 0);
            game->unhatched--;
            quarry->kind = -1;
        }
        e->hunter.quarry = -1;
        quarry->sprite = CAUGHT_SPRITE;
        quarry->timer = CAUGHT_TICKS;
        play(SOUND_CAUGHT);
        return;
    }
    vector_shrink_to(&dx, &dy, HUNTER_STEP, HUNTER_STEP);
    e->vx = dx;
    e->vy = dy;
    e->x = wrap16(e->x + dx);
    e->y = wrap16(e->y + dy);
}
