/*
 * state.h - the game's state as ordinary C (docs/PLAN.md §0.1).
 *
 * Shared header (PLAN.md §7): changes go through the integration lead and get a line in
 * docs/CHANGELOG-shared.md.
 *
 * The structs are designed for reading, not for the original's byte layout; the mapping to
 * DGROUP lives only in tests/adapters. Comments give the original location (DS offsets in
 * game86's DGROUP 3559, record offsets as +xx) for traceability, from
 * docs/re-notes-game86.md §8. [H] marks meanings that are still hypotheses; fields whose
 * meaning is unknown keep a neutral name and the address, and get renamed once understood.
 */
#ifndef LLAMATRON_GAME_STATE_H
#define LLAMATRON_GAME_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "formats.h"   /* HighScore (src/assets) */
#include "rng.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ENTITY_COUNT = 70,          /* DS:48C1, 0x46 records */
    PLAYER_COUNT = 2,
    SHOT_COUNT = 15,            /* player shots, DS:68C9 */
    TRAIL_LENGTH = 128,         /* position history per player, DS:6CC9 / DS:70C9 */
    ROSTER_SIZE = 50,           /* DS:220A, 50 slots cleared; 31 searched (1000:1ced) */
    SCORE_DIGITS = 8,           /* scores are kept modulo 10^8 */
    HISCORE_COUNT = 10,
    HISCORE_NAME_LENGTH = 8,
    LEVEL_COUNT = 100,          /* tbl_level_scripts DS:2BDA..2CA1 */
};

/* --------------------------------------------------------------------------------------
 * Entities (64-byte records at DS:48C1; g_companion at DS:4881 uses the same layout)
 * ------------------------------------------------------------------------------------ */

/* Free slots have think == ENTITY_FREE (the original writes 0xFFFF to the whole type word,
 * game86 1000:4307). */
#define ENTITY_FREE (-1)
/* Animate handler index of an entity that only stands there (hook_nop), game86 1000:3d61. */
#define ANIM_IDLE 0x12

typedef struct Entity {
    /* +00 type word. The original tests the sign of the whole word in the think/animate/alloc
     * loops (i.e. hatch_into < 0) but the low byte in wave_spawn_enemies (1000:1d0a), whose
     * port tests think < 0 explicitly; both bytes are 0xFF for a free slot. */
    int8_t think;               /* +00 lo: tbl_think_handlers index (46 handlers) */
    int8_t hatch_into;          /* +00 hi: think index it becomes when a spawn hatches (think_00) */
    int16_t x, y;               /* +02/+04 sprite centre, screen pixels */
    int16_t anim;               /* +06 tbl_anim_handlers index (19 handlers; low byte used) */
    int16_t vx, vy;             /* +08/+0a velocity (entity_move_bounce) */
    int16_t sprite;             /* +0c sprite bank index; < 0 = use sprite_ref */
    int16_t scale;              /* +0e draw scale / hatch countdown */
    int16_t solid;              /* +10 shots and players collide only when non-zero */
    int16_t aux12;              /* +12 handler-specific */
    int16_t timer;              /* +14 general timer; pickup kind for think_1c */
    int16_t aux16;              /* +16 handler-specific (projectile parameter) */
    int16_t hp;                 /* +18 hit points; < 0 after a hit -> kill action (ent_hit_03) */
    int16_t aux1a;              /* +1a handler-specific (projectile parameter) */
    int16_t aux1c;              /* +1c handler-specific: callback id (3119/32d7) or accumulator low */
    int16_t kill_action;        /* +1e tbl_hit_kill_actions index (accumulator high for some types) */
    int16_t sub_mode;           /* +20 think_0e sub-mode */
    uint8_t reload, countdown;  /* +22/+23 think_25 */
    int16_t aux24, aux26;       /* +24/+26 handler-specific accumulators */
    int16_t anim_toggle;        /* +28 anim_01 */
    int16_t aux2a;              /* +2a */
    int16_t sprite_ref;         /* +2c explicit sprite when sprite < 0 */
    int16_t aux2e, aux30;       /* +2e/+30 */
    /* +32..+38 overlay sprite drawn after the animate handler for `frames` frames
     * (game86 1000:4392..43be). x == 0 means no overlay. */
    struct {
        int16_t x, y;           /* +32/+34 */
        int16_t sprite;         /* +36 sprite bank index */
        int16_t frames;         /* +38 frames left; overlay cleared when it reaches 0 */
    } overlay;
    int16_t kind;               /* +3a roster kind (Player.roster); < 0 = not counted */
    uint8_t death_sample;       /* +3c lo: sound sample requested on death (1000:0eb0) */
    uint8_t death_rate;         /* +3c hi: its rate index */
    int16_t hit_handler;        /* +3e tbl_hit_handlers index (CS:0C04, 18 handlers) */
} Entity;

