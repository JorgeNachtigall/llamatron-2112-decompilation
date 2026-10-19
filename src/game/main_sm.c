/* main_sm.c - the top-level flow as a per-frame state machine (see main_sm.h). */
#include "main_sm.h"

#include "attract.h"
#include "bullets.h"
#include "continue.h"
#include "draw.h"
#include "entity_dispatch.h"
#include "frame.h"
#include "gfx.h"
#include "game_data.h"
#include "hud.h"
#include "input.h"
#include "player.h"
#include "rng.h"
#include "score.h"
#include "script.h"
#include "scroller.h"
#include "sound.h"
#include "tick.h"

/* Where the flow is. Each state's code runs until it either moves on (and the next state
 * runs in the same frame) or waits for the vertical retrace (the frame ends). */
typedef enum MainState {
    MS_BOOT,                /* video_init 1000:5960 */
    MS_BOOT_REVEAL,         /* 5966 */
    MS_BOOT_FIRE_RELEASE,   /* 596c */
    MS_BOOT_SCROLL_START,   /* 5973 */
    MS_BOOT_SCROLL,         /* text_scroller 5844 */
    MS_ATTRACT,             /* 00db */
    MS_ATTRACT_TITLE,       /* 0106 */
    MS_ATTRACT_LLAMAS,      /* 0121 */
    MS_TITLE,               /* 0136..0168 */
    MS_ATTRACT_SCROLL,      /* 02d4 / 02f6 */
    MS_NEW_GAME,            /* 018f */
    MS_NEXT_LEVEL,          /* 01e5 */
    MS_WAVE_START,          /* 045d */
    MS_WAVE_SHOW,           /* 05d5 */
    MS_WAVE,                /* 0616 */
    MS_WAVE_OVER,           /* 0633 */
    MS_WARP_SOUND,          /* 065d */
    MS_WAVE_CLEARED,        /* 0699 */
    MS_WAVE_NEXT,           /* 06b6 */
    MS_WARP_BOX_START,      /* 06d4 */
    MS_WARP_BOX,            /* 06e8 */
    MS_WARP_BOX_CLEAR,      /* 072d */
    MS_PLAYER_DIED,         /* 075c */
    MS_RESPAWN,             /* 0784 */
    MS_CONTINUE,            /* 07c7 */
    MS_CONTINUE_SHOW,       /* 07d6 */
    MS_CONTINUE_WAIT,       /* 07ee */
    MS_GAME_OVER,           /* 08ee */
    MS_GAME_OVER_SOUND,     /* 0924 */
    MS_GAME_OVER_SCORES,    /* 0933 */
    MS_PALETTE,             /* palette_set 1000:1be6: wait, then set */
    MS_REVEAL,              /* screen_reveal_bg 1000:61ff */
    MS_QUIT,
} MainState;

typedef struct MainFlow {
    MainState state;
    MainState after;        /* where MS_PALETTE / MS_REVEAL continue */
    PaletteId palette;      /* MS_PALETTE's palette */
    int phase;              /* step within MS_REVEAL */
    int counter;            /* loop counter of the current state */
    struct { int16_t x0, y0, x1, y1; unsigned colour; } box;   /* the warp box (06e8) */
    unsigned attract_case;  /* CS:0267, the attract rotation */
    bool extra_wait;        /* the last frame_run's flash step reloaded the palette */
    Scroller scroller;
} MainFlow;

static MainFlow flow;

enum {
    ATTRACT_TIMEOUT_TICKS = 0x5DC,      /* 013b: 30 s on the title */
    NEW_GAME_LIVES = 5,
    NEW_GAME_BONUS_COUNTDOWN = 2,
    WAVE_START_DELAY = 0x32,
    WAVE_INTRO_TICKS = 0x32,
    WAVE_FREE_SLOTS = 0x45,
    WAVE_SPAWN_DELAY = 7,
    WAVE_SPEEDUP_PERIOD = 220,          /* DS:233A, never written */
    WAVE_SPEED_ACCUM = 0x800,
    WAVE_RESPAWN_TIMER = 0x64,
    WAVE_CLEARED_FRAMES = 11,           /* 0690 */
    WARP_LEVELS = 5,
    DEMO_WAVES = 3,
    DEMO_LEVELS = 0x40,                 /* 0290: a random level below this */
    WAVES_PER_CYCLE = 0x1B,
    LABEL_OFFSET_P1 = 0x0C,
    LABEL_OFFSET_SHARED_P2 = 0x5E,
    CONTINUE_LIVES = 5,

