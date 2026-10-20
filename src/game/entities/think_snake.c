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

/* Head: snake_head at +08, the steering state is the homing mover's (think_homing.c).
 * game86 1000:36a0 */
void think_1d(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    SnakeHeadMotion *head = &e->snake_head;
    if ((int8_t)--head->spawn.left < 0) {
        head->spawn.left = head->spawn.period;
        entity_spawn_01_at(game, e);
    }

    /* The original indexes the trail with the slot unmasked, so one outside 0..63 would
     * write past DS:4481's 64 entries (below them: the score digits at 4441). Real play
     * can't get there: only enemy_init_10 (2def) makes a head, with slot 0, and nothing but
     * this step ((slot + 1) & 63) writes it afterwards (hit 08/06 at 115d/11b6 and the homing
     * mover 38a2 don't). The port wraps instead (docs/DEVIATIONS.md #21). */
    SnakeTrailPoint *point = &game->snake_trail[(uint16_t)head->next_trail_slot % SNAKE_TRAIL_LENGTH];
    point->x = e->x;
    point->y = e->y;
    point->sprite = e->sprite;
    head->next_trail_slot = (int16_t)((head->next_trail_slot + 1) & (SNAKE_TRAIL_LENGTH - 1));
    game->snake_head = head->next_trail_slot;

    /* The sign of velocity x's fraction word picks the frame. */
    e->sprite = (int16_t)e->homing.velocity_x.frac < 0 ? HEAD_SPRITE_ALT : HEAD_SPRITE;
    entity_homing_move(game, e);
}

/* Segment: sits segment.steps_behind trail steps behind the head. game86 1000:36f6 */
void think_1e(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    const SnakeTrailPoint *point =
        &game->snake_trail[(uint16_t)(game->snake_head - e->segment.steps_behind) & (SNAKE_TRAIL_LENGTH - 1)];
    e->x = point->x;
    e->y = point->y;
    int16_t sprite = (int16_t)(SEGMENT_SPRITE + (point->sprite & 1));
    if (e->segment.flash_frames >= 0) {
        sprite += SEGMENT_FLASH_OFFSET;
        e->segment.flash_frames--;
    }
    e->sprite = sprite;
}
