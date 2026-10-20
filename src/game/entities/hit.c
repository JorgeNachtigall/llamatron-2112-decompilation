/*
 * hit.c - what happens when a player shot (or the smart bomb) hits an entity: the 18 hit
 * handlers (tbl_hit_handlers, CS:0C04), the common kill and the pickup drop.
 *
 * `hit->shot` is the shot that hit; with the smart bomb it is the shot whose move triggered
 * the sweep (W4's shots_update), and handlers read it the same way.
 */
#include "entities.h"

#include "game_data.h"
#include "rng.h"
#include "score.h"
#include "sound.h"

/* Kills that drop a bonus pickup (DS:063A counts up to it). */
enum { KILLS_PER_BONUS = 200 };
/* Below this level the bonus pickup can be the warp (1000:0e7f). */
enum { LAST_WARP_LEVEL = 0x5a };
/* Sprites: the pickup-chance overlay; the fire-back shots (as enemy types 06 and 07). */
enum { SPRITE_PICKUP_CHANCE = 0x46, SPRITE_SHOT_MEDIUM = 0x7f, SPRITE_SHOT_FAST = 0x83 };

static int16_t word(int value)
{
    return (int16_t)(uint16_t)value;
}

void entity_start_flash(GameState *game, FlashListId id)
{
    game->flash_list = asset_flash_list(id);
    game->flash_pos = 0;
}

/* game86 1000:0eb8: the dying entity becomes pickup kind pickup_pending - 1. */
static void become_pickup(GameState *game, Entity *e)
{
    if (game->wave_result == WAVE_CLEARED)
        return;
    int16_t kind = word(game->player.pickup_pending - 1);
    game->player.pickup_pending = 0;
    e->timer = kind;
    e->hp = 200;                            /* ticks before it disappears */
    const EnemyTables *tables = asset_enemy_tables();
    if (tables && kind >= 0 && kind < PICKUP_KINDS)
        e->sprite = (int16_t)tables->pickup_sprites[kind];
    e->anim = 0x0d;
    e->kind = -1;
    e->vx = 0;
    e->vy = 0;
    entity_set_type(e, 0x1c, 0x1c);
    e->aux16 = e->sprite;
    e->solid = 0;
    game->enemies_alive++;
    game_sound_play(SAMPLE_ID_ALERT, 3, 4);
}

/* ent_hit_00, game86 1000:0dfe */
void entity_kill(GameState *game, Entity *e, const EntityHit *hit)
{
    const EnemyTables *tables = asset_enemy_tables();
    if (tables)
        e->timer = tables->drop_by_direction[(hit->shot->direction - 0x1d) & 3];
    e->kind = -1;
    if (e->think != 0x01)
        game->enemies_alive--;

    /* Score by think index. The table's last record (FFFF FFFF, think 0x2c) is never used:
     * that type's hit handler (ent_hit_11) does not kill. */
    uint8_t think = (uint8_t)e->think;
    if (tables && think + 1u < tables->score_award_count) {
        ScoreAward award = tables->score_awards[think];
        if (award.digit != 0)
            score_add(game, award.digit, award.count_minus_1 + 1);
    }

    entity_set_type(e, 0x02, 0);            /* think_02: the explosion fades */
    e->anim = 0x05;
    e->solid = 0;
    entity_start_flash(game, FLASH_LIST_KILL);

    if (++game->kills_toward_bonus >= KILLS_PER_BONUS) {
        int16_t queued = game->player.pickup_pending;
        game->kills_toward_bonus = 0;
        game->player.pickup_pending = 1;
        if (--game->player.bonus_countdown == 0) {
            if (game->player.level < LAST_WARP_LEVEL)
                game->player.pickup_pending = 2;
            game->player.bonus_countdown = 2;
        }
        become_pickup(game, e);
        game->player.pickup_pending = queued;
    } else if (game->player.pickup_pending != 0) {
        become_pickup(game, e);
    } else {
        game_sound_play(e->death_sample, e->death_rate, 2);
    }
}

/* game86 1000:0dbf: a chance of queueing a pickup before the kill. */
static void ent_hit_02(GameState *game, Entity *e, const EntityHit *hit)
{
    e->overlay.sprite = SPRITE_PICKUP_CHANCE;
    e->overlay.frames = 0x14;
    e->overlay.x = e->x;
    e->overlay.y = e->y;
    int limit = game->wave_intro_timer != 0 ? 0xa0 : 0x3c;
    int roll = rng_next_byte(&game->rng);
    if (roll < limit)
        game->player.pickup_pending = game->level_flags < 0 ? 4 : 9;
    else if (roll < 0x8c)
        game->player.pickup_pending = game->shots_bounce ? 9 : 8;
    entity_kill(game, e, hit);
}