    /* g3 title-bank shapes (the original's ES:[offset] / 2) */
    SHAPE_GAME_OVER = 1,                /* 30EB:0002 */
    SHAPE_WAVE_FRAME = 3,               /* 30EB:0006 */
};

static void go(MainState state)
{
    flow.state = state;
    flow.phase = 0;
}

/* palette_set (1000:1be6): wait for the retrace, set the palette, go on with `after`. */
static void set_palette_then(PaletteId palette, MainState after)
{
    flow.palette = palette;
    flow.after = after;
    go(MS_PALETTE);
}

/* frame_run (1e7b:013f), with the extra retrace of its palette reload. */
static void run_frame(GameState *game)
{
    if (frame_run(game))
        flow.extra_wait = true;
}

/* screen_reveal_bg (1000:61ff), then `after`. */
static void reveal_then(MainState after)
{
    flow.after = after;
    go(MS_REVEAL);
}

static void hud_refresh_if_dirty(GameState *game)     /* game86 1000:0309 */
{
    if (!game->hud_dirty)
        return;
    game->hud_dirty = false;
    hud_refresh(game);
}

static DrawTarget shown_page(void)
{
    return draw_target_page(gfx_display_page_id());
}

static const char *text_of(TextId id)
{
    const GameText *text = asset_text(id);
    return text != NULL ? text->raw : "\\e";
}

static void scroller_run(GameState *game, TextId text, uint16_t end_offset, TextId late_text)
{
    ScrollerConfig config = {
        .text = text_of(text),
        .end_offset = end_offset,
        .late_text = late_text == TEXT_ID_COUNT ? NULL : text_of(late_text),
    };
    scroller_start(&flow.scroller, &config, &game->text);
}

/* What the scroller reads between frames (Prism, scroller.h): the keyboard's last scan code
 * (cleared after every step that is not a pause) or the joystick buttons. */
static ScrollerInput scroller_input(void)
{
    InputState *in = input_state();
    ScrollerInput input = {
        .skip = in->last_scan_code == PC_KEY_SPACE || (in->joystick_buttons & 0x10),
        .pause = in->last_scan_code == PC_KEY_P || (in->joystick_buttons & 0x20),
    };
    return input;
}

/* One scroller frame; true when the run is over. */
static bool scroller_step(void)
{
    ScrollerInput input = scroller_input();
    ScrollerStatus status = scroller_frame(&flow.scroller, input);
    if (!input.pause)
        input_state()->last_scan_code = 0;
    if (status == SCROLLER_RUNNING)
        return false;
    /* SCROLLER_SHOW_HISCORE (the story run's table, 1000:5941) is not ported yet. */
    scroller_finish(&flow.scroller);
    return true;
}

/* ---- Start-up (video_init 1000:5960) ------------------------------------------------- */

static bool boot(GameState *game)
{
    if (!game->vga) {                               /* video_set_mode e4f5: EGA registers */
        set_palette_then(PALETTE_ID_EGA, MS_BOOT_REVEAL);
        return true;
    }
    go(MS_BOOT_REVEAL);
    return true;
}

static bool boot_reveal(GameState *game)
{
    (void)game;
    gfx_clear(GFX_PAGE_BACKGROUND, 0);              /* 5963 */
    reveal_then(MS_BOOT_FIRE_RELEASE);
    return true;
}

static bool boot_fire_release(GameState *game)      /* 596c */
{
    (void)game;
    if (input_state()->controls[0] & 0x80)
        return false;                               /* still held: look again next frame */
    set_palette_then(PALETTE_ID_GAME, MS_BOOT_SCROLL_START);
    return true;
}

