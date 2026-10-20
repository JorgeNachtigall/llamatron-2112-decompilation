/*
 * main_sm.c - the top-level flow as a per-frame state machine (see main_sm.h).
 *
 * The game, as the player sees it:
 *
 *   start-up     the version text scrolls up once (MS_BOOT..MS_BOOT_SCROLL)
 *   attract      the title with its walking llamas until fire or 30 seconds; then, in turn,
 *                the story text, a demo, the credits, a demo (MS_ATTRACT..MS_ATTRACT_TEXT)
 *   a game       per level: the level script, then waves. A wave starts with the playfield
 *                and the HUD, is played until it ends, and its outcome decides what follows:
 *                cleared -> the warp box -> next level; warp gate -> 5 levels on; the player
 *                died -> the same wave again, or the continue prompt when no lives are left
 *   game over    the banner, then each player's score against the hi-score table (a name
 *                entry for a new entry), the table, and back to attract mode
 *
 * Each state's function runs the original's code from one wait for the vertical retrace to
 * the next. It returns true when it moved on to another state without waiting (the next
 * state runs in the same frame) and false when the frame ends there. Original addresses
 * are game86 1000:xxxx unless written otherwise.
 */
#include "main_sm.h"

#include "attract.h"
#include "bullets.h"
#include "continue.h"
#include "draw.h"
#include "draw_play.h"
#include "entity_dispatch.h"
#include "frame.h"
#include "gfx.h"
#include "game_data.h"
#include "hiscore.h"
#include "hud.h"
#include "input.h"
#include "player.h"
#include "rng.h"
#include "score.h"
#include "script.h"
#include "scroller.h"
#include "sound.h"
#include "tick.h"

typedef enum MainState {
    /* Start-up: video_init 5960 */
    MS_BOOT,                /* EGA: the EGA palette first */
    MS_BOOT_REVEAL,         /* 5963: a black background revealed */
    MS_BOOT_FIRE_RELEASE,   /* 596c: wait until fire is let go */
    MS_BOOT_SCROLL_START,   /* 5973: the version text */
    MS_BOOT_SCROLL,         /* text_scroller 5844, until it has scrolled past */
    /* Attract mode: 00db..0306 */
    MS_ATTRACT,             /* 00db: back to the player's settings */
    MS_ATTRACT_TITLE,       /* 0106: the title picture */
    MS_TITLE_LLAMAS,        /* 0121: the llamas start walking */
    MS_TITLE,               /* 0136..0168: until fire or the timeout */
    MS_ATTRACT_TEXT,        /* 02d4 / 02f6: the story or credits scroller */
    /* A game: 018f..0616 */
    MS_NEW_GAME,            /* 018f: lives, start positions */
    MS_NEXT_LEVEL,          /* 01e5: the level script */
    MS_WAVE_START,          /* 045d: the playfield of a wave */
    MS_WAVE_BEGIN_PLAY,     /* 05d5: the HUD, the play hooks */
    MS_WAVE_PLAY,           /* 0616: until the wave ends */
    /* The wave's outcome: 0633..07b4 */
    MS_WAVE_OVER,           /* 0633: what the result leads to */
    MS_WARP_SOUND,          /* 065d: the warp gate's banner and sound */
    MS_WAVE_CLEARED,        /* 0699: a few more frames of play */
    MS_WAVE_NEXT,           /* 06b6: end of a demo, or on to the warp box */
    MS_WARP_BOX_START,      /* 06d4 */
    MS_WARP_BOX,            /* 06e8: the growing rectangle */
    MS_WARP_BOX_CLEAR,      /* 072d: and its erasing */
    MS_PLAYER_DIED,         /* 075c: a life lost */
    MS_RESPAWN,             /* 0784: the same wave again */
    /* Continue and game over: 07c7..097d */
    MS_CONTINUE_PROMPT,     /* 07c7: a continue left? */
    MS_CONTINUE_SHOW,       /* 07d6: the countdown's hooks */
    MS_CONTINUE_WAIT,       /* 07ee: until fire or the countdown's end */
    MS_GAME_OVER,           /* 08ee: the banner and its sound */
    MS_GAME_OVER_SOUND,     /* 0924: until the sound has played */
    MS_HISCORE_START,       /* 0933 */
    MS_HISCORE_CHECK,       /* hiscore_check_insert 5da2, for each player */
    MS_NAME_ENTRY_SCREEN,   /* hiscore_name_entry 59fa, called at 5e47 */
    MS_NAME_ENTRY_START,    /* 5a88: the entry's hooks */
    MS_NAME_ENTRY,          /* 5aae: until the entry times out */
    MS_GAME_OVER_TABLE,     /* 094f */
    MS_TABLE_SHOW,          /* hiscore_table_show 5c3c */
    MS_TABLE_SCROLL,        /* 5d19: the table scrolls in */
    MS_TABLE_END,           /* 5d3b */
    MS_TABLE_WAIT,          /* 5d47: until fire or 20 seconds */
    /* The waits shared by many states */
    MS_PALETTE,             /* palette_set 1be6: wait, then set */
    MS_REVEAL,              /* screen_reveal_bg 61ff */
    MS_QUIT,
} MainState;

