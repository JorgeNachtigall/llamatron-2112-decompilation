/*
 * player.c - the players' ships.
 *
 * Each ship has a ShipControl: the velocity it moves with this tick, the velocity/direction of
 * its next shot and the controller direction ("aim") that shot came from. The controllers
 * give a 4-bit direction nibble (INPUT_UP/DOWN/LEFT/RIGHT); ControlTables turns a nibble into
 * shot and movement vectors, ship sprite and trail flag. Holding fire keeps the aim.
 *
 * Control modes (GameOptions):
 *   one player              P1 from controller 1
 *   two players, two sticks P2 from controller 2 as well
 *   two players, one stick  P2 is the droid: companion_update flies it to the llamas and
 *                           enemy_aim_update points its guns at the most crowded quadrant
 *   demo                    the droid flies P1 (and the fire button ends the demo)
 *
 * Every ship that moved appends its position to its trail (game->trail), which rescued llamas
 * (think_06) walk along.
 */
#include "player.h"

#include "entities/entities.h"
#include "game_data.h"
#include "input.h"
#include "sound.h"

enum {
    FIRE_PERIOD = 8,                    /* a volley every 9th tick */
    SHOT_LIFE = 0x3c,
    DROID_AIM_PERIOD = 0x14,
    DROID_SEARCH_SLOTS = 0x45,          /* companion_update looks at the first 69 slots */
    DROID_STEP_X = 2, DROID_STEP_Y = 1,
    DROID_FACING_LEFT = 5, DROID_FACING_RIGHT = 1,
    DROID_TRAIL_LEFT = 0, DROID_TRAIL_RIGHT = 6,
    DROID_SEARCH = -1, DROID_TO_COMPANION = -2,
    DEMO_WANDER_PERIOD = 0x3f,          /* tick mask */
    THINK_LLAMA = 0x05,
};

static const SoundRequest SOUND_FIRE = { 0x0e, 0, 1 };     /* 1000:0ad1, 0bf7 */

static int16_t wrap16(int v) { return (int16_t)(uint16_t)v; }

static unsigned nibble(uint8_t control)
{
    return control & INPUT_DIRECTIONS;
}

/* Bit 0: `from` is left of (x, y), bit 1: above it (16-bit compares, as the original). */
static unsigned quadrant(Point16 from, int16_t x, int16_t y)
{
    return (wrap16(from.x - x) < 0 ? 1u : 0u) | (wrap16(from.y - y) < 0 ? 2u : 0u);
}

/* Moves a ship by `move`, each axis only if it stays inside the playfield. */
static void ship_step(Point16 *ship, Point16 move)
{
    int16_t x = wrap16(ship->x + move.x);
    if (x >= SHIP_X_MIN && x < SHIP_X_END)
        ship->x = x;
    int16_t y = wrap16(ship->y + move.y);
    if (y >= SHIP_Y_MIN && y < SHIP_Y_END)
        ship->y = y;
}

/* Aims at controller direction `n`; a direction without a shot keeps the old one.
 * game86 1000:18d0..190c */
static void aim_at(ShipControl *c, const ControlTables *t, unsigned n)
{
    c->aim = (int16_t)n;
    if (t->shot_direction[n] == 0)
        return;
    c->shot_velocity = (Point16){ t->shot_vx[n], t->shot_vy[n] };
    c->shot_direction = (int8_t)t->shot_direction[n];
}

/* ---- the droid --------------------------------------------------------------------------- */

/* First llama among the first 69 slots (claimed or not). game86 1000:4890..48bc */
static bool droid_find_llama(GameState *game)
{
    for (int slot = 0; slot < DROID_SEARCH_SLOTS; slot++) {
        if (game->entities[slot].think == THINK_LLAMA) {
            game->droid_quarry = (int16_t)slot;
            return true;
        }
    }
    game->droid_quarry = DROID_TO_COMPANION;
    return false;
}