static bool boot_scroll_start(GameState *game)
{
    game->tune.fire_pressed = false;                /* 597b */
    game->pulse_enable = true;
    scroller_run(game, TEXT_ID_VERSION_SCROLL, 0xE800, TEXT_ID_COUNT);
    go(MS_BOOT_SCROLL);
    return true;
}

static bool boot_scroll(GameState *game)
{
    if (!scroller_step())
        return false;
    game->score[0] = game->score[1] = 0;            /* 00c5 */
    game->pulse_enable = true;                      /* 00d5 */
    go(MS_ATTRACT);
    return true;
}

/* ---- Title and attract mode (1000:00db..0306) ---------------------------------------- */

static bool attract(GameState *game)
{
    game->tick_hook = TICK_HOOK_NONE;               /* 00db */
    game->demo_mode = 0;
    game->options = game->options_default & (uint8_t)~OPT_CURRENT_IS_P2;
    game->player.level = game->level_default;
    game->flash_list = NULL;
    game->flash_pos = 0;
    set_palette_then(PALETTE_ID_GAME, MS_ATTRACT_TITLE);
    return true;
}

static bool attract_title(GameState *game)
{
    game->player.wave = 0;                          /* 0106 */
    game->continues = 4;
    game_sound_play(0x15, 1, 2);
    title_draw(game, attract_art_default());        /* 3ee0..3f6b */
    reveal_then(MS_ATTRACT_LLAMAS);
    return true;
}

static bool attract_llamas_show(GameState *game)
{
    attract_llamas_init();                          /* 0121 */
    game->tick_hook = TICK_HOOK_TITLE;
    game->draw_hook = DRAW_HOOK_TITLE;
    game->tick_count = 0;
    go(MS_TITLE);
    return true;
}

/* The attract rotation (1000:0271): story text, demo, credits, demo. */
static bool attract_cycle(GameState *game)
{
    switch (flow.attract_case++ & 3) {
    case 0:                                         /* 02d4 */
        title_tune_start(game);
        scroller_run(game, TEXT_ID_STORY_SCROLL, 0, TEXT_ID_STORY_LATE);
        go(MS_ATTRACT_SCROLL);
        return true;
    case 2:                                         /* 02f6 */
        title_tune_start(game);
        scroller_run(game, TEXT_ID_CREDITS_SCROLL, 0x8800, TEXT_ID_COUNT);
        go(MS_ATTRACT_SCROLL);
        return true;
    default:                                        /* 0287: a demo of a random level */
        game->level_default = game->player.level;
        game->player.level = rng_next_byte(&game->rng) & (DEMO_LEVELS - 1);
        game->player.wave = rng_next_byte(&game->rng) & 0x1F;
        if (game->player.wave >= WAVES_PER_CYCLE)
            game->player.wave = 0;
        game->options_default = game->options;
        game->options = 0;
        game->demo_mode = DEMO_WAVES;
        game->tuning.init0d_hp = 0;
        game->player.script_param[1] = 0;
        game->player.script_param[0] = 0;
        go(MS_NEW_GAME);
        return true;
    }
}

static bool title(GameState *game)
{
    run_frame(game);                                /* 0136 */
    if (game->quit)
        return false;
    if ((int16_t)game->tick_count >= ATTRACT_TIMEOUT_TICKS) {
        attract_cycle(game);
        return false;
    }
    const uint8_t *controls = input_state()->controls;
    bool second = (controls[1] & 0x80) &&
                  (!(game->options & OPT_TWO_PLAYERS) || (game->options & OPT_DOUBLE_CONTROLLER));
    if (second)
        game->started_with_stick_b = true;          /* 015b */
    else if (controls[0] & 0x80)
        game->started_with_stick_b = false;         /* 016a */
    else
        return false;
    game->score[0] = game->score[1] = 0;            /* 0170 */
    game->loop_count = game->options & OPT_TWO_PLAYERS;
    game->player.level = 0;
    go(MS_NEW_GAME);
    return false;                                   /* the frame_run above was this frame's */
}

static bool attract_scroll(GameState *game)
{
    (void)game;
    if (!scroller_step())
        return false;
    go(MS_ATTRACT);                                 /* 02f3 / 0306 */
    return true;
}

/* ---- A game (1000:018f..0616) --------------------------------------------------------- */