typedef struct MainFlow {
    MainState state;
    MainState after;        /* where MS_PALETTE / MS_REVEAL go on */
    PaletteId palette;      /* MS_PALETTE's palette */
    int phase;              /* calls of the current state so far: the frame within
                               MS_REVEAL, a wait's first frame, after_frame_run */
    int frames_left;        /* MS_WAVE_CLEARED's frames */
    struct { int16_t x0, y0, x1, y1; unsigned colour; } box;   /* the warp box (06e8) */
    unsigned attract_case;  /* CS:0267, the attract rotation */
    bool extra_wait;        /* the last frame_run's flash step reloaded the palette */
    bool second_check;      /* game over: the other player's score is being checked */
    int rank;               /* the hi-score row being named */
    int rows_left;          /* the hi-score table's scroll */
    TickHook saved_tick_hook;   /* pushed by the name entry (5a8e) */
    DrawHook saved_draw_hook;
    NameEntry name;
    Scroller scroller;
} MainFlow;

static MainFlow flow;

/* The host's scores.yak hook from GameLaunch (5e89 writes the file itself). */
typedef struct ScoreSaver {
    void (*save)(const uint8_t *yak, size_t size, void *user);
    void *user;
} ScoreSaver;

static ScoreSaver score_saver;

enum {
    ATTRACT_TIMEOUT_TICKS = 0x5DC,      /* 013b: 30 s on the title */
    ATTRACT_CONTINUES = 4,              /* 0106: the prompt takes one first, so 3 continues */
    DEMO_WAVES = 3,
    DEMO_LEVELS = 0x40,                 /* 0290: a random level below this */
    WAVES_PER_CYCLE = 0x1B,
    NEW_GAME_LIVES = 5,
    NEW_GAME_BONUS_COUNTDOWN = 2,
    CONTINUE_LIVES = 5,
    LEVEL_SWARM_REAIM_PERIOD = 0x0F,    /* 01f3 */
    WAVE_START_DELAY = 0x32,
    WAVE_INTRO_TICKS = 0x32,
    WAVE_FREE_SLOTS = 0x45,
    WAVE_SPAWN_DELAY = 7,
    WAVE_SPEEDUP_PERIOD = 220,          /* DS:233A, never written */
    WAVE_SPEED_ACCUM = 0x800,
    WAVE_RESPAWN_TIMER = 0x64,
    WAVE_CLEARED_FRAMES = 11,           /* 0690 */
    WARP_LEVELS = 5,
    WARP_BONUS_THOUSANDS = 0x32,        /* 0671: 50,000 points */
    SCORE_PLACE_THOUSANDS = 4,          /* score_add's digit */
    LABEL_OFFSET_P1 = 0x0C,
    LABEL_OFFSET_SHARED_P2 = 0x5E,
    TABLE_WAIT_TICKS = 0x3E8,           /* 5d4e */
    /* Scroller runs' last window offset (ScrollerConfig.end_offset, smc CS:582d). */
    VERSION_SCROLL_END = 0xE800,        /* the version text at start-up, 597b.. */
    STORY_SCROLL_END = 0,               /* unused: the story ends through its late text */
    CREDITS_SCROLL_END = 0x8800,        /* 02f6 */
    /* The joystick's buttons in InputState.joystick_buttons. */
    JOYSTICK_BUTTON_1 = 0x10,
    JOYSTICK_BUTTON_2 = 0x20,
};

/* Where the ships stand: at a new game (01b9..01d9), for the first frames of each wave
 * (048b..04a4). One ship stands in the middle; two stand side by side. */
static const Point16 NEW_GAME_SHIP = { 0x98, 0x8C };
static const int16_t NEW_GAME_SHIP_LEFT_X = 0x84;
static const Point16 NEW_GAME_SHIP_2 = { 0xAC, 0x8C };
static const Point16 WAVE_SHIP = { 0xA0, 0x68 };
static const int16_t WAVE_SHIP_LEFT_X = 0x8C;
static const Point16 WAVE_SHIP_2 = { 0xB4, 0x68 };