void companion_update(GameState *game)
{
    if (game->wave_intro_timer != 0) {
        game->droid_quarry = DROID_SEARCH;
        return;
    }
    if (game->tick_count & 1)
        return;

    if (game->droid_quarry < 0) {
        game->companion.x = game->chain_tail.x;
        game->companion.y = game->chain_tail.y;
        if (game->demo_mode) {
            if ((game->tick_count & DEMO_WANDER_PERIOD) == 0) {
                game->demo_wander.x = (int16_t)((int8_t)rng_next_byte(&game->rng) + 0xa0);
                game->demo_wander.y = (int16_t)(((int8_t)rng_next_byte(&game->rng) >> 1) + 0x64);
            }
            game->companion.x = game->demo_wander.x;
            game->companion.y = game->demo_wander.y;
        }
        if (droid_find_llama(game))
            return;     /* sets off next time */
    }

    const Entity *goal = &game->companion;
    if (game->droid_quarry != DROID_TO_COMPANION) {
        const Entity *llama = game->droid_quarry < ENTITY_COUNT
                                  ? &game->entities[game->droid_quarry] : NULL;
        if (llama && llama->think == THINK_LLAMA) {
            goal = llama;
        } else {
            /* rescued or caught meanwhile: search again (without moving the companion) */
            game->droid_quarry = DROID_SEARCH;
            if (droid_find_llama(game))
                return;
        }
    }

    Point16 *ship = &game->ship[1];
    ShipControl *c = &game->ship_control[1];
    int16_t dx = wrap16(goal->x - ship->x);
    int16_t dy = wrap16(goal->y - ship->y);
    vector_shrink_to(&dx, &dy, DROID_STEP_X, DROID_STEP_Y);
    bool right = dx >= 0;
    c->facing = right ? DROID_FACING_RIGHT : DROID_FACING_LEFT;
    c->trail_flag = right ? DROID_TRAIL_RIGHT : DROID_TRAIL_LEFT;
    c->move = (Point16){ dx, dy };
    ship->x = wrap16(ship->x + dx);
    ship->y = wrap16(ship->y + dy);
}

/* Index of the largest vote, the first on a tie; -1 when all are 0. */
static int most_votes(const uint8_t votes[4])
{
    int best = -1;
    uint8_t most = 0;
    for (int q = 0; q < 4; q++) {
        if (votes[q] > most) {
            most = votes[q];
            best = q;
        }
    }
    return best;
}

/* Moves the droid, and every 21st tick points its guns at the quadrant (around P2) holding
 * the most solid enemies, or with none at P1's side. game86 1000:176c */
static void enemy_aim_update(GameState *game)
{
    companion_update(game);
    if (--game->droid_aim_timer >= 0)
        return;
    game->droid_aim_timer = DROID_AIM_PERIOD;

    Point16 droid = game->ship[1];
    uint8_t *votes = game->droid_aim_votes;
    for (int q = 0; q < 4; q++)
        votes[q] = 0;
    for (const Entity *e = game->entities; e < game->entities + ENTITY_COUNT; e++) {
        if (!entity_is_free(e) && e->solid != 0)
            votes[quadrant(droid, e->x, e->y)]++;
    }
    int best = most_votes(votes);
    if (best < 0) {
        /* No enemy: aim at P1's side. When P1 is not below the droid the original counts the
         * vote and re-runs the count over the 4 records after the table (slots 70..73, which
         * entities_clear leaves free), so that side wins. game86 1000:17d6..17f3 */
        best = (int)quadrant(droid, game->ship[0].x, game->ship[0].y);
        if (!(best & 2))
            votes[best]++;
    }

    const ControlTables *t = asset_control_tables();
    ShipControl *c = &game->ship_control[1];
    c->shot_velocity = (Point16){ t->droid_shot_vx[best], t->droid_shot_vy[best] };
    c->shot_direction = t->droid_shot_direction[best];
    c->aim = t->droid_aim[best];
}