static bool new_game(GameState *game)
{
    game->tick_hook = TICK_HOOK_NONE;               /* 018f */
    game->game_started = 0;
    game->wave_start_delay = WAVE_START_DELAY;
    game->loop_count = 0;
    game->player.param_21ca = 0;
    game->player.lives = NEW_GAME_LIVES;
    game->player.bonus_countdown = NEW_GAME_BONUS_COUNTDOWN;
    game->ship[0] = (Point16){ 0x98, 0x8C };
    if (game->demo_mode != 0 || (game->options & OPT_TWO_PLAYERS)) {
        if (game->demo_mode == 0)
            game->ship[0].x = 0x84;
        game->ship[1] = (Point16){ 0xAC, 0x8C };    /* 01d9 */
    }
    go(MS_NEXT_LEVEL);
    return true;
}

static bool next_level(GameState *game)
{
    game_sound_play(0x15, 1, 0x1E);                 /* 01e5 */
    game->player.swarm_reaim_period = 0x0F;
    game->player.multi_shot = 0;
    game->powerup_0652 = 0;
    game->level_flags = -1;
    script_start_level(game, asset_levels());       /* 020b..045d */
    go(MS_WAVE_START);
    return true;
}

static bool wave_start(GameState *game)
{
    if (game->game_started == 0) {                  /* 0461 */
        game->game_started = 1;
        player_copy_state(game);
    }
    game->player_label_offset = LABEL_OFFSET_P1;    /* 0471 */
    if ((game->options & OPT_CURRENT_IS_P2) && !(game->options & OPT_DOUBLE_CONTROLLER))
        game->player_label_offset = LABEL_OFFSET_SHARED_P2;

    game->tick_hook = TICK_HOOK_NONE;               /* 048b */
    game->ship[0] = (Point16){ 0xA0, 0x68 };
    if (game->options & OPT_TWO_PLAYERS) {
        game->ship[0].x = 0x8C;
        game->ship[1] = (Point16){ 0xB4, 0x68 };
    }
    game->unhatched = 0;                            /* 04b6 */
    game->free_slots = WAVE_FREE_SLOTS;
    game->spawn_delay = WAVE_SPAWN_DELAY;
    game->enemies_alive = 0;
    game->wave_result = WAVE_RUNNING;
    game->spawning_done = false;
    game->wave_intro_timer = WAVE_INTRO_TICKS;
    shots_clear(game);
    entities_clear(game);
    level_select_variant(game, asset_levels());
    for (int p = 0; p < PLAYER_COUNT; p++) {        /* 04e9 */
        game->trail_head[p] = 0;
        game->follower_lag[p] = 0;
        for (int i = 0; i < TRAIL_LENGTH; i++) {
            game->trail[p][i].x = game->ship[p].x;
            game->trail[p][i].y = game->ship[p].y;
            game->trail[p][i].flag = 0;
        }
    }
    game->speedup_period = WAVE_SPEEDUP_PERIOD;     /* 0532 */
    game->speedup_timer = game->speedup_period;
    game->speed_accum = WAVE_SPEED_ACCUM;
    game->fire_turn = 0;
    game->shots_bounce = false;
    game->spawn_remaining = -1;

    /* 055c: the wave's background */
    gfx_clear(GFX_PAGE_BACKGROUND, 0);
    draw_rect_outline(GFX_PAGE_BACKGROUND, 0, 8, 0x13E, 0xC6, 2);
    DrawTarget background = draw_target_page(GFX_PAGE_BACKGROUND);
    draw_sprite_unrecorded(&background, asset_sprite_gfx(SPRITE_BANK_TITLE, SHAPE_WAVE_FRAME),
                           0x9F, 3);
    if (game->player.wave_type != 0)
        text_print_home(&background, &game->text, text_of(TEXT_ID_HERD_WAVE_BANNER));
    if (game->demo_mode != 0)
        text_print_home(&background, &game->text, text_of(TEXT_ID_DEMO_BANNER));
    const GameText *level_title = asset_level_title(game->player.level);
    if (level_title != NULL)
        text_print_home(&background, &game->text, level_title->raw);
    reveal_then(MS_WAVE_SHOW);
    return true;
}