/* The warp box's first rectangle (06dc..06e5). */
enum { WARP_BOX_X0 = 0x5E, WARP_BOX_Y0 = 0x66, WARP_BOX_X1 = 0xE0, WARP_BOX_Y1 = 0x68 };

/* ---- Moving between states and the waits ----------------------------------------------- */

static void go(MainState state)
{
    flow.state = state;
    flow.phase = 0;
}

/* palette_set (1be6): wait for the retrace, set the palette, go on with `after`. */
static void set_palette_then(PaletteId palette, MainState after)
{
    flow.palette = palette;
    flow.after = after;
    go(MS_PALETTE);
}

/* screen_reveal_bg (61ff), then `after`. */
static void reveal_then(MainState after)
{
    flow.after = after;
    go(MS_REVEAL);
}

/* frame_run (1e7b:013f): one frame of play or of a screen with hooks. When its screen flash
 * reloads the palette, that costs one more retrace. */
static void run_frame(GameState *game)
{
    if (frame_run(game))
        flow.extra_wait = true;
}

/* The frame_run loops (0136, 0616, 07ee, 5aae) test their exit after frame_run's wait_vsync,
 * so they see the ticks raised during it; here that test opens the next frame. False on a
 * state's first call (no frame_run yet), true from then on. */
static bool after_frame_run(void)
{
    return flow.phase++ != 0;
}

/* The shared waits' first call ends the frame (the wait); the second goes on. */
static bool first_call(void)
{
    return flow.phase++ == 0;
}

static bool fire_held(void)
{
    return input_state()->controls[0] & INPUT_FIRE;
}

/* ---- Helpers ------------------------------------------------------------------------- */

static void flash_cancel(GameState *game)
{
    game->flash_list = NULL;
    game->flash_pos = 0;
}

static void hud_refresh_if_dirty(GameState *game)     /* 0309 */
{
    if (!game->hud_dirty)
        return;
    game->hud_dirty = false;
    hud_refresh(game);
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
        .skip = in->last_scan_code == PC_KEY_SPACE || (in->joystick_buttons & JOYSTICK_BUTTON_1),
        .pause = in->last_scan_code == PC_KEY_P || (in->joystick_buttons & JOYSTICK_BUTTON_2),
    };
    return input;
}

/* One scroller frame. SCROLLER_DONE: the run is finished; SCROLLER_SHOW_HISCORE: the story
 * run goes on to the hi-score table (MS_TABLE_SHOW) in this same frame, as 5941 follows the
 * skip check (58a4) or the scroll step (5928) without a wait. */
static ScrollerStatus scroller_step(void)
{
    ScrollerInput input = scroller_input();
    ScrollerStatus status = scroller_frame(&flow.scroller, input);
    /* 58b8 clears the latch only on a scroll step: a pause (58aa) and a skip that ends the
     * run (5896) keep it, so a space skip also stops the table's scroll at once (5d25). */
    bool skipped_out = input.skip && status != SCROLLER_RUNNING;
    if (!input.pause && !skipped_out)
        input_state()->last_scan_code = 0;
    if (status == SCROLLER_SHOW_HISCORE)            /* 5941: the pen is already below */
        go(MS_TABLE_SHOW);
    else if (status == SCROLLER_DONE)
        scroller_finish(&flow.scroller);            /* 594a */
    return status;
}

/* The pictures and texts of the hi-score screens. */
static const HiscoreArt *hiscore_art(void)
{
    static HiscoreArt art;
    static bool built;
    if (!built) {
        art = (HiscoreArt){
            .logo_left = asset_title_shape(TITLE_SHAPE_LOGO_LEFT),
            .logo_right = asset_title_shape(TITLE_SHAPE_LOGO_RIGHT),
            .cursor = asset_title_shape(TITLE_SHAPE_NAME_CURSOR),
            .marker = { asset_sprite_id_gfx(SPRITE_ID_HISCORE_MARK_1),
                        asset_sprite_id_gfx(SPRITE_ID_HISCORE_MARK_2) },
            .control_labels = asset_title_shape,
            .table_heading = text_of(TEXT_ID_HISCORE_TITLE),
            .entry_heading = text_of(TEXT_ID_HISCORE_ENTRY),
            .entry_prompt = text_of(TEXT_ID_HISCORE_NAME),
            .data = asset_hiscore_data(),
        };
        built = art.data != NULL;
    }
    return &art;
}

