/*
 * tick.c - the play tick (see tick.h).
 *
 * While a wave is played, the timer interrupt runs play_tick_hook 50 times a second. In order:
 * the clocks of the wave (start delay, shield, speed-up, the death sound), the pause and abort
 * keys, the players' respawns, the wave-cleared check; then the ships move and fire, shots
 * fly and hit, every entity thinks, and the wave's next enemy may warp in. Any of the first
 * steps can end the tick early. Original addresses are game86 1000:xxxx.
 */
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
    ROSTER_SCAN = 31,               /* 1d1c: BP = 0x1e, the roster entries searched */

    /* the spawn effect entity (1d51..1d83) */
    WARP_IN_ANIM = 0x0C,
    WARP_IN_HATCH = 0x01,
    WARP_IN_SCALE = 0x32,
    WARP_IN_DEATH_SAMPLE = SAMPLE_ID_KNOCK,
};

/* 1567..1587 */
static void wave_clocks(GameState *game)
{
    if (game->wave_start_delay != 0)
        game->wave_start_delay--;
    if (game->shield_timer != 0 && --game->shield_timer == SHIELD_WARNING_TICKS)
        game_sound_play(SAMPLE_ID_ALERT, 1, 0x0A);             /* 1587 */
}

/* 1595..15c0: P pauses (with one of four messages), X gives up the wave. True when the
 * tick ends here. */
static bool pause_or_abort_key(GameState *game)
{
    InputState *input = input_state();
    if (input->last_make_code == PC_KEY_P) {        /* 1595 */
        game->tick_hook = TICK_HOOK_PAUSE;
        game->pause_message = (int16_t)((rng_next_byte(&game->rng) & 3) + 1);
        return true;
    }
    if (input->last_make_code == PC_KEY_X) {        /* 15b3: abort to the title */
        game->wave_result = WAVE_ABORTED;
        return true;
    }
    return false;
}

/* 15c1..161f: once the intro is over the enemies speed up now and then, ever more often;
 * the rescue markers rise; the death sound of the last enemies plays when its timer ends. */
static void level_clocks(GameState *game)
{
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
        game_sound_play(SAMPLE_ID_EXPLOSION, 3, 0x1E);             /* 1614 */
}

/* 1620..1664: the respawn timers count down by 2 (0x64 at the wave start; a ship that is
 * touched starts dying at -2, PLAYER_DYING). At RESPAWN_GAME_OVER the wave ends as a death;
 * while a ship is dying the rest of the tick waits. True when the tick ends here. */
static bool respawn_pending(GameState *game)
{
    for (int p = 0; p < PLAYER_COUNT; p++)          /* 1620 */
        if (game->respawn_timer[p] != 0)
            game->respawn_timer[p] -= RESPAWN_TIMER_STEP;
    if (game->respawn_timer[0] == RESPAWN_GAME_OVER || game->respawn_timer[1] == RESPAWN_GAME_OVER) {
        game->tick_hook = TICK_HOOK_NONE;           /* 1640 */
        game->wave_result = WAVE_PLAYER_DEAD;
        return true;
    }
    return game->respawn_timer[0] < 0 || game->respawn_timer[1] < 0;
}

/* 1665..16c0: after the last spawn the intro counts down, and the wave is cleared when no
 * enemy is left (and, when the wave record's type is set, nothing is left unhatched): next
 * level, next wave of the cycle. True when the tick ends here. */
static bool wave_cleared_check(GameState *game)
{
    if (!game->spawning_done)                       /* 1665 */
        return false;
    if (game->wave_intro_timer != 0)
        game->wave_intro_timer--;
    bool cleared = game->enemies_alive == 0 &&
                   (game->player.wave_type == 0 || game->unhatched == 0);
    if (!cleared || game->wave_result == WAVE_CLEARED)
        return false;
    game->wave_result = WAVE_CLEARED;               /* 1693 */
    game->tick_hook = TICK_HOOK_NONE;
    game->player.level++;
    if (++game->player.wave == WAVES_PER_CYCLE)
        game->player.wave = 0;
    game_sound_play(SAMPLE_ID_WAVE_CLEARED, 0, 0x1E);
    return true;
}

/* game86 1000:1562 */
void play_tick_hook(GameState *game)
{
    if (!game->frame_drawn)                         /* 1562 */
        return;
    wave_clocks(game);
    if (pause_or_abort_key(game))
        return;
    level_clocks(game);
    if (respawn_pending(game) || wave_cleared_check(game))
        return;

    players_tick(game);                             /* 16c1..1a00 */
    player_fire(game);                              /* 1a01 */
    /* The shot power-up (level_flags >= 0) runs out, unless pickup 03 keeps it. */
    if (game->level_flags >= 0 && game->shot_power_kept == 0)
        game->level_flags--;                        /* 1a04 */
    game->play_ticked = 1;
    shots_update(game);                             /* 1a1c */
    game->swarm_count = 0;                          /* 1a1f */
    entities_think_all(game);
    /* The swarm re-aims more often the fewer swarmers are left. */
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

/* game86 1000:1d40..1dcb: the wave's enemies arrive one per tick. First spawn_delay warp-in
 * effects, then the level script's pairs of (type, count), one enemy at a time. */
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
    if (game->spawn_remaining < 0) {                /* 1d8d: the next pair */
        script_next_spawn_pair(game, levels);
        return;
    }

    /* 1d97: one enemy of the current pair, its sprite by level (at random when the wave
     * record's type is set) */
    unsigned pick = (uint16_t)game->player.level;
    if (game->player.wave_type != 0)
        pick = rng_next_byte(&game->rng);
    game->spawn_sprite = (int16_t)levels->spawn_sprites[pick & 3];
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
 * next try at the same wave (1000:07b1): one entry per kind, with how many are alive. */
void wave_spawn_enemies(GameState *game)
{
    RosterEntry *roster = game->player.roster;
    for (int i = 0; i < ROSTER_SIZE; i++)
        roster[i] = (RosterEntry){ -1, 0xFF };
    game->spawn_delay = -1;                         /* 1cfe: no warp-in effects */
    for (int i = 0; i < ENTITY_COUNT; i++) {
        const Entity *e = &game->entities[i];
        if (e->think < 0 || e->kind < 0)            /* 1d0a: the low byte, see Entity */
            continue;
        int8_t kind = (int8_t)e->kind;
        for (int r = 0; r < ROSTER_SCAN; r++) {
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