static inline bool entity_is_free(const Entity *e) { return e->hatch_into < 0; }

/* --------------------------------------------------------------------------------------
 * Player progress block (200 bytes at DS:21A6 = current player, DS:226E = the other one;
 * swapped wholesale by player_swap_state 1000:5317, copied by player_copy_state 1000:5336).
 * DS:21CC..2209 is never referenced and has no field here (the adapter writes zeros).
 * ------------------------------------------------------------------------------------ */

/* One entry of the per-wave roster (DS:220A): which kinds of enemy are left in the wave,
 * so a player resumes their own wave after the other player's turn (1000:07b1 -> 045d). */
typedef struct RosterEntry {
    int8_t kind;                /* entity kind; < 0 = unused (and terminates the search) */
    uint8_t count;              /* [H] live entities of that kind minus one (1000:1d38 stores 0
                                   for the first) */
} RosterEntry;

typedef struct Player {
    int16_t lives;              /* 21A6, 5 at a new game */
    int16_t wave;               /* 21A8 wave within the level */
    int16_t level;              /* 21AA 0..LEVEL_COUNT-1; past the last script it wraps to 0
                                   and loop_count goes up */
    int16_t enemy_speed_x;      /* 21AC script op 0 */
    int16_t enemy_speed_y;      /* 21AE */
    int16_t wave_type;          /* 21B0 first byte of the current wave record */
    int16_t swarm_reaim_period; /* 21B2 think_03's re-aim period: max(swarm_count / 2, 3) (1a28) */
    /* 21B4 the multi-shot power-up level (pickup 04, 1000:0fdb, sets 1 for the rest of the
     * wave): player_fire adds the 3-way spread while it is non-zero (0a00/0b26), and
     * score_add awards every increment multi_shot+1 times. */
    int16_t multi_shot;
    int16_t script_param[7];    /* 21B6..21C2 per-level parameters set by script ops 1..2 [H] */
    int16_t powerup;            /* 21C4 [H] power-up state, sign tested by anim_10 */
    int16_t pickup_pending;     /* 21C6 k+1: the next killed enemy becomes pickup kind k (0eb8);
                                   score_add's extra life sets 3 */
    int16_t bonus_countdown;    /* 21C8 -1 per 200 kills; at 0 back to 2 and, below level 90,
                                   pickup 1 is queued (2 at a new game) */
    int16_t param_21ca;         /* 21CA */
    RosterEntry roster[ROSTER_SIZE]; /* 220A */
} Player;

/* --------------------------------------------------------------------------------------
 * Player ships, shots and trails (outside the swapped block)
 * ------------------------------------------------------------------------------------ */

typedef struct Point16 {
    int16_t x, y;
} Point16;

/* The per-tick hook [0x39E4] and the per-frame draw hook [0x39E0]: the original stores code
 * addresses, the port names them. */
typedef enum TickHook {
    TICK_HOOK_NONE,             /* 3fde hook_nop */
    TICK_HOOK_TITLE_TUNE,       /* 084e title_tune_tick */
    TICK_HOOK_TITLE,            /* 3f6f title_tick_hook */
    TICK_HOOK_PLAY,             /* 1562 play_tick_hook */
    TICK_HOOK_PAUSE,            /* 14d0 pause_tick_hook */
    TICK_HOOK_CONTINUE,         /* 0816 continue_tick_hook */
    TICK_HOOK_NAME_ENTRY,       /* 5b06 hi-score name entry */
} TickHook;

typedef enum DrawHook {
    DRAW_HOOK_NONE,             /* 0000: nothing installed yet (image value) */
    DRAW_HOOK_TITLE,            /* 402c title_draw_hook */
    DRAW_HOOK_PLAY,             /* 1a3b play_draw_hook */
    DRAW_HOOK_CONTINUE,         /* 0894 continue_draw_hook */
    DRAW_HOOK_NAME_ENTRY,       /* 5ac4 hi-score name entry */
} DrawHook;

