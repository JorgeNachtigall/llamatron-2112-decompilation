/*
 * spawn.c - placing new enemies and the small records they leave behind: spawn positions,
 * enemy projectiles, plain children and fragments (see entities.h).
 *
 * Coordinates are 16-bit words like the original's; differences are wrapped to int16_t
 * before they are compared.
 */
#include "entities.h"

#include <stddef.h>

#include "rng.h"
#include "script.h"

/* Sprites: the dropped bomb; entity_burst_four's children, left to right. */
enum {
    SPRITE_BOMB = 0x91,
    SPRITE_BURST_FAR_LEFT = 0xeb,
    SPRITE_BURST_LEFT = 0xec,
    SPRITE_BURST_RIGHT = 0xed,
    SPRITE_BURST_FAR_RIGHT = 0xee,
};

static int16_t word(int value)
{
    return (int16_t)(uint16_t)value;
}

/* |a - b| as the original computes it with SUB / NEG: -32768 stays negative. */
static int16_t distance16(int16_t a, int16_t b)
{
    int16_t d = word(a - b);
    return d < 0 ? word(-d) : d;
}

static Entity *first_free_slot(GameState *game)
{
    for (int i = 0; i < ENTITY_COUNT; i++)
        if (entity_is_free(&game->entities[i]))
            return &game->entities[i];
    return NULL;
}

/* game86 1000:1c67 */
void enemy_random_spawn_pos(GameState *game, Entity *e)
{
    bool two_players = game->options & OPT_TWO_PLAYERS;
    int16_t x;
    do {
        x = word((rng_next_byte(&game->rng) & 0xff) + (rng_next_byte(&game->rng) & 0x1f) + 0x10);
    } while (distance16(game->ship[0].x, x) < 0x20 ||
             (two_players && distance16(game->ship[1].x, x) < 0x20));
    e->x = x;

    /* The distance to P1 is only checked when the spawn lies below P1 (the original skips the
     * compare when ship y - spawn y is not negative, 1000:1cbc); P2 is checked both ways. */
    int16_t y;
    for (;;) {
        y = word((rng_next_byte(&game->rng) & 0x7f) + (rng_next_byte(&game->rng) & 0x3f) + 0x20);
        int16_t below_p1 = word(game->ship[0].y - y);
        if (below_p1 < 0 && word(-below_p1) < 0x20)
            continue;
        if (two_players && distance16(game->ship[1].y, y) < 0x20)
            continue;
        break;
    }
    if (y < 0)
        y = 0;
    if (y > 0xb2)
        y = 0xb2;
    e->y = y;
}

/* game86 1000:1232 */
void enemy_shot_init(GameState *game, Entity *shot, const Entity *from, const EnemyShot *tmpl)
{
    shot->death_sample = SAMPLE_ID_SHOT_DEATH;
    shot->death_rate = 0;
    shot->anim = 0x0d;
    shot->scale = 2;
    shot->solid = 1;
    shot->sprite = tmpl->sprite;
    shot->aux16 = tmpl->aim_y;
    shot->timer = 0;
    shot->aux1a = tmpl->aim_x;
    shot->hp = 0;
    shot->x = from->x;
    shot->y = from->y;
    shot->aux1c = tmpl->speed;
    shot->kill_action = 0;
    shot->sub_mode = tmpl->speed;
    shot->reload = 0;
    shot->countdown = 0;
    shot->aux24 = shot->sprite;
    shot->anim_toggle = shot->x;
    shot->aux26 = 0;
    shot->aux30 = (int16_t)((rng_next_byte(&game->rng) & 7) + 3);
    shot->sprite_ref = shot->y;
    shot->aux2a = 0;
    shot->hit_handler = tmpl->hit_handler;
    entity_set_type(shot, 0x10, 0x10);
    game->enemies_alive++;
}