/* ---- players_tick ----------------------------------------------------------------------- */

/* P2 on the second stick. game86 1000:16d5..1763 */
static void p2_from_stick(GameState *game, const ControlTables *t)
{
    uint8_t control = input_state()->controls[1];
    unsigned n = nibble(control);
    ShipControl *c = &game->ship_control[1];
    if (!(control & INPUT_FIRE) && t->shot_direction[n] != 0)
        aim_at(c, t, n);
    /* (1000:170f doubles the move without a second stick; this code only runs with one) */
    c->move = (Point16){ t->move_vx[n], t->move_vy[n] };
    /* 1000:1727 tests the facing byte together with the sign-extended move_vy above it, so
     * an upward move without a facing sets facing 0. */
    if (t->ship_facing[n] != 0 || t->move_vy[n] < 0)
        c->facing = (int8_t)t->ship_facing[n];
    c->trail_flag = (int8_t)t->trail_flag[n];
    ship_step(&game->ship[1], c->move);
}

/* Entry `index` counted from the start of P1's trail: the original adds follower_lag to the
 * head without masking, running on into P2's trail. Past P2's (head + lag >= 256, i.e. more
 * than ~25 llamas rescued with the head near the end) it reads whatever follows; the port
 * wraps to P1's start instead. game86 1000:1947..195e */
static const TrailPoint *trail_entry(const GameState *game, unsigned index)
{
    index %= PLAYER_COUNT * TRAIL_LENGTH;
    return &game->trail[index / TRAIL_LENGTH][index % TRAIL_LENGTH];
}

/* Appends the ship's position to its trail unless it has not moved. game86 1000:1961..198e
 * (P1), 1999..19ce (P2: see DEVIATIONS #10 for the original's first compare after load) */
static void trail_record(GameState *game, int player, int16_t flag)
{
    const Point16 *ship = &game->ship[player];
    TrailPoint *trail = game->trail[player];
    const TrailPoint *newest = &trail[game->trail_head[player] & (TRAIL_LENGTH - 1)];
    if (newest->x == ship->x && newest->y == ship->y)
        return;
    int16_t head = (int16_t)((game->trail_head[player] + 1) & (TRAIL_LENGTH - 1));
    trail[head].x = ship->x;
    trail[head].y = ship->y;
    trail[head].flag = flag;
    game->trail_head[player] = head;
}

void players_tick(GameState *game)
{
    const ControlTables *t = asset_control_tables();
    if (game->options & OPT_TWO_PLAYERS) {
        if (game->options & OPT_DOUBLE_CONTROLLER)
            p2_from_stick(game, t);
        else
            enemy_aim_update(game);
    }

    ShipControl *p1 = &game->ship_control[0];
    const ShipControl *p2 = &game->ship_control[1];
    uint8_t control = input_state()->controls[0];
    unsigned n;                         /* the nibble P1 moves (and leaves its trail flag) by */
    if (game->demo_mode) {              /* game86 1000:181b */
        if (control & INPUT_FIRE) {
            game->demo_mode = 1;
            game->wave_result = WAVE_CLEARED;
        }
        enemy_aim_update(game);
        p1->shot_velocity = p2->shot_velocity;
        p1->shot_direction = p2->shot_direction;
        p1->aim = p2->aim;
        p1->facing = p2->facing;
        p1->move = p2->move;
        game->ship[0] = game->ship[1];
        n = (unsigned)p2->aim & (INPUT_NIBBLES - 1);    /* always a nibble */
        aim_at(p1, t, n);
    } else {                            /* game86 1000:1886 */
        n = nibble(control);
        if (game->started_with_stick_b)
            aim_at(p1, t, nibble(input_state()->controls[1]));
        else if (!(control & INPUT_FIRE))
            aim_at(p1, t, n);
        p1->move = (Point16){ t->move_vx[n], t->move_vy[n] };
        if (t->ship_facing[n] != 0)
            p1->facing = (int8_t)t->ship_facing[n];
    }
    p1->trail_flag = (int8_t)t->trail_flag[n];

    const TrailPoint *tail = trail_entry(game, (unsigned)(uint16_t)(game->trail_head[0]
                                                                    + game->follower_lag[0]));
    game->chain_tail.x = tail->x;
    game->chain_tail.y = tail->y;
    trail_record(game, 0, p1->trail_flag);
    if (game->options & OPT_TWO_PLAYERS)
        trail_record(game, 1, p2->trail_flag);

    if (!game->demo_mode)
        ship_step(&game->ship[0], p1->move);
}

