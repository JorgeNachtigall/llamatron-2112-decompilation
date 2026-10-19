/* tick.c - the play tick (see tick.h). */
#include "tick.h"

#include <stddef.h>

#include "bullets.h"
#include "entities/entities.h"
#include "entity_dispatch.h"
#include "game_data.h"
#include "input.h"
#include "player.h"
#include "script.h"
#include "sound.h"

enum {
    SHIELD_WARNING_TICKS = 0x32,    /* the shield's last second: warning sample */
    RESPAWN_TIMER_STEP = 2,
    RESPAWN_GAME_OVER = -200,       /* a respawn timer this low ends the wave as a death */
    WAVES_PER_CYCLE = 0x1B,         /* Player.wave wraps here */
    SPEEDUP_STEP = 0x200,
    SPEEDUP_HALVE_ABOVE = 0x10,
    MIN_SWARM_REAIM = 3,

    /* the spawn effect entity (1d51..1d83) */
    WARP_IN_ANIM = 0x0C,
    WARP_IN_HATCH = 0x01,
    WARP_IN_SCALE = 0x32,
    WARP_IN_DEATH_SAMPLE = 0x0D,
};

/* game86 1000:1562..16c0: timers, the pause and abort keys, respawns and the wave-cleared
 * check. Returns false when the rest of the tick is skipped. */
static bool play_tick_timers(GameState *game)
{
    if (!game->frame_drawn)                         /* 1562 */
        return false;
    if (game->wave_start_delay != 0)
        game->wave_start_delay--;
    if (game->shield_timer != 0 && --game->shield_timer == SHIELD_WARNING_TICKS)
        game_sound_play(0x1A, 1, 0x0A);             /* 1587 */

    InputState *input = input_state();
    if (input->last_make_code == PC_KEY_P) {        /* 1595 */
        game->tick_hook = TICK_HOOK_PAUSE;
        game->pause_message = (int16_t)((rng_next_byte(&game->rng) & 3) + 1);
        return false;
    }
    if (input->last_make_code == PC_KEY_X) {        /* 15b3: abort to the title */
        game->wave_result = WAVE_ABORTED;
        return false;
    }

    if (game->wave_intro_timer == 0) {              /* 15c1 */
        game->player.powerup--;
        if (--game->speedup_timer < 0) {
            int16_t period = game->speedup_period;
            if (period > SPEEDUP_HALVE_ABOVE)
                period >>= 1;
            game->speedup_timer = period;
            game->speed_accum += SPEEDUP_STEP;
        }
    }
    for (int p = 0; p < PLAYER_COUNT; p++)          /* 15ea */
        if (game->rescue_marker_y[p] >= 0)
            game->rescue_marker_y[p]--;
    if (game->death_sound_timer != 0 && --game->death_sound_timer == 0 && !game->rude_name)
        game_sound_play(0x11, 3, 0x1E);             /* 1614 */

    for (int p = 0; p < PLAYER_COUNT; p++)          /* 1620 */
        if (game->respawn_timer[p] != 0)
            game->respawn_timer[p] -= RESPAWN_TIMER_STEP;
    if (game->respawn_timer[0] == RESPAWN_GAME_OVER || game->respawn_timer[1] == RESPAWN_GAME_OVER) {
        game->tick_hook = TICK_HOOK_NONE;           /* 1640 */
        game->wave_result = WAVE_PLAYER_DEAD;
        return false;
    }
    if (game->respawn_timer[0] < 0 || game->respawn_timer[1] < 0)
        return false;

    if (game->spawning_done) {                      /* 1665 */
        if (game->wave_intro_timer != 0)
            game->wave_intro_timer--;
        bool cleared = game->enemies_alive == 0 &&
                       (game->player.wave_type == 0 || game->unhatched == 0);
        if (cleared && game->wave_result != WAVE_CLEARED) {
            game->wave_result = WAVE_CLEARED;       /* 1693 */
            game->tick_hook = TICK_HOOK_NONE;
            game->player.level++;
            if (++game->player.wave == WAVES_PER_CYCLE)
                game->player.wave = 0;
            game_sound_play(0x14, 0, 0x1E);
            return false;
        }
    }
    return true;
}