static bool wave_show(GameState *game)
{
    game->hud_dirty = true;                         /* 05d5: both players' HUD */
    hud_refresh_if_dirty(game);
    player_swap_state(game);
    game->hud_dirty = true;
    hud_refresh_if_dirty(game);
    player_swap_state(game);
    game->smart_bomb = false;                       /* 05ed */
    game->play_flag_0617 = -1;
    game->frame_drawn = false;
    game->tick_hook = TICK_HOOK_PLAY;
    game->draw_hook = DRAW_HOOK_PLAY;
    game->respawn_timer[0] = WAVE_RESPAWN_TIMER;
    game->respawn_timer[1] = WAVE_RESPAWN_TIMER;
    go(MS_WAVE);
    return true;
}

static bool wave(GameState *game)
{
    run_frame(game);                                /* 0616 */
    hud_refresh_if_dirty(game);
    if (game->wave_result != WAVE_RUNNING) {
        game->flash_list = NULL;                    /* 0625 */
        game->flash_pos = 0;
        set_palette_then(PALETTE_ID_GAME, MS_WAVE_OVER);
    }
    return false;
}

/* ---- The wave's outcome (1000:0633..07b4) --------------------------------------------- */

static bool wave_over(GameState *game)
{
    switch (game->wave_result) {
    case WAVE_WARP: {                               /* 063a */
        game->tick_hook = TICK_HOOK_NONE;
        DrawTarget page = shown_page();
        text_print_home(&page, &game->text, text_of(TEXT_ID_WARP_BANNER));
        game_sound_play(6, 0, 0x0A);
        go(MS_WARP_SOUND);
        flow.counter = 0;
        return false;                               /* 0658 wait_vsync */
    }
    case WAVE_ABORTED:
        go(MS_ATTRACT);                             /* 0683 */
        return true;
    case WAVE_PLAYER_DEAD:
        go(MS_PLAYER_DIED);
        return true;
    default:
        flow.counter = WAVE_CLEARED_FRAMES;         /* 0690: the play tick runs on */
        game->tick_hook = TICK_HOOK_PLAY;
        go(MS_WAVE_CLEARED);
        return true;
    }
}

static bool warp_sound(GameState *game)             /* 065d */
{
    if (game_sound_status().playing != 0)
        return false;
    game->player.level += WARP_LEVELS;
    score_add(game, 4, 0x32);
    go(MS_WAVE_NEXT);
    return true;
}

static bool wave_cleared(GameState *game)
{
    if (flow.counter-- > 0) {                       /* 0699 */
        run_frame(game);
        return false;
    }
    game->tick_hook = TICK_HOOK_NONE;               /* 06a2 */
    game->flash_list = NULL;
    game->flash_pos = 0;
    set_palette_then(PALETTE_ID_GAME, MS_WAVE_NEXT);
    return true;
}

static bool wave_next(GameState *game)
{
    if (game->demo_mode != 0 && --game->demo_mode == 0) {   /* 06b6 */
        go(MS_ATTRACT);
        return true;
    }
    game->flash_list = NULL;                        /* 06c6 */
    game->flash_pos = 0;
    set_palette_then(PALETTE_ID_GAME, MS_WARP_BOX_START);
    return true;
}

/* DS:0619, four colour bytes. [H] Not extracted yet: a stand-in until game_data has it. */
static unsigned warp_box_colour(unsigned index)
{
    return (index + 1) & 0x0F;
}

static void box_reset(void)
{
    flow.box.x0 = 0x5E;
    flow.box.y0 = 0x66;
    flow.box.x1 = 0xE0;
    flow.box.y1 = 0x68;
    flow.box.colour = 0;
}

static void box_grow(void)
{
    flow.box.y1 += 2;
    flow.box.x1 += 2;
    flow.box.y0 -= 2;
    flow.box.x0 -= 2;
}

static void box_draw(unsigned colour)
{
    draw_rect_outline(gfx_display_page_id(), flow.box.x0, flow.box.y0, flow.box.x1,
                      flow.box.y1, (uint8_t)colour);
}