/* game86 1000:1221 */
Entity *enemy_shot_alloc(GameState *game, const Entity *from, const EnemyShot *tmpl)
{
    /* With no free slot the original returns the address after the table and its callers
     * write the kind word there; that byte is outside every record and is dropped here. */
    Entity *shot = first_free_slot(game);
    if (shot)
        enemy_shot_init(game, shot, from, tmpl);
    return shot;
}

/* game86 1000:10b0. Its enemy_shot_init call (1000:10ea) runs with think_13's code offset
 * in SI, so the "parent" words it clears are two spawn pairs of a level script; the script
 * side reproduces that (script_clobber_spawns). */
Entity *enemy_shot_drop(GameState *game, const Entity *from)
{
    Entity *shot = first_free_slot(game);
    if (!shot)
        return NULL;
    shot->anim = ANIM_IDLE;
    shot->scale = 0;
    const EnemyShot bomb = { .aim_y = 0, .aim_x = 0, .sprite = SPRITE_BOMB, .speed = 0xa00,
                             .hit_handler = 0 };
    enemy_shot_init(game, shot, from, &bomb);
    script_clobber_spawns(game);
    shot->kind = -1;
    shot->death_sample = SAMPLE_ID_RELEASE;
    shot->death_rate = 0;
    return shot;
}

/* game86 1000:3ca3 */
void entity_init_child(GameState *game, Entity *child, const Entity *parent, int16_t sprite,
                       int16_t vx, int16_t vy)
{
    child->x = parent->x;
    child->y = parent->y;
    game->enemies_alive++;
    game->free_slots--;
    child->anim = 0x0d;
    child->sprite = sprite;
    child->vx = vx;
    child->vy = vy;
    child->scale = 0;
    child->solid = 1;
    child->death_sample = SAMPLE_ID_POP;
    child->death_rate = 3;
    child->hit_handler = 0;
    child->overlay.x = 0;
    child->kind = -1;
    entity_set_type(child, 0x0f, 0);
}

/* game86 1000:3c91 */
Entity *entity_spawn_child(GameState *game, const Entity *parent, int16_t sprite, int16_t vx,
                           int16_t vy)
{
    Entity *child = first_free_slot(game);
    if (child)
        entity_init_child(game, child, parent, sprite, vx, vy);
    return child;
}

/* game86 1000:3c84 */
Entity *entity_spawn_child_if(GameState *game, const Entity *parent, int16_t sprite,
                              int16_t vx, int16_t vy)
{
    if (game->wave_intro_timer != 0 || sprite == 0)
        return NULL;
    return entity_spawn_child(game, parent, sprite, vx, vy);
}

/* game86 1000:3b26 */
void entity_burst_four(GameState *game, const Entity *parent)
{
    /* The two middle children fly up or down towards P1 (not the current target). */
    int16_t towards_p1 = word(game->ship[0].y - parent->y) < 0 ? -1 : 1;
    entity_spawn_child_if(game, parent, SPRITE_BURST_FAR_LEFT, -3, 0);
    entity_spawn_child(game, parent, SPRITE_BURST_LEFT, -2, towards_p1);
    entity_spawn_child(game, parent, SPRITE_BURST_RIGHT, 2, towards_p1);
    entity_spawn_child(game, parent, SPRITE_BURST_FAR_RIGHT, 3, 0);
}

/* game86 1000:4f93 */
Entity *entity_spawn_debris(GameState *game, const Entity *at)
{
    Entity *d = first_free_slot(game);
    if (!d)
        return NULL;
    game->free_slots--;
    d->x = at->x;
    d->y = at->y;
    d->vx = 0;
    d->vy = 0;
    d->scale = 0;
    d->solid = 1;
    d->death_sample = SAMPLE_ID_ENEMY_DEATH;
    d->death_rate = 0;
    d->hit_handler = 0;
    d->anim = 0x0d;
    d->overlay.x = 0;
    d->kind = -1;
    return d;
}