/* game86 1000:1562 */
void play_tick_hook(GameState *game)
{
    if (!play_tick_timers(game))
        return;

    players_tick(game);                             /* 16c1..1a00 */
    player_fire(game);                              /* 1a01 */
    if (game->level_flags >= 0 && game->powerup_0652 == 0)
        game->level_flags--;                        /* 1a04 */
    game->play_flag_0617 = 1;
    shots_update(game);                             /* 1a1c */
    game->swarm_count = 0;                         /* 1a1f */
    entities_think_all(game);
    int16_t reaim = (int16_t)(game->swarm_count >> 1);
    game->player.swarm_reaim_period = reaim < MIN_SWARM_REAIM ? MIN_SWARM_REAIM : reaim;
    wave_spawn_tick(game);                          /* 1a38 */
}

/* entity_alloc, or with a full table the record past it (see GameState.entity_overflow). */
static Entity *spawn_slot(GameState *game)
{
    Entity *e = entity_alloc(game);
    return e != NULL ? e : &game->entity_overflow;
}

/* game86 1000:1d40..1dcb */
void wave_spawn_tick(GameState *game)
{
    if (game->spawn_delay >= 0) {                   /* 1d40: a warp-in effect */
        game->spawn_delay--;
        Entity *e = spawn_slot(game);
        enemy_random_spawn_pos(game, e);
        e->anim = WARP_IN_ANIM;
        e->vx = 0;
        e->vy = 0;
        e->sprite = game->level_variant;
        e->think = 0;
        e->hatch_into = WARP_IN_HATCH;
        e->scale = WARP_IN_SCALE;
        e->solid = 0;
        e->death_sample = WARP_IN_DEATH_SAMPLE;
        e->death_rate = 0;
        e->hit_handler = 0;
        e->overlay.x = 0;
        e->kind = -1;
        game->free_slots--;
        return;
    }

    const LevelSet *levels = asset_levels();
    if (game->spawn_remaining < 0) {                /* 1d8d */
        script_next_spawn_pair(game, levels);
        return;
    }

    /* 1d97: one enemy of the current pair */
    unsigned pick = (uint16_t)game->player.level;
    if (game->player.wave_type != 0)
        pick = rng_next_byte(&game->rng);
    game->spawn_sprite = levels->spawn_sprites[pick & 3];
    game->spawn_remaining--;
    Entity *e = spawn_slot(game);
    e->overlay.x = 0;
    e->kind = game->spawn_type;
    game->free_slots--;
    unsigned type = (uint16_t)game->spawn_type;
    EnemyInitFn init = type < ENEMY_INIT_COUNT ? enemy_init_handlers[type] : NULL;
    if (init != NULL)
        init(game, e);
}

/* game86 1000:1ced: rebuild the current player's roster from the live entities, for the
 * next try at the same wave (1000:07b1). */
void wave_spawn_enemies(GameState *game)
{
    RosterEntry *roster = game->player.roster;
    for (int i = 0; i < ROSTER_SIZE; i++)
        roster[i] = (RosterEntry){ -1, 0xFF };
    game->spawn_delay = -1;                         /* 1cfe */
    for (int i = 0; i < ENTITY_COUNT; i++) {
        const Entity *e = &game->entities[i];
        if (e->think < 0 || e->kind < 0)            /* 1d0a: the low byte, see Entity */
            continue;
        int8_t kind = (int8_t)e->kind;
        for (int r = 0; r < 31; r++) {              /* BP = 0x1e: 31 entries */
            if (roster[r].kind == kind) {
                roster[r].count++;
                break;
            }
            if (roster[r].kind < 0) {               /* 1d38 */
                roster[r] = (RosterEntry){ kind, 0 };
                break;
            }
        }
    }
}