/* The title tune (1000:083b/084e): a list of notes played one per `period` ticks. */
typedef struct TitleTune {
    bool fire_pressed;          /* 0625 set by title_tune_tick while controls[0] fire is down;
                                   read at 599a/5d47, cleared at 083b/597b/5d3b (symbols:
                                   g_tune_active) */
    int16_t period;             /* 0627 */
    int16_t countdown;          /* 0629 -1 = silent */
    uint16_t position;          /* 062B next note: byte offset into the tune list */
    uint16_t restart;           /* 062D where the list starts over: byte offset */
    /* both TUNE_NO_LIST (the image's null pointers) until title_tune_begin */
} TitleTune;

enum { TUNE_NO_LIST = 0xFFFF };

enum { TITLE_LETTERS = 10 };

enum { PAUSE_MESSAGES = 4 };    /* the pointers at DS:09E2 */

/* Per-ship steering and aim, written by the play tick (1000:16c1..1a00) and player_fire. */
typedef struct ShipControl {
    Point16 move;               /* 06BE/06C0 P1, 06B0/06B2 P2: this tick's ship velocity */
    Point16 shot_velocity;      /* 069C/069E P1, 06A0/06A2 P2 */
    int16_t shot_direction;     /* 06AC P1, 06AE P2 */
    int16_t aim;                /* 06B6 P1, 06B8 P2: direction nibble of the last aim; indexes
                                   player_fire's spread tables */
    int16_t facing;             /* 06C2 P1, 07F9 P2: ship sprite (drawn at 1b5d) */
    int16_t trail_flag;         /* 06B4 P2; for P1 the original uses a local (1933..198a), so
                                   ship_control[0].trail_flag is port-only scratch, not mapped */
} ShipControl;

/* Ship position limits (clamped by the play tick): x in [0x0F, 0x130), y in [0x12, 0xBC). */
enum { SHIP_X_MIN = 0x0F, SHIP_X_END = 0x130, SHIP_Y_MIN = 0x12, SHIP_Y_END = 0xBC };

typedef struct Shot {           /* 16-byte records at DS:68C9 */
    int16_t x;                  /* +0 < 0 = free */
    int16_t y;                  /* +2 */
    int16_t vx, vy;             /* +4/+6 */
    int16_t direction;          /* +8 sprite / direction index */
    int16_t aux0a;              /* +a */
    int16_t bounce_life;        /* +c [H] lifetime when shots bounce */
    int16_t aux0e;              /* +e */
} Shot;

typedef struct TrailPoint {     /* 8-byte records, DS:6CC9 (P1) / DS:70C9 (P2) [H] */
    int16_t x, y;
    int16_t flag;
    int16_t aux;
} TrailPoint;

/* --------------------------------------------------------------------------------------
 * Whole game
 * ------------------------------------------------------------------------------------ */

typedef enum WaveResult {       /* DS:0810 */
    WAVE_RUNNING = 0,
    WAVE_PLAYER_DEAD = 1,
    WAVE_CLEARED = 2,
    WAVE_ABORTED = 3,           /* 'X' key */
    WAVE_WARP = 4,              /* +5 levels */
} WaveResult;

typedef enum GameOptions {      /* DS:2CF6 bits */
    OPT_TWO_PLAYERS = 0x01,
    OPT_DOUBLE_CONTROLLER = 0x02,
    OPT_SHARED_START = 0x04,    /* player_copy_state copies P1's block to P2 */
    OPT_CURRENT_IS_P2 = 0x10,   /* toggled by player_swap_state */
} GameOptions;

/* One high-score row (scores.yak / DS:40A0 is 8 ASCII digits + 8 name bytes); the same type
 * the extractor's score table uses, so loading and saving copy rows unchanged. */
typedef HighScore HiscoreEntry;

/* Where wave spawns are read from (DS:39EB points into one of them): the level's own
 * (type, count) list, or the player's roster when a player resumes a wave (1000:07b1). Both
 * hold count = entities - 1 and end with a count of 0xFF (1000:3d0d). */
/* SPAWN_NONE: the original's null cursor before the first level starts. */
typedef enum SpawnSource { SPAWN_NONE, SPAWN_FROM_LEVEL, SPAWN_FROM_ROSTER } SpawnSource;