/* game86 1000:1074: pushed back by half the shot's velocity, inside the field. */
static void ent_hit_01(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)game;
    int16_t x = word((hit->shot->vx >> 1) + e->x);
    if (x >= 8 && x < 0x138)
        e->x = x;
    int16_t y = word((hit->shot->vy >> 1) + e->y);
    if (y >= 0x18 && y < 0xb8)
        e->y = y;
    e->aux16 = 5;
    game_sound_play(SAMPLE_ID_KNOCK, 1, 3);
}

/* game86 1000:105f: hit points; at < 0 the record's kill action runs. */
static void ent_hit_03(GameState *game, Entity *e, const EntityHit *hit)
{
    entity_start_flash(game, FLASH_LIST_HIT);
    if (--e->hp >= 0)
        ent_hit_01(game, e, hit);
    else
        hit_kill_action(game, e, hit);
}

/* Projectiles fanned out along the shot's velocity, each marked with roster kind `kind`.
 * The original fills the template transposed (the shot's vx * 2 into 041B, which the shot
 * flies as velocity_y, and vy * 2 into 0419, velocity_x), so they fly mirrored across the
 * diagonal. enemy_shot_init also zeroes the shot's direction and +0a word (its SI is the shot here),
 * so the kill that follows reads direction 0. game86 1000:1108 / 11e2 */
static void fire_back(GameState *game, Entity *e, const EntityHit *hit, EnemyShot *tmpl,
                      int16_t kind, const bool *negate_vx, const bool *negate_vy, int count)
{
    tmpl->velocity_y = word(hit->shot->vx * 2);
    tmpl->velocity_x = word(hit->shot->vy * 2);
    for (int i = 0; i < count; i++) {
        if (negate_vx[i])
            tmpl->velocity_x = word(-tmpl->velocity_x);
        if (negate_vy[i])
            tmpl->velocity_y = word(-tmpl->velocity_y);
        Entity *shot = enemy_shot_alloc(game, e, tmpl);
        if (shot) {
            hit->shot->direction = 0;
            hit->shot->unused_0a = 0;
            shot->kind = kind;
        }
    }
}

/* game86 1000:11d2: with level_flags < 0, two projectiles back along the shot. */
static void ent_hit_04(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->level_flags < 0) {
        EnemyShot tmpl = { .sprite = SPRITE_SHOT_MEDIUM, .speed = 0x400, .hit_handler = 5 };
        static const bool flip_vx[] = { false, true }, flip_vy[] = { false, true };
        fire_back(game, e, hit, &tmpl, 6, flip_vx, flip_vy, 2);
    }
    entity_kill(game, e, hit);
}

/* game86 1000:10f8: with level_flags < 0, four projectiles in an X. */
static void ent_hit_05(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->level_flags < 0) {
        EnemyShot tmpl = { .sprite = SPRITE_SHOT_FAST, .speed = 0x600, .hit_handler = 0 };
        static const bool flip_vx[] = { false, true, false, true };
        static const bool flip_vy[] = { false, false, true, false };
        fire_back(game, e, hit, &tmpl, 7, flip_vx, flip_vy, 4);
    }
    entity_kill(game, e, hit);
}

/* game86 1000:11b6: knocked away at four times the shot's velocity. */
static void ent_hit_06(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)game;
    e->homing.velocity_x.whole = word(hit->shot->vx * 4);
    e->homing.velocity_y.whole = word(hit->shot->vy * 4);
    e->homing.start_delay = 5;
    game_sound_play(SAMPLE_ID_KNOCK, 1, 3);
}

/* game86 1000:1022 */
static void ent_hit_07(GameState *game, Entity *e, const EntityHit *hit)
{
    game_sound_play(SAMPLE_ID_KNOCK, 0, 3);
    if (--e->hp < 0) {
        entity_kill(game, e, hit);
        return;
    }
    e->vx = hit->shot->vx >> 1;
    e->vy = hit->shot->vy >> 1;
}

/* game86 1000:115d: the snake head (think 1d). Every egg_hits period of hits the first
 * segment left becomes an egg; with none left the head
 * goes, without a kill. */
static void ent_hit_08(GameState *game, Entity *e, const EntityHit *hit)
{
    entity_start_flash(game, FLASH_LIST_HIT);
    game_sound_play(SAMPLE_ID_RESCUE, 0, 4);
    ByteCounter *egg_hits = &e->homing.egg_hits;
    egg_hits->left--;
    if ((int8_t)egg_hits->left >= 0) {
        ent_hit_06(game, e, hit);
        return;
    }
    egg_hits->left = egg_hits->period;

    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *s = &game->entities[i];
        if (s->think == 0x1e && s->hatch_into == 0x1e) {
            enemy_setup_00(game, s, true);
            return;
        }
        if (s->think == 0x1d && s->hatch_into == 0x1d)
            break;
    }
    entity_vanish(e);
    game->enemies_alive--;
}

/* game86 1000:11b0 */
static void ent_hit_09(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)game;
    (void)hit;
    e->hp = 0x28;
}