/* ---- player_fire ------------------------------------------------------------------------ */

static Shot *next_free_shot(GameState *game, Shot *from)
{
    for (Shot *s = from; s < game->shots + SHOT_COUNT; s++) {
        if (s->x < 0)
            return s;
    }
    return NULL;
}

static void shot_launch(Shot *s, Point16 at, Point16 velocity, int16_t direction)
{
    s->bounce_life = SHOT_LIFE;
    s->direction = direction;
    s->vx = velocity.x;
    s->vy = velocity.y;
    s->y = at.y;
    s->x = at.x;
}

/* One ship's volley: a shot along its aim, and with `spread` and a non-zero multi_shot up
 * to two more in the directions the spread tables give for that aim. False when no shot slot
 * was free at all. game86 1000:09ab..0ad1 (P2), 0add..0bf7 (P1) */
static bool fire_volley(GameState *game, int player, bool spread)
{
    const ShipControl *c = &game->ship_control[player];
    Point16 at = game->ship[player];
    Shot *s = next_free_shot(game, game->shots);
    if (!s)
        return false;
    shot_launch(s, at, c->shot_velocity, c->shot_direction);
    if (!spread || game->player.multi_shot == 0)
        return true;

    const ControlTables *t = asset_control_tables();
    unsigned aim = (unsigned)c->aim & (INPUT_NIBBLES - 1);     /* always a nibble */
    const uint8_t extra[2] = { t->spread_a[aim], t->spread_b[aim] };
    for (int i = 0; i < 2 && extra[i] != 0; i++) {
        s = next_free_shot(game, s);
        if (!s)
            break;
        unsigned d = extra[i];
        shot_launch(s, at, (Point16){ t->shot_vx[d], t->shot_vy[d] }, t->shot_direction[d]);
    }
    return true;
}

void player_fire(GameState *game)
{
    if (game->respawn_timer[0] != 0 || game->respawn_timer[1] != 0)
        return;
    game->fire_delay--;
    if ((int8_t)game->fire_delay >= 0)
        return;
    game->fire_delay = FIRE_PERIOD;
    if (!game->spawning_done)
        return;

    /* With two players the ships take turns firing the spread; a volley that finds no free
     * shot slot ends the whole call. */
    if (game->options & OPT_TWO_PLAYERS) {
        game->fire_turn ^= 1;
        if (!fire_volley(game, 1, game->fire_turn != 0))
            return;
        game_sound_play(SOUND_FIRE.sample, SOUND_FIRE.rate_index, SOUND_FIRE.priority);
    }
    if (!fire_volley(game, 0, game->fire_turn == 0))
        return;
    game_sound_play(SOUND_FIRE.sample, SOUND_FIRE.rate_index, SOUND_FIRE.priority);
}

/* ---- turns ------------------------------------------------------------------------------ */

void player_swap_state(GameState *game)
{
    Player current = game->player;
    game->player = game->player_other;
    game->player_other = current;
    game->options ^= OPT_CURRENT_IS_P2;
}

void player_copy_state(GameState *game)
{
    if (game->options & OPT_SHARED_START)
        game->player_other = game->player;
}