typedef struct SpawnCursor {
    SpawnSource source;
    int16_t level;              /* level whose list is read (SPAWN_FROM_LEVEL) */
    uint16_t index;             /* next (type, count) pair */
} SpawnCursor;

/* Per-level enemy parameters set by level-script ops 3..7 (1000:0392..03e1), each read by
 * one enemy handler; named after their address until W5 knows their meaning. */
typedef struct LevelTuning {
    int16_t init01_mode;        /* 0812 op 5: enemy_init_01 sub_mode */
    int16_t init0d_hp;          /* 0816 op 4: enemy_init_0d hit points */
    int16_t segment_family;     /* 0818 op 3: enemy_init_11 family, ent_hit_0e +2e */
    int16_t orbit_phase_step;   /* 0820 op 7: enemy_init_15 sine phase step (0x40 until set) */
    int16_t p0826;              /* 0826 op 3 */
    uint32_t accum_start;       /* 0828/082A op 6: enemy_init_22..25 accumulators */
    int16_t p082c;              /* 082C op 6: copied to +2e */
    int16_t p082e;              /* 082E op 6: copied to +24 */
    int16_t p0830;              /* 0830 op 5 */
    int16_t init19_vx;          /* 086E op 4: enemy_init_19 vx */
} LevelTuning;

enum { SNAKE_TRAIL_LENGTH = 64 };

typedef struct SnakeTrailPoint {    /* DS:4481, 8 bytes each */
    int16_t x, y;
    int16_t sprite;
    int16_t unused;
} SnakeTrailPoint;