static bool warp_box_start(GameState *game)
{
    (void)game;
    box_reset();
    go(MS_WARP_BOX);
    return false;                                   /* 06f3 wait_vsync */
}

/* 06e8: a growing rectangle in four colours, one step per frame. */
static bool warp_box(GameState *game)
{
    (void)game;
    box_draw(warp_box_colour(flow.box.colour));
    box_grow();
    flow.box.colour = (flow.box.colour + 1) & 3;
    if (flow.box.x0 >= 0)
        return false;
    box_reset();
    go(MS_WARP_BOX_CLEAR);
    return false;                                   /* 072f wait_vsync */
}

/* 072d: the same rectangle again in colour 0. */
static bool warp_box_clear(GameState *game)
{
    (void)game;
    box_draw(0);
    box_grow();
    if (flow.box.x0 != 0)
        return false;
    go(MS_NEXT_LEVEL);
    return true;
}

static bool player_died(GameState *game)
{
    if (--game->player.lives == 0) {                /* 075c */
        if (!(game->options & OPT_SHARED_START)) {
            go(MS_CONTINUE);
            return true;
        }
        player_swap_state(game);
        if (game->player.lives == 0) {
            go(MS_CONTINUE);
            return true;
        }
        player_swap_state(game);
    }
    game->flash_list = NULL;                        /* 0776 */
    game->flash_pos = 0;
    set_palette_then(PALETTE_ID_GAME, MS_RESPAWN);
    return true;
}

static bool respawn(GameState *game)
{
    wave_spawn_enemies(game);                       /* 0784 */
    if (game->options & OPT_SHARED_START) {
        player_swap_state(game);
        if (game->player.lives == 0)
            player_swap_state(game);
        game->wave_start_delay = WAVE_START_DELAY;  /* 079b */
        if (game->game_started == 1) {
            game->game_started = 2;
            go(MS_NEXT_LEVEL);
            return true;
        }
    }
    game->spawn = (SpawnCursor){ SPAWN_FROM_ROSTER, 0, 0 };   /* 07b1 */
    go(MS_WAVE_START);
    return true;
}

/* ---- Continue and game over (1000:07c7..097d) ----------------------------------------- */

static bool continue_prompt(GameState *game)
{
    if (--game->continues == 0) {                   /* 07c7 */
        go(MS_GAME_OVER);
        return true;
    }
    draw_save_screen_as_background();               /* 07d0 */
    reveal_then(MS_CONTINUE_SHOW);
    return true;
}

static bool continue_show(GameState *game)
{
    continue_begin(game);                           /* 07d6 */
    game->draw_hook = DRAW_HOOK_CONTINUE;
    game->tick_hook = TICK_HOOK_CONTINUE;
    go(MS_CONTINUE_WAIT);
    return true;
}

static bool continue_wait(GameState *game)
{
    run_frame(game);                                /* 07ee */
    if (game->continue_ticks < 0) {                 /* 0807: fire pressed */
        game->player.lives = CONTINUE_LIVES;
        game->player_other.lives = CONTINUE_LIVES;
        game->flash_list = NULL;
        game->flash_pos = 0;
        set_palette_then(PALETTE_ID_GAME, MS_RESPAWN);
    } else if (game->continue_seconds < 0) {
        draw_show_background();                     /* 0801 */
        go(MS_GAME_OVER);
    }
    return false;
}

static bool game_over(GameState *game)
{
    (void)game;
    DrawTarget page = shown_page();                 /* 08ee */
    draw_sprite_unrecorded(&page, asset_sprite_gfx(SPRITE_BANK_TITLE, SHAPE_GAME_OVER), 0xA0, 0x5A);
    game_sound_play(0x1E, 1, 8);
    go(MS_GAME_OVER_SOUND);
    return false;                                   /* 091f wait_vsync */
}

static bool game_over_sound(GameState *game)        /* 0924 */
{
    (void)game;
    if (game_sound_status().playing != 0)
        return false;
    go(MS_GAME_OVER_SCORES);
    return true;
}

/* 0933..097d: the hi-score check, name entry and table are not ported to main_sm yet. */
static bool game_over_scores(GameState *game)
{
    game->hiscore_mark[0] = -1;
    game->hiscore_mark[1] = -1;
    go(MS_ATTRACT);
    return true;
}

