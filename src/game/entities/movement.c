/*
 * movement.c - movement helpers shared by the think handlers (see entities.h).
 *
 * Positions and velocities are 16-bit like the original's words; sums are wrapped to int16_t
 * before they are compared.
 */
#include "entities.h"

static int16_t wrap16(int value)
{
    return (int16_t)(uint16_t)value;
}

/* |v| as the original computes it with NEG: -32768 stays -32768. */
static int16_t magnitude16(int16_t v)
{
    return v < 0 ? wrap16(-v) : v;
}

/* game86 1000:4e7f */
void entity_move_bounce(GameState *game, Entity *e)
{
    game->bounced = 0;

    /* A velocity too large for the playfield in both directions never settles; the original
     * loops forever there too (no caller produces one). */
    int16_t x = wrap16(e->x + e->vx);
    while (x < BOUNCE_X_MIN || x > BOUNCE_X_MAX) {
        e->vx = wrap16(-e->vx);
        game->bounced = 1;
        x = wrap16(e->x + e->vx);
    }
    e->x = x;

    /* Off the top it retries with the reversed velocity; off the bottom it pins y to just
     * inside the edge (1000:4ebc..4ec1). */
    int16_t y = wrap16(e->y + e->vy);
    while (y < BOUNCE_Y_MIN || y > BOUNCE_Y_MAX) {
        e->vy = wrap16(-e->vy);
        game->bounced = 1;
        if (y >= 100) {
            y = BOUNCE_Y_MAX - 1;
            break;
        }
        y = wrap16(e->y + e->vy);
    }
    e->y = y;
}

/* game86 1000:4b1a..4b3a, 1000:50b8..50db */
void vector_shrink_to(int16_t *dx, int16_t *dy, int16_t max_x, int16_t max_y)
{
    int16_t size_x = magnitude16(*dx);
    int16_t size_y = magnitude16(*dy);
    while (size_x > max_x || size_y > max_y) {
        *dx = (int16_t)(*dx >> 1);
        *dy = (int16_t)(*dy >> 1);
        size_x = (int16_t)(size_x >> 1);
        size_y = (int16_t)(size_y >> 1);
    }
}

/* game86 1000:509b */
Point16 entity_aim_at_target(const GameState *game, const Entity *e, int16_t max_x, int16_t max_y)
{
    Point16 v = { wrap16(game->target.x - e->x), wrap16(game->target.y - e->y) };
    vector_shrink_to(&v.x, &v.y, max_x, max_y);
    return v;
}

/* game86 1000:32fe. Bresenham along the major axis; all arithmetic in 16 bits. */
Point16 line_walk(Point16 from, Point16 to, int16_t steps)
{
    bool steep = false;
    for (;;) {
        int16_t step = wrap16(to.x - from.x) < 0 ? -1 : 1;
        int16_t first = step < 0 ? to.x : from.x;
        int16_t last = step < 0 ? from.x : to.x;
        int16_t run = wrap16(last - first);
        int16_t rise = wrap16(to.y - from.y);
        int16_t rise_step = 1;
        if (rise < 0) {
            rise = wrap16(from.y - to.y);
            rise_step = -1;
        }
        if (wrap16(rise - run) > 0) {
            /* Steeper than 45 degrees: walk along y instead (once). */
            from = (Point16){ from.y, from.x };
            to = (Point16){ to.y, to.x };
            steep = true;
            continue;
        }

        int16_t error = wrap16(2 * rise - run);
        int16_t error_straight = wrap16(2 * rise);
        int16_t error_diagonal = wrap16(2 * (rise - run));
        Point16 at = from;
        for (int16_t i = first;; i = wrap16(i + 1)) {
            if (wrap16(i - last) >= 0)
                return (Point16){ 1, 1 };   /* ran out of line (not transposed back) */
            at.x = wrap16(at.x + step);
            if (error < 0) {
                error = wrap16(error + error_straight);
            } else {
                at.y = wrap16(at.y + rise_step);
                error = wrap16(error + error_diagonal);
            }
            steps = wrap16(steps - 1);
            if (steps < 0)
                break;
        }
        return steep ? (Point16){ at.y, at.x } : at;
    }
}