typedef struct GameState {
    /* Configuration from the launcher's command tail (game86 1000:5f6b) */
    bool vga;                           /* 058A */
    bool joystick;                      /* 058B (058D keyboard = !joystick) */
    bool sound_present;                 /* 0689 a sound driver was loaded (609c) */
    bool sound_on;                      /* 0688 toggled at runtime by 1000:1514 */
    uint8_t flash_mode;                 /* 0589 0 none, 1 pulsing, 2 pulsing and flashes */
    bool quit;                          /* leave the game (exit path 1000:5ed9) */

    Rng rng;                            /* DS:6AC9 table, 39E9 position, 0453 mixer */

    uint8_t options;                    /* 2CF6 GameOptions */
    Player player;                      /* 21A6 the player whose turn it is */
    Player player_other;                /* 226E */
    Point16 ship[PLAYER_COUNT];         /* 2CFB/2CFD P1, 2CFF/2D01 P2 */
    Point16 target;                     /* 2D03/2D05 ship the current entity aims at */
    /* 4441/4449, there 8 decimal digits, one per byte, most significant first. Kept as a number
     * modulo 10^8: score_add's digit carries are plain addition (score.h says when its extra
     * life fires), the high-score compare is digit-wise (= numeric) and the HUD converts to
     * digits when drawing. */
    uint32_t score[PLAYER_COUNT];
    bool hud_dirty;                     /* 05A1 redraw the HUD this frame (tested at 1000:0309) */
    int16_t continues;                  /* 061F 4 per game */
    int16_t respawn_timer[PLAYER_COUNT];/* 07F3 / 0634 [H] 0x64 at wave start, P1 0xFFFE = dying */
    int16_t loop_count;                 /* 233C times the level scripts wrapped to level 0 */
    int16_t level_flags;                /* 233E [H] sign selects shot sprites */
    int16_t speedup_timer;              /* 2336 -1 per play tick (15cc); below 0 it reloads from
                                           speedup_period (halved while > 0x10) and speed_accum
                                           grows by 0x200. A shot hit reloads it (0d82) */
    int16_t speedup_period;             /* 2338 [H] reload value; no code writes it */
    int16_t demo_mode;                  /* 06C4 demo waves left: 3 when a demo starts (02b9), -1 per
                                           wave (06bd); non-zero disables score_add */

    Entity companion;                   /* 4881 */
    Entity entities[ENTITY_COUNT];      /* 48C1 */
    int16_t free_slots;                 /* 0927 0x45 at wave start */
    int16_t enemies_alive;              /* 062F the wave ends at 0 */
    int16_t bounced;                    /* 0925 set by entity_move_bounce */

    Shot shots[SHOT_COUNT];             /* 68C9 */
    bool shots_bounce;                  /* 0650 power-up: shots reflect at the edges */
    TrailPoint trail[PLAYER_COUNT][TRAIL_LENGTH]; /* 6CC9 / 70C9 */
    /* 2CF7/2CF9 newest TrailPoint of each ship (the original keeps index * 8, always masked
     * to the ring by the play tick 1000:1975..19ce) */
    int16_t trail_head[PLAYER_COUNT];
    /* 06BA/06BC how many trail entries a rescued llama follows behind (+5 per rescue, think_05
     * 4d8c/4d22). [H] The play tick reads head + lag unmasked (1000:194f), i.e. past the ring
     * into the next one: tick.c reproduces that. */
    int16_t follower_lag[PLAYER_COUNT];
    bool rescue_marker_pending;         /* 091F last llama rescued (4cfe), cleared at 4da1 */
    int16_t rescue_marker_y[PLAYER_COUNT]; /* 0921/0923 marker above the ship (1aed/1bbc), -1 per
                                              tick while >= 0 (15ea) */
    /* The controller bytes 07FC/07FB are the engine's input_state()->controls[0]/[1]. */
    ShipControl ship_control[PLAYER_COUNT];
    uint8_t fire_delay;                 /* 0631 ticks until the next volley (reload 8) */
    int16_t fire_turn;                  /* 0632 XOR 1 per volley in two-player mode: which ship
                                           fires the spread */
    bool started_with_stick_b;          /* 09D3 the second stick's fire started the game
                                           (title 1000:015b/016a); P1 then moves with
                                           controls[0] and aims with controls[1] (188d), and
                                           input_frame_read reads both sticks (1e7b:0065) */
    /* The droid: P2 in two-player single-controller mode, flown by companion_update (480e). */
    int16_t droid_aim_timer;            /* 0682 0x14-tick period of enemy_aim_update */
    uint8_t droid_aim_votes[4];         /* 409C..409F solid enemies per quadrant around P2 */
    int16_t droid_quarry;               /* 0919 slot of the llama it flies to; -1 search, -2 none
                                           (fly to the companion) */
    Point16 chain_tail;                 /* 06A4/06A6 P1 trail entry at head + follower_lag[0] */
    Point16 demo_wander;                /* 06A8/06AA random spot, re-rolled every 64 ticks in
                                           demo mode */

    WaveResult wave_result;             /* 0810 */
    int16_t wave_intro_timer;           /* 08FC 0x32 ticks of freeze at wave start */
    LevelTuning tuning;
    SpawnCursor spawn;                  /* 39EB; the byte address mapping lives in tests/adapters */
    int16_t spawn_remaining;            /* 08FA entities of the current pair still to spawn, -1 none */
    int16_t spawn_type;                 /* 0900 enemy type (tbl_enemy_init index) being spawned */
    bool spawning_done;                 /* 0802 the whole list is out: player fire (0981) and the
                                           wave-cleared check (1665) wait for it */
    int16_t unhatched_left;             /* 08F8 [H] unhatched when the list ended, -1 at 4cb3 */
    bool spawns_clobbered_10b0;         /* a think_13 drop has zeroed LevelSet.clobber_10b0's
                                           spawn pairs (1000:123c via 10b0); never reset, as the
                                           original never reloads its script data */

    int16_t shield_timer;               /* 0907 invulnerability ticks (500 from pickup 0fa2) */
    bool smart_bomb;                    /* 0615 hit every solid entity (pickup 0fb5, cleared 0d27) */
    int16_t death_sound_timer;          /* 0872 0x14 when < 10 enemies are left (43f7); sample 0x11 at 0 */
    int16_t swarm_count;                /* 091D think_03 entities this tick (reset 1a1f) */
    const FlashList *flash_list;        /* 06C7 pending screen-flash list, NULL = none */

    /* Wave-wide enemy state (W5 handlers; [H] names are proposals) */
    int16_t spawn_sprite;               /* 0806 from tbl_spawn_kind (0808) by level & 3 or random (1daa) */
    int16_t spawn_count_0804;           /* 0804 [H] enemy_init_00 counts up, no reader found */
    int16_t unhatched;                  /* 091B [H] enemy_init_00 counts up, think handlers read */
    int16_t spawn_phase;                /* 0814 enemy_init_01: timer = value & 0x1f */
    uint32_t orbit_angle;               /* 081A/081C 0x20000 from enemy_init_15, think_21 adds 0x40 */
    int16_t orbit_next_phase;           /* 081E enemy_init_15 sine phase (low byte) */
    int16_t segment_sprite;             /* 0824 enemy_init_11..14/1f, read by ent_hit_0e */
    uint32_t speed_accum;               /* 0636/0638 [H] 0x800 at start, the play tick adds 0x200 (15df) */
    int16_t kills_toward_bonus;         /* 063A ent_hit_00 counts; at 200 resets and a pickup drops */
    int16_t powerup_0652;               /* 0652 [H] 1 from pickup 03 (with level_flags = 0x32) */
    int16_t debris_alt;                 /* 0876 [H] 0 from kill actions 01/02/09, 1 from 04 */
    SnakeTrailPoint snake_trail[SNAKE_TRAIL_LENGTH]; /* 4481 enemy_init_10 fills it with the head */
    int16_t snake_head;                 /* 0870 newest snake_trail entry, 0..63 (think_1d 36e1) */
    uint16_t tick_count;                /* 39E6 50 Hz counter */
    TickHook tick_hook;                 /* 39E4 what each timer tick runs */
    /* Wave and game bookkeeping of the top-level flow (main_sm.c, 1000:00db..0980). */
    int16_t game_started;               /* 061D 0 at a new game, 1 after the first wave start
                                           (045d), 2 once both players had a turn (07a8) */
    int16_t wave_start_delay;           /* 068A 0x32 at a new game / respawn; the play tick
                                           counts it down (156a) and the draw hook tests it (1a40) */
    int16_t player_label_offset;        /* 07F5 0x0C, or 0x5E for P2 in shared-start mode (0471);
                                           a draw offset of play_draw_hook (1aab) */
    int16_t spawn_delay;                /* 07FE 7 at wave start; the spawn tick (1d40) counts it
                                           down, wave_spawn_enemies sets -1 (1cfe) */
    int16_t play_flag_0617;             /* 0617 [H] -1 at wave start (05f3), 1 from the play tick
                                           (1a16); no reader known */
    int16_t level_default;              /* 059F level the attract mode starts with (00ed), saved
                                           before a demo picks a random one (028a) */
    bool pulse_enable;                  /* 0902 the timer may flash colour 1 (3e55); set at 00d5 */
    DrawHook draw_hook;                 /* 39E0 what each frame draws */
    bool frame_drawn;                   /* 0429 the tick hook idles until a wave frame is drawn */

    HiscoreEntry hiscores[HISCORE_COUNT]; /* 40A0 */
    /* 09DE/09E0 table row of each player's new entry, -1 = none: set by hiscore_insert
     * (1000:5da2, which also shifts the other mark), reset at 1000:0933, drawn by 5c3c. */
    int16_t hiscore_mark[PLAYER_COUNT];
    bool rude_name;                     /* 0874 set by 1000:5e6d; 1000:160d then skips sample 0x11 */

    /* Title screen and attract mode (attract.c, W5b; the loop is main_sm's 00db..0306). The
     * 32 bouncing title llamas, which the original keeps in the entity table's bytes, are
     * private to attract.c. */
    TitleTune tune;
    uint8_t options_default;            /* 059E GameOptions the next game starts with: read at
                                           00e7, written at 02b1 and by title_draw_hook (40de) */
    int16_t title_objects[TITLE_LETTERS]; /* 2104..2117 per-letter countdown: 0x80 + 0x10 * i at
                                             start, -2 per tick, -1 once landed */
    int16_t title_timer;                /* 0904 0x50 at start; at 0 the logo goes into all pages
                                           and it becomes -1 */
    uint8_t title_key_latch;            /* 0906 direction last taken in the option menu, so one
                                           press is one step */

    /* Continue prompt and pause screen (continue.c, pause.c, W5b). */
    int16_t continue_seconds;           /* 0621 9 at 07d6, -1 per second; the prompt ends < 0 */
    int16_t continue_ticks;             /* 0623 0x32 per second; -1 = fire pressed (continue) */
    bool sound_key_latch;               /* 0686 S toggles sound once per press (1507/150e/1538) */
    int16_t pause_message;              /* 09EC 0 = none, else 1..PAUSE_MESSAGES: the message
                                           picked on P (1595..15ae), drawn by play_draw_hook */
} GameState;

#ifdef __cplusplus
}
#endif

#endif