/* ---- The waits ----------------------------------------------------------------------- */

static bool palette(GameState *game)
{
    (void)game;
    if (flow.phase++ == 0)
        return false;                               /* palette_set's wait_vsync */
    gfx_set_game_palette(asset_palette(flow.palette));
    go(flow.after);
    return true;
}

/* screen_reveal_bg: black, three more retraces, the clean background into both pages, the
 * game palette. With palette_set's own waits that is five frames. game86 1000:61ff */
static bool reveal(GameState *game)
{
    (void)game;
    switch (flow.phase++) {
    case 0:
        return false;                               /* palette_set's wait */
    case 1:
        gfx_set_game_palette(asset_palette(PALETTE_ID_BLACK));
        return false;                               /* 6207 */
    case 2:
    case 3:
        return false;                               /* 620c, 6211 */
    case 4:
        gfx_copy_page(GFX_PAGE_0, GFX_PAGE_BACKGROUND);   /* 6217..62dc */
        gfx_copy_page(GFX_PAGE_1, GFX_PAGE_BACKGROUND);
        return false;                               /* palette_set's wait */
    default:
        gfx_set_game_palette(asset_palette(PALETTE_ID_GAME));
        go(flow.after);
        return true;
    }
}

/* ---- Driver --------------------------------------------------------------------------- */

/* One state's code; true = go on with the next state in this frame, false = the frame ends. */
static bool step(GameState *game)
{
    switch (flow.state) {
    case MS_BOOT:                   return boot(game);
    case MS_BOOT_REVEAL:            return boot_reveal(game);
    case MS_BOOT_FIRE_RELEASE:      return boot_fire_release(game);
    case MS_BOOT_SCROLL_START:      return boot_scroll_start(game);
    case MS_BOOT_SCROLL:            return boot_scroll(game);
    case MS_ATTRACT:                return attract(game);
    case MS_ATTRACT_TITLE:          return attract_title(game);
    case MS_ATTRACT_LLAMAS:         return attract_llamas_show(game);
    case MS_TITLE:                  return title(game);
    case MS_ATTRACT_SCROLL:         return attract_scroll(game);
    case MS_NEW_GAME:               return new_game(game);
    case MS_NEXT_LEVEL:             return next_level(game);
    case MS_WAVE_START:             return wave_start(game);
    case MS_WAVE_SHOW:              return wave_show(game);
    case MS_WAVE:                   return wave(game);
    case MS_WAVE_OVER:              return wave_over(game);
    case MS_WARP_SOUND:             return warp_sound(game);
    case MS_WAVE_CLEARED:           return wave_cleared(game);
    case MS_WAVE_NEXT:              return wave_next(game);
    case MS_WARP_BOX_START:         return warp_box_start(game);
    case MS_WARP_BOX:               return warp_box(game);
    case MS_WARP_BOX_CLEAR:         return warp_box_clear(game);
    case MS_PLAYER_DIED:            return player_died(game);
    case MS_RESPAWN:                return respawn(game);
    case MS_CONTINUE:               return continue_prompt(game);
    case MS_CONTINUE_SHOW:          return continue_show(game);
    case MS_CONTINUE_WAIT:          return continue_wait(game);
    case MS_GAME_OVER:              return game_over(game);
    case MS_GAME_OVER_SOUND:        return game_over_sound(game);
    case MS_GAME_OVER_SCORES:       return game_over_scores(game);
    case MS_PALETTE:                return palette(game);
    case MS_REVEAL:                 return reveal(game);
    case MS_QUIT:                   return false;
    }
    return false;
}

void main_sm_start(GameState *game)
{
    (void)game;
    flow = (MainFlow){ .state = MS_BOOT };
}

void main_sm_frame(GameState *game)
{
    if (flow.extra_wait) {
        flow.extra_wait = false;
        return;
    }
    /* Bounded so that a state machine error cannot hang the host. */
    for (int i = 0; i < 64 && !game->quit; i++)
        if (!step(game))
            break;
    if (game->quit)
        flow.state = MS_QUIT;
}
