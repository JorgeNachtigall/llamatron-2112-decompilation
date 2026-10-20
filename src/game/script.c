/* script.c - level scripts (see script.h). */
#include "script.h"

#include "sound.h"

enum {
    OP_ENEMY_SPEED,     /* byte 0xFF, game86 1000:0421 */
    OP_LEVEL_PARAMS,    /* 0xFE, 1000:0438 */
    OP_SOUND_PARAMS,    /* 0xFD, 1000:03f4 */
    OP_TUNING_3,        /* 0xFC, 1000:03e1 */
    OP_TUNING_4,        /* 0xFB, 1000:03cf */
    OP_TUNING_5,        /* 0xFA, 1000:03bc */
    OP_TUNING_6,        /* 0xF9, 1000:039c */
    OP_ORBIT_STEP,      /* 0xF8, 1000:0392 */
};

enum { ROSTER_END = 0xFF };     /* count byte that ends a spawn list (1000:3d10) */

/* A byte copied into both halves of a word (MOV AH,AL). */
static int16_t doubled(uint8_t b)
{
    return (int16_t)(b * 0x101);
}

/* 8086 SHL AX,CL: no masking of the count, so 16 or more clears the word. */
static int16_t shift_left(int8_t value, int count)
{
    if (count < 0 || count >= 16)
        return 0;
    return (int16_t)(uint16_t)((uint16_t)(int16_t)value << count);
}

void script_run_op(GameState *game, const LevelOp *op)
{
    const uint8_t *b = op->operands;
    Player *player = &game->player;
    LevelTuning *tuning = &game->tuning;

    switch (op->handler) {
    case OP_ENEMY_SPEED:    /* 1000:0421 */
        player->enemy_speed_x = shift_left((int8_t)b[0], game->loop_count);
        player->enemy_speed_y = shift_left((int8_t)b[1], game->loop_count);
        break;
    case OP_LEVEL_PARAMS:   /* 1000:0438: 21B6..21BC, sign-extended */
        for (int i = 0; i < 4; i++)
            player->script_param[i] = (int8_t)b[i];
        break;
    case OP_SOUND_PARAMS:   /* 1000:03f4: 21C0, 21BE, 21C2; the 4th byte is skipped */
        player->script_param[5] = doubled(b[0]);
        player->script_param[4] = b[1] & 0x0F;
        player->script_param[6] = doubled(b[2]);
        game_sound_play(SAMPLE_ID_LEVEL_SOUND_OP, 0, 0x1E);
        break;
    case OP_TUNING_3:       /* 1000:03e1 */
        tuning->growth_step = b[0];
        tuning->segment_family = b[1];
        break;
    case OP_TUNING_4:       /* 1000:03cf */
        tuning->init0d_hp = b[0];
        tuning->init19_vx = doubled(b[1]);
        break;
    case OP_TUNING_5:       /* 1000:03bc */
        tuning->dropper_creep_mask = b[0];
        tuning->init01_mode = b[1];
        break;
    case OP_TUNING_6:       /* 1000:039c */
        tuning->accum_start = (uint32_t)b[0] << 8;
        tuning->runner_fire = doubled(b[1]);
        tuning->runner_hits = b[2];
        break;
    case OP_ORBIT_STEP:     /* 1000:0392 */
        tuning->orbit_phase_step = b[0];
        break;
    default:
        break;
    }
}

/* game86 1000:020b..0243, 0455..045d */
void script_start_level(GameState *game, const LevelSet *levels)
{
    /* A 0 entry after the last script sends the game back to level 0, one loop further.
     * [H] levels past the table's end never happen (warp is +5 within it). */
    while (game->player.level < 0 || game->player.level >= levels->level_count) {
        game->player.level = 0;
        game->loop_count++;
        game->player.level_loops++;
    }

    const LevelScript *script = &levels->levels[game->player.level];
    for (unsigned i = 0; i < script->op_count; i++)
        script_run_op(game, &script->ops[i]);

    game->player.wave_type = script->wave_type;
    game->spawn = (SpawnCursor){ SPAWN_FROM_LEVEL, game->player.level, 0 };
}

/* game86 1000:10ea -> 123c..1241 (see script.h) */
void script_clobber_spawns(GameState *game)
{
    game->spawn_pairs_clobbered = true;
}

static bool spawn_is_clobbered(const GameState *game, const LevelSet *levels, int level,
                               unsigned index)
{
    const LevelSpawnClobber *clobber = &levels->clobber_10b0;
    return game->spawn_pairs_clobbered && level == clobber->level &&
           index >= clobber->first_spawn && index - clobber->first_spawn < clobber->spawn_count;
}

/* game86 1000:3d09 */
bool script_next_spawn_pair(GameState *game, const LevelSet *levels)
{
    SpawnCursor *cursor = &game->spawn;
    int type = -1;
    int count = ROSTER_END;

    if (cursor->source == SPAWN_FROM_ROSTER) {
        if (cursor->index < ROSTER_SIZE) {
            const RosterEntry *entry = &game->player.roster[cursor->index];
            type = (uint8_t)entry->kind;
            count = entry->count;
        }
    } else if (cursor->level >= 0 && cursor->level < levels->level_count) {
        const LevelScript *script = &levels->levels[cursor->level];
        if (cursor->index < script->spawn_count) {
            type = script->spawns[cursor->index].enemy_type;
            count = script->spawns[cursor->index].count;
            if (spawn_is_clobbered(game, levels, cursor->level, cursor->index))
                type = count = 0;
        }
    }

    if (count == ROSTER_END) {
        game->spawning_done = true;
        game->unhatched_left = game->unhatched;
        return false;
    }
    game->spawn_remaining = (int16_t)count;
    game->spawn_type = (int16_t)type;
    cursor->index++;
    return true;
}

/* game86 1000:5350 */
void level_select_variant(GameState *game, const LevelSet *levels)
{
    game->level_variant = (int16_t)levels->warp_sprites[(uint16_t)game->player.level & 3];
}