/* game86 1000:2da7 */
static void ent_hit_0a(GameState *game, Entity *e, const EntityHit *hit)
{
    HomingView *h = &e->homing;
    if (--h->hits_left < 0) {
        entity_kill(game, e, hit);
        return;
    }
    entity_start_flash(game, FLASH_LIST_HIT);
    /* The fractions only, from the shot's y and vx: the original's own choice of words. */
    h->velocity_x.frac = (uint16_t)(hit->shot->y * 4);
    h->velocity_y.frac = (uint16_t)(hit->shot->vx * 4);
    h->accel_x = (Fixed16){ .frac = 0x200, .whole = 0 };
    h->accel_y = (Fixed16){ .frac = 0x200, .whole = 0 };
    h->flash_frames = 0x14;
    game_sound_play(SAMPLE_ID_POP, 1, 3);
}

/* game86 1000:2ae1 */
static void ent_hit_0b(GameState *game, Entity *e, const EntityHit *hit)
{
    e->aux1a = 0x10;
    entity_start_flash(game, FLASH_LIST_HIT);
    if (--e->aux16 < 0) {
        entity_kill(game, e, hit);
        return;
    }
    game_sound_play(SAMPLE_ID_HIT_0B, 1, 3);
}

/* game86 1000:363c: the walker (think 23) counts its hits left; no kill, no score. */
static void ent_hit_0c(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)hit;
    if (--e->homing.hits.left != 0) {
        entity_start_flash(game, FLASH_LIST_HIT_0C);
        game_sound_play(SAMPLE_ID_GAME_OVER, 3, 4);
        return;
    }
    entity_vanish(e);
    game->enemies_alive--;
    entity_start_flash(game, FLASH_LIST_KILL_0C);
    game_sound_play(SAMPLE_ID_GAME_OVER, 1, 6);
}

/* game86 1000:31d8: only killed once the intro is over and its timer is running. */
static void ent_hit_0d(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->wave_intro_timer == 0 && e->timer != 0) {
        entity_kill(game, e, hit);
        return;
    }
    e->aux24 = 0x0a;
    e->vx = hit->shot->vx >> 1;
    e->vy = hit->shot->vy >> 1;
    game_sound_play(SAMPLE_ID_POP, 3, 4);
}

/* game86 1000:12ed: leaves a segment of the current segment family behind (type 0x28). */
static void ent_hit_0e(GameState *game, Entity *e, const EntityHit *hit)
{
    Entity *s = NULL;
    for (int i = 0; i < ENTITY_COUNT && !s; i++)
        if (entity_is_free(&game->entities[i]))
            s = &game->entities[i];
    if (s) {
        s->x = e->x;
        s->y = e->y;
        s->death_sample = SAMPLE_ID_POP;
        s->death_rate = 0;
        s->sprite = game->segment_sprite;
        s->scale = 0;
        s->solid = 1;
        HomingView *h = &s->homing;
        fixed16_set(&h->velocity_x, 0);
        fixed16_set(&h->velocity_y, 0);
        fixed16_set(&h->accel_x, 0);
        fixed16_set(&h->accel_y, 0);
        h->flash_frames = -1;
        h->position_x.whole = 0;            /* the fractions keep what the slot held */
        h->position_y.whole = 0;
        h->start_delay = 0;
        h->segment_family = game->tuning.segment_family;
        s->hit_handler = 0x0a;
        s->anim = 0x0d;
        s->growth.unused_0a = 0;
        s->growth.sprite_base = s->sprite;
        entity_set_type(s, 0x28, 0x28);
        game->enemies_alive++;
        s->kind = -1;
    }
    entity_kill(game, e, hit);
}

/* game86 1000:12d2 */
static void ent_hit_0f(GameState *game, Entity *e, const EntityHit *hit)
{
    entity_start_flash(game, FLASH_LIST_HIT_ALT);
    if (--e->homing.gun_hits_left == 0) {
        entity_kill(game, e, hit);
        return;
    }
    game_sound_play(SAMPLE_ID_HIT_0F, 3, 4);
}

/* game86 1000:270a: after enough hits it turns into think 2c (and starts over). */
static void ent_hit_11(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)hit;
    entity_start_flash(game, FLASH_LIST_HIT_ALT);
    game_sound_play(SAMPLE_ID_GAME_OVER, 4, 4);
    if (--e->aux16 >= 0)
        return;
    e->anim = ANIM_IDLE;
    e->timer = 0x96;
    e->aux16 = 0x404;
    entity_set_type(e, 0x2c, 0x2c);
}

/* CS:0C04. Index 00 and 10 are the plain kill. */
const HitFn entity_hit_handlers[HIT_HANDLER_COUNT] = {
    entity_kill, ent_hit_01, ent_hit_02, ent_hit_03, ent_hit_04, ent_hit_05,
    ent_hit_06, ent_hit_07, ent_hit_08, ent_hit_09, ent_hit_0a, ent_hit_0b,
    ent_hit_0c, ent_hit_0d, ent_hit_0e, ent_hit_0f, entity_kill, ent_hit_11,
};
