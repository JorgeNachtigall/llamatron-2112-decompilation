/* init.c - the state game86 starts with (see init.h). */
#include "init.h"

#include "game_data.h"
#include "hud.h"
#include "text.h"

/* Every value below is what game86's data segment holds when the program is loaded. The
 * addresses are the DGROUP offsets; the comment says which code later overwrites the value,
 * where that is known. */
void init_image_values(GameState *game)
{
    Player *p = &game->player;

    p->enemy_speed_x = 10;                  /* 21AC script op 0 at each level start */
    p->enemy_speed_y = 10;                  /* 21AE */
    p->swarm_reaim_period = 15;             /* 21B2 1a28 */
    p->script_param[3] = 1;                 /* 21BC script op 1 */
    p->script_param[4] = 2;                 /* 21BE script op 2 */
    p->powerup = 1;                         /* 21C4 */
    p->bonus_countdown = 2;                 /* 21C8 */

    game->continues = 4;                    /* 061F 4 again at every attract start (010c) */
    game->demo_mode = 1;                    /* 06C4 0 at the attract start (00e1) */
    game->level_flags = -1;                 /* 233E */
    game->speedup_timer = 220;              /* 2336 */
    game->speedup_period = 220;             /* 2338 */
    game->speed_accum = 0x800;              /* 0636 */
    game->spawn_remaining = 12;             /* 08FA */
    game->level_variant = 0x21;             /* 0941 set at every wave start (5350) */
    game->spawn_delay = 0x11;               /* 07FE 7 at every wave start (04c2) */
    game->player_label_offset = 0x0C;       /* 07F5 set at every wave start (0471) */
    game->player2_label_offset = 0x5E;      /* 07F7 never written */
    game->play_flag_0617 = -1;              /* 0617 */

    game->tuning.init01_mode = 1;           /* 0812 script op 5 */
    game->tuning.orbit_phase_step = 0x40;   /* 0820 script op 7 */
    game->tuning.p0826 = 4;                 /* 0826 script op 3 */
    game->tuning.p0830 = 3;                 /* 0830 script op 5 */
    game->tuning.init19_vx = 0x3939;        /* 086E script op 4 */

    game->fire_delay = 8;                   /* 0631 */
    game->droid_aim_timer = 0x14;           /* 0682 */
    game->droid_quarry = -1;                /* 0919 */
    game->demo_wander = (Point16){ 0xA0, 0x64 };            /* 06A8 */
    game->ship_control[0].shot_velocity = (Point16){ -8, 0 }; /* 069C */
    game->ship_control[1].shot_velocity = (Point16){ 3, 3 };  /* 06A0 */
    game->ship_control[0].shot_direction = 0x1D;            /* 06AC */
    game->ship_control[1].shot_direction = 0x20;            /* 06AE */
    game->ship_control[0].facing = 1;                       /* 06C2 */
    game->ship_control[1].facing = 1;                       /* 07F9 */

    /* 0x80 + 0x10 * i from title_draw (3ee0) on */
    for (int i = 0; i < TITLE_LETTERS; i++)
        game->title_objects[i] = -1;        /* 2104 */
    game->title_timer = 0x50;               /* 0904 */
    game->tune.position = TUNE_NO_LIST;     /* 062B null until title_tune_begin */
    game->tune.restart = TUNE_NO_LIST;      /* 062D */
    game->hiscore_mark[0] = -1;             /* 09DE */
    game->hiscore_mark[1] = -1;             /* 09E0 */

    game->spawn = (SpawnCursor){ SPAWN_NONE, 0, 0 };        /* 39EB null */
    game->tick_hook = TICK_HOOK_NONE;
    game->draw_hook = DRAW_HOOK_NONE;       /* 39E0 0 */
}

/* game86 1000:60aa..6100: scores.yak (exactly 0xA0 bytes) replaces the built-in table at
 * DS:40A0. */
void init_load_data(GameState *game, const uint8_t *saved_scores)
{
    text_use_game_assets();                         /* the font and \s sprites */
    hud_use_game_assets();                          /* the HUD digits and labels */
    HighScoreTable saved;
    const HighScoreTable *table = NULL;
    if (saved_scores != NULL && scores_parse_yak(saved_scores, SCORES_YAK_SIZE, &saved))
        table = &saved;
    if (table == NULL || table->count != HISCORE_COUNT)
        table = asset_user_scores();
    if (table == NULL || table->count != HISCORE_COUNT)
        table = asset_default_scores();
    if (table == NULL || table->count != HISCORE_COUNT)
        return;
    for (int i = 0; i < HISCORE_COUNT; i++)
        game->hiscores[i] = table->entries[i];
}
