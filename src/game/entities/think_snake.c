/*
 * think_snake.c - the snake: a head (think_1d) that steers like think_10 and records where it
 * has been in game->snake_trail, and body segments (think_1e) that sit on that trail a fixed
 * number of steps behind it. enemy_init_10 builds the snake and fills the trail with the head.
 */
#include "entities.h"

enum {
    HEAD_SPRITE = 0xcc, HEAD_SPRITE_ALT = 0xca,
    SEGMENT_SPRITE = 0xc6,              /* + trail frame bit, + 2 while flashing after a hit */
    SEGMENT_FLASH_OFFSET = 2,
};

static uint8_t high_byte(int16_t w) { return (uint8_t)((uint16_t)w >> 8); }
static uint8_t low_byte(int16_t w) { return (uint8_t)w; }

/* Head. vx = next trail slot (0..63), vy = spawn period (low byte) / countdown (high byte);
 * the steering state is the homing mover's (think_homing.c). game86 1000:36a0 */
void think_1d(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    uint8_t countdown = (uint8_t)(high_byte(e->vy) - 1);
    bool spawn = (int8_t)countdown < 0;
    if (spawn)
        countdown = low_byte(e->vy);
    e->vy = (int16_t)(uint16_t)((uint16_t)countdown << 8 | low_byte(e->vy));
    if (spawn)
        entity_spawn_01_at(game, e);

    /* The original indexes the trail with vx unmasked, so a vx outside 0..63 would
     * write past DS:4481's 64 entries (below them: the score digits at 4441). Real play
     * can't get there: only enemy_init_10 (2def) makes a head, with vx = 0, and nothing but
     * this step ((vx + 1) & 63) writes it afterwards (hit 08/06 at 115d/11b6 and the homing
     * mover 38a2 don't). The port wraps instead (docs/DEVIATIONS.md #21). */
    SnakeTrailPoint *point = &game->snake_trail[(uint16_t)e->vx % SNAKE_TRAIL_LENGTH];
    point->x = e->x;
    point->y = e->y;
    point->sprite = e->sprite;
    e->vx = (int16_t)((e->vx + 1) & (SNAKE_TRAIL_LENGTH - 1));
    game->snake_head = e->vx;

    /* The sign of velocity x's fraction word picks the frame. */
    e->sprite = e->timer < 0 ? HEAD_SPRITE_ALT : HEAD_SPRITE;
    entity_homing_move(game, e);
}

/* Segment. timer = how many trail steps behind the head it is; hp = frames left to flash.
 * game86 1000:36f6 */
void think_1e(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    const SnakeTrailPoint *point =
        &game->snake_trail[(uint16_t)(game->snake_head - e->timer) & (SNAKE_TRAIL_LENGTH - 1)];
    e->x = point->x;
    e->y = point->y;
    int16_t sprite = (int16_t)(SEGMENT_SPRITE + (point->sprite & 1));
    if (e->hp >= 0) {
        sprite += SEGMENT_FLASH_OFFSET;
        e->hp--;
    }
    e->sprite = sprite;
}