/* ---- Start-up (video_init 5960) ------------------------------------------------------ */

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
    if (fire_held())
        return false;                               /* still held: look again next frame */
    set_palette_then(PALETTE_ID_GAME, MS_BOOT_SCROLL_START);
    return true;
}

static bool boot_scroll_start(GameState *game)
{
    game->tune.fire_pressed = false;                /* 597b */
    game->pulse_enable = true;
    scroller_run(game, TEXT_ID_VERSION_SCROLL, VERSION_SCROLL_END, TEXT_ID_COUNT);
    go(MS_BOOT_SCROLL);
    return true;
}

static bool boot_scroll(GameState *game)
{
    ScrollerStatus status = scroller_step();
    if (status != SCROLLER_DONE)
        return status != SCROLLER_RUNNING;
    game->score[0] = game->score[1] = 0;            /* 00c5 */
    game->pulse_enable = true;                      /* 00d5 */
    go(MS_ATTRACT);
    return true;
}

/* ---- Title and attract mode (00db..0306) ---------------------------------------------- */

/* Back from a game, a demo or a text: the player's own options and starting level. */
static bool attract(GameState *game)
{
    game->tick_hook = TICK_HOOK_NONE;               /* 00db */
    game->demo_mode = 0;
    game->options = game->options_default & (uint8_t)~OPT_CURRENT_IS_P2;
    game->player.level = game->level_default;
    flash_cancel(game);
    set_palette_then(PALETTE_ID_GAME, MS_ATTRACT_TITLE);
    return true;
}

static bool attract_title(GameState *game)
{
    game->player.wave = 0;                          /* 0106 */
    game->continues = ATTRACT_CONTINUES;
    game_sound_play(SAMPLE_ID_WAVE_START, 1, 2);
    title_draw(game, attract_art_default());        /* 3ee0..3f6b */
    reveal_then(MS_TITLE_LLAMAS);
    return true;
}

static bool title_llamas_start(GameState *game)
{
    attract_llamas_init();                          /* 0121 */
    game->tick_hook = TICK_HOOK_TITLE;
    game->draw_hook = DRAW_HOOK_TITLE;
    game->tick_count = 0;
    go(MS_TITLE);
    return true;
}

/* What follows a title that timed out (0271), in turn: the story, a demo, the credits, a
 * demo. A demo plays DEMO_WAVES waves of a random level without the player's options. */
static bool attract_cycle(GameState *game)
{
    switch (flow.attract_case++ & 3) {
    case 0:                                         /* 02d4 */
        title_tune_start(game);
        scroller_run(game, TEXT_ID_STORY_SCROLL, STORY_SCROLL_END, TEXT_ID_STORY_LATE);
        go(MS_ATTRACT_TEXT);
        return true;
    case 2:                                         /* 02f6 */
        title_tune_start(game);
        scroller_run(game, TEXT_ID_CREDITS_SCROLL, CREDITS_SCROLL_END, TEXT_ID_COUNT);
        go(MS_ATTRACT_TEXT);
        return true;
    default:                                        /* 0287 */
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

/* The title: fire on stick A starts a game; so does stick B, except in a two-player game
 * without the double-controller option. After 30 seconds the attract rotation. */
static bool title(GameState *game)
{
    if (after_frame_run()) {
        if ((int16_t)game->tick_count >= ATTRACT_TIMEOUT_TICKS)     /* 013b */
            return attract_cycle(game);
        const uint8_t *controls = input_state()->controls;
        bool second = (controls[1] & INPUT_FIRE) &&
                      (!(game->options & OPT_TWO_PLAYERS) ||
                       (game->options & OPT_DOUBLE_CONTROLLER));
        bool first = controls[0] & INPUT_FIRE;
        if (second || first) {
            game->started_with_stick_b = second;    /* 015b / 016a */
            game->score[0] = game->score[1] = 0;    /* 0170 */
            game->loop_count = game->options & OPT_TWO_PLAYERS;
            game->player.level = 0;
            go(MS_NEW_GAME);
            return true;
        }
    }
    run_frame(game);                                /* 0136 */
    return false;
}

static bool attract_text(GameState *game)
{
    (void)game;
    ScrollerStatus status = scroller_step();
    if (status != SCROLLER_DONE)
        return status != SCROLLER_RUNNING;
    go(MS_ATTRACT);                                 /* 02f3 / 0306 */
    return true;
}

/* ---- A game (018f..0616) ------------------------------------------------------------- */

static bool new_game(GameState *game)
{
    /* Reached only from the title (0170) and the demo (0287): the title llamas' bytes stay
     * in slots 0..8, and fields a spawn does not set read them (DEVIATIONS #26). */
    attract_end(game);
    game->tick_hook = TICK_HOOK_NONE;               /* 018f */
    game->game_started = 0;
    game->wave_start_delay = WAVE_START_DELAY;
    game->loop_count = 0;
    game->player.level_loops = 0;
    game->player.lives = NEW_GAME_LIVES;
    game->player.bonus_countdown = NEW_GAME_BONUS_COUNTDOWN;
    game->ship[0] = NEW_GAME_SHIP;
    if (game->demo_mode != 0 || (game->options & OPT_TWO_PLAYERS)) {
        if (game->demo_mode == 0)
            game->ship[0].x = NEW_GAME_SHIP_LEFT_X;
        game->ship[1] = NEW_GAME_SHIP_2;            /* 01d9 */
    }
    go(MS_NEXT_LEVEL);
    return true;
}

static bool next_level(GameState *game)
{
    game_sound_play(SAMPLE_ID_WAVE_START, 1, 0x1E);     /* 01e5 */
    game->player.swarm_reaim_period = LEVEL_SWARM_REAIM_PERIOD;
    game->player.multi_shot = 0;
    game->shot_power_kept = 0;
    game->level_flags = -1;
    script_start_level(game, asset_levels());       /* 020b..045d */
    go(MS_WAVE_START);
    return true;
}

/* A wave from scratch (also after a death): ships home, no shots or enemies, both trails at
 * the ships, the playfield drawn and revealed. */
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
    game->ship[0] = WAVE_SHIP;
    if (game->options & OPT_TWO_PLAYERS) {
        game->ship[0].x = WAVE_SHIP_LEFT_X;
        game->ship[1] = WAVE_SHIP_2;
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

    wave_draw_background(game);                     /* 055c..05cf */
    reveal_then(MS_WAVE_BEGIN_PLAY);
    return true;
}

/* Both players' HUD, then the play hooks: from here the timer tick plays the wave. */
static bool wave_begin_play(GameState *game)
{
    game->hud_dirty = true;                         /* 05d5 */
    hud_refresh_if_dirty(game);
    player_swap_state(game);
    game->hud_dirty = true;
    hud_refresh_if_dirty(game);
    player_swap_state(game);
    game->smart_bomb = false;                       /* 05ed */
    game->play_ticked = -1;
    game->frame_drawn = false;
    game->tick_hook = TICK_HOOK_PLAY;
    game->draw_hook = DRAW_HOOK_PLAY;
    game->respawn_timer[0] = WAVE_RESPAWN_TIMER;
    game->respawn_timer[1] = WAVE_RESPAWN_TIMER;
    go(MS_WAVE_PLAY);
    return true;
}

/* Frames of play until the tick sets the wave's result. */
static bool wave_play(GameState *game)
{
    if (after_frame_run()) {
        hud_refresh_if_dirty(game);                 /* 061b */
        if (game->wave_result != WAVE_RUNNING) {
            flash_cancel(game);                     /* 0625 */
            set_palette_then(PALETTE_ID_GAME, MS_WAVE_OVER);
            return true;
        }
    }
    run_frame(game);                                /* 0616 */
    return false;
}

/* ---- The wave's outcome (0633..07b4) ------------------------------------------------- */

static bool wave_over(GameState *game)
{
    switch (game->wave_result) {
    case WAVE_WARP:                                 /* 063a */
        game->tick_hook = TICK_HOOK_NONE;
        wave_draw_warp_banner(game);                /* 0640 */
        game_sound_play(SAMPLE_ID_WARP, 0, 0x0A);
        go(MS_WARP_SOUND);
        return false;                               /* 0658 wait_vsync */
    case WAVE_ABORTED:
        go(MS_ATTRACT);                             /* 0683 */
        return true;
    case WAVE_PLAYER_DEAD:
        go(MS_PLAYER_DIED);
        return true;
    default:
        flow.frames_left = WAVE_CLEARED_FRAMES;     /* 0690: the play tick runs on */
        game->tick_hook = TICK_HOOK_PLAY;
        go(MS_WAVE_CLEARED);
        return true;
    }
}

/* 065d: one more retrace, then until the warp sound has played; the warp gate takes the
 * player five levels on, with a bonus. */
static bool warp_sound(GameState *game)
{
    if (first_call() || game_sound_status().playing != 0)
        return false;
    game->player.level += WARP_LEVELS;
    score_add(game, SCORE_PLACE_THOUSANDS, WARP_BONUS_THOUSANDS);   /* 0671 (AX = 0x31) */
    go(MS_WAVE_NEXT);
    return true;
}

static bool wave_cleared(GameState *game)
{
    if (flow.frames_left-- > 0) {                   /* 0699 */
        run_frame(game);
        return false;
    }
    game->tick_hook = TICK_HOOK_NONE;               /* 06a2 */
    flash_cancel(game);
    set_palette_then(PALETTE_ID_GAME, MS_WAVE_NEXT);
    return true;
}

/* A demo ends after its waves; a game goes on through the warp box. */
static bool wave_next(GameState *game)
{
    if (game->demo_mode != 0 && --game->demo_mode == 0) {   /* 06b6 */
        go(MS_ATTRACT);
        return true;
    }
    flash_cancel(game);                             /* 06c6 */
    set_palette_then(PALETTE_ID_GAME, MS_WARP_BOX_START);
    return true;
}

static void box_reset(void)
{
    flow.box.x0 = WARP_BOX_X0;
    flow.box.y0 = WARP_BOX_Y0;
    flow.box.x1 = WARP_BOX_X1;
    flow.box.y1 = WARP_BOX_Y1;
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

/* 06e8: a rectangle that grows from the middle to the screen's edge, one step per frame,
 * cycling through four colours. */
static bool warp_box(GameState *game)
{
    (void)game;
    box_draw(asset_levels()->warp_box_colours[flow.box.colour] & 0x0F);
    box_grow();
    flow.box.colour = (flow.box.colour + 1) % WARP_BOX_COLOUR_COUNT;
    if (flow.box.x0 >= 0)
        return false;
    box_reset();
    go(MS_WARP_BOX_CLEAR);
    return false;                                   /* 072f wait_vsync */
}

/* 072d: the same rectangle again in colour 0, which wipes the screen. */
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

/* A life lost. With none left the continue prompt follows, except in a shared start while
 * the other player still has lives. */
static bool player_died(GameState *game)
{
    if (--game->player.lives == 0) {                /* 075c */
        if (!(game->options & OPT_SHARED_START)) {
            go(MS_CONTINUE_PROMPT);
            return true;
        }
        player_swap_state(game);
        if (game->player.lives == 0) {
            go(MS_CONTINUE_PROMPT);
            return true;
        }
        player_swap_state(game);
    }
    flash_cancel(game);                             /* 0776 */
    set_palette_then(PALETTE_ID_GAME, MS_RESPAWN);
    return true;
}

/* The wave starts again, its roster rebuilt from the enemies still alive. In a shared
 * start the players take turns; the second player's first turn starts the level afresh. */
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

/* ---- Continue and game over (07c7..097d) --------------------------------------------- */

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

/* Fire continues with full lives for both players; the countdown's end is game over. */
static bool continue_wait(GameState *game)
{
    if (after_frame_run()) {
        if (game->continue_ticks < 0) {             /* 07f3 -> 0807: fire pressed */
            game->player.lives = CONTINUE_LIVES;
            game->player_other.lives = CONTINUE_LIVES;
            flash_cancel(game);
            set_palette_then(PALETTE_ID_GAME, MS_RESPAWN);
            return true;
        }
        if (game->continue_seconds < 0) {           /* 07fa */
            draw_show_background();                 /* 0801 */
            go(MS_GAME_OVER);
            return true;
        }
    }
    run_frame(game);                                /* 07ee */
    return false;
}

static bool game_over(GameState *game)
{
    (void)game;
    game_over_draw();                               /* 08ee..0910 */
    game_sound_play(SAMPLE_ID_GAME_OVER, 1, 8);
    go(MS_GAME_OVER_SOUND);
    return false;                                   /* 091f wait_vsync */
}

/* 0924: one more retrace, then until the sound has played. */
static bool game_over_sound(GameState *game)
{
    (void)game;
    if (first_call() || game_sound_status().playing != 0)
        return false;
    go(MS_HISCORE_START);
    return true;
}

/* ---- The hi-score table (0933..097d, 5a8e..5e89, 5c3c..5d5c) -------------------------- */

static bool hiscore_start(GameState *game)
{
    game->hiscore_mark[0] = -1;                     /* 0933 */
    game->hiscore_mark[1] = -1;
    flow.second_check = false;
    go(MS_HISCORE_CHECK);
    return true;
}

/* After a player's check (and name entry): in a shared start the other player's score is
 * checked too, then the table is shown. */
static bool hiscore_checked(GameState *game)
{
    if (!flow.second_check && (game->options & OPT_SHARED_START)) {     /* 0942 */
        flow.second_check = true;
        player_swap_state(game);
        go(MS_HISCORE_CHECK);
        return true;
    }
    go(MS_GAME_OVER_TABLE);
    return true;
}

/* 093f / 094c: rank the current player's score; a new entry gets a name. */
static bool hiscore_check(GameState *game)
{
    flow.rank = hiscore_check_insert(game);
    if (flow.rank != HISCORE_NO_RANK) {
        go(MS_NAME_ENTRY_SCREEN);                   /* 5e47 */
        return true;
    }
    return hiscore_checked(game);
}

static bool name_entry_screen(GameState *game)
{
    name_entry_begin(&flow.name, &game->text, game->options, hiscore_art());   /* 59fa */
    reveal_then(MS_NAME_ENTRY_START);               /* 5a85 */
    return true;
}

static bool name_entry_start(GameState *game)
{
    flow.saved_draw_hook = game->draw_hook;         /* 5a8e */
    flow.saved_tick_hook = game->tick_hook;
    game->draw_hook = DRAW_HOOK_NAME_ENTRY;
    game->tick_hook = TICK_HOOK_NAME_ENTRY;
    game->tick_count = 0;                           /* 5aa8 */
    go(MS_NAME_ENTRY);
    return true;
}

/* 5e89 hiscore_save: write the table (DS:40A0) to scores.yak; here the host stores the
 * bytes. Nothing is saved without a host hook (replays, tests). */
static void save_hiscores(const GameState *game)
{
    if (score_saver.save == NULL)
        return;
    HighScoreTable table = { .count = HISCORE_COUNT };
    for (int i = 0; i < HISCORE_COUNT; i++)
        table.entries[i] = game->hiscores[i];
    uint8_t yak[SCORES_YAK_SIZE];
    if (scores_format_yak(&table, yak))
        score_saver.save(yak, SCORES_YAK_SIZE, score_saver.user);
}

/* The player spells the name with the stick (the entry's tick hook) until the time runs
 * out; then the name goes into the table and the table is saved. */
static bool name_entry(GameState *game)
{
    if (!after_frame_run() || !name_entry_done(game->tick_count)) {    /* 5ab3 */
        run_frame(game);                            /* 5aae */
        return false;
    }
    game->tick_hook = flow.saved_tick_hook;         /* 5abb */
    game->draw_hook = flow.saved_draw_hook;
    const HiscoreArt *art = hiscore_art();          /* 5e4b..5e85 */
    hiscore_set_name(&game->hiscores[flow.rank], flow.name.name, art->data->rude_word,
                     &game->rude_name);
    save_hiscores(game);                            /* 5e89 */
    return hiscore_checked(game);
}

void main_sm_name_entry_tick(GameState *game)      /* tick hook 5b06 */
{
    NameEntrySound sound = name_entry_tick(&flow.name, input_state()->controls[0],
                                           &game->tick_count, hiscore_art());
    if (sound.sample != 0)
        game_sound_play(sound.sample, sound.rate_index, sound.priority);
}

void main_sm_name_entry_draw(GameState *game, const DrawTarget *frame)   /* 5ac4 */
{
    name_entry_draw(&flow.name, frame, &game->text, hiscore_art());
}

static bool game_over_table(GameState *game)
{
    scroller_start_on_screen(&flow.scroller, &game->text);   /* 094f..095e */
    go(MS_TABLE_SHOW);
    return true;
}

/* hiscore_table_show (5c3c): the title tune and the table; after it, attract mode. */
static bool table_show(GameState *game)
{
    title_tune_begin(game);                         /* 5c3f */
    hiscore_table_draw(&flow.scroller, &game->text, game->hiscores, game->hiscore_mark,
                       hiscore_art());
    flow.rows_left = HISCORE_TABLE_SCROLL_ROWS;
    go(MS_TABLE_SCROLL);
    return true;
}

/* 5d19: one row per frame. A skip leaves at once (5d2a); after the last row the loop's
 * wait_vsync (5d33) still comes first. */
static bool table_scroll(GameState *game)
{
    (void)game;
    bool skip = scroller_input().skip;
    if (!hiscore_table_scroll_frame(&flow.scroller, &flow.rows_left, skip))
        return false;                               /* 5d33 wait_vsync */
    go(MS_TABLE_END);
    return skip;
}

static bool table_end(GameState *game)
{
    game->tune.fire_pressed = false;                /* 5d3b */
    game->tick_count = 0;
    go(MS_TABLE_WAIT);
    return true;
}

/* 5d47: until fire (seen by the tune's tick hook) or 20 seconds. */
static bool table_wait(GameState *game)
{
    if (!game->tune.fire_pressed && (int16_t)game->tick_count < TABLE_WAIT_TICKS)
        return false;
    game->tick_hook = TICK_HOOK_NONE;               /* 5d56 */
    scroller_finish(&flow.scroller);                /* 0967..097a / 594a..595d */
    go(MS_ATTRACT);
    return true;
}

/* ---- The shared waits ----------------------------------------------------------------- */

static bool palette(GameState *game)
{
    (void)game;
    if (first_call())
        return false;                               /* palette_set's wait_vsync */
    gfx_set_game_palette(asset_palette(flow.palette));
    go(flow.after);
    return true;
}

/* screen_reveal_bg: black, three more retraces, the clean background into both pages, the
 * game palette. With palette_set's own waits that is five frames. 61ff */
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
    case MS_TITLE_LLAMAS:           return title_llamas_start(game);
    case MS_TITLE:                  return title(game);
    case MS_ATTRACT_TEXT:           return attract_text(game);
    case MS_NEW_GAME:               return new_game(game);
    case MS_NEXT_LEVEL:             return next_level(game);
    case MS_WAVE_START:             return wave_start(game);
    case MS_WAVE_BEGIN_PLAY:        return wave_begin_play(game);
    case MS_WAVE_PLAY:              return wave_play(game);
    case MS_WAVE_OVER:              return wave_over(game);
    case MS_WARP_SOUND:             return warp_sound(game);
    case MS_WAVE_CLEARED:           return wave_cleared(game);
    case MS_WAVE_NEXT:              return wave_next(game);
    case MS_WARP_BOX_START:         return warp_box_start(game);
    case MS_WARP_BOX:               return warp_box(game);
    case MS_WARP_BOX_CLEAR:         return warp_box_clear(game);
    case MS_PLAYER_DIED:            return player_died(game);
    case MS_RESPAWN:                return respawn(game);
    case MS_CONTINUE_PROMPT:        return continue_prompt(game);
    case MS_CONTINUE_SHOW:          return continue_show(game);
    case MS_CONTINUE_WAIT:          return continue_wait(game);
    case MS_GAME_OVER:              return game_over(game);
    case MS_GAME_OVER_SOUND:        return game_over_sound(game);
    case MS_HISCORE_START:          return hiscore_start(game);
    case MS_HISCORE_CHECK:          return hiscore_check(game);
    case MS_NAME_ENTRY_SCREEN:      return name_entry_screen(game);
    case MS_NAME_ENTRY_START:       return name_entry_start(game);
    case MS_NAME_ENTRY:             return name_entry(game);
    case MS_GAME_OVER_TABLE:        return game_over_table(game);
    case MS_TABLE_SHOW:             return table_show(game);
    case MS_TABLE_SCROLL:           return table_scroll(game);
    case MS_TABLE_END:              return table_end(game);
    case MS_TABLE_WAIT:             return table_wait(game);
    case MS_PALETTE:                return palette(game);
    case MS_REVEAL:                 return reveal(game);
    case MS_QUIT:                   return false;
    }
    return false;
}

void main_sm_start(GameState *game, const GameLaunch *launch)
{
    (void)game;
    flow = (MainFlow){ .state = MS_BOOT };
    score_saver = launch != NULL ? (ScoreSaver){ launch->save_scores, launch->host_user }
                                 : (ScoreSaver){ NULL, NULL };
}

void main_sm_enter(GameState *game, MainSmTestState state)
{
    (void)game;
    flow = (MainFlow){ .state = MS_BOOT };          /* the host's score saver stays */
    if (state == MAIN_SM_TEST_GAME_OVER)
        go(MS_GAME_OVER);
}

bool main_sm_step(GameState *game)
{
    return step(game);
}

MainSmTestState main_sm_state(void)
{
    switch (flow.state) {
    case MS_GAME_OVER:          return MAIN_SM_TEST_GAME_OVER;
    case MS_NAME_ENTRY_SCREEN:  return MAIN_SM_TEST_NAME_ENTRY;
    case MS_GAME_OVER_TABLE:    return MAIN_SM_TEST_GAME_OVER_TABLE;
    default:                    return MAIN_SM_TEST_OTHER;
    }
}

void main_sm_frame(GameState *game)
{
    if (flow.extra_wait) {          /* frame_run's palette reload: one more retrace */
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
