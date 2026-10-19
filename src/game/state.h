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
    SCORE_DIGITS = 8,
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
    int16_t param_21b2;         /* 21B2 [H] */
    int16_t score_scale;        /* 21B4 score_add awards every increment score_scale+1 times */
    int16_t script_param[7];    /* 21B6..21C2 per-level parameters set by script ops 1..2 [H] */
    int16_t powerup;            /* 21C4 [H] power-up state, sign tested by anim_10 */
    int16_t extra_life_flag;    /* 21C6 [H] 3 on extra life */
    int16_t param_21c8;         /* 21C8 2 at a new game and in think_01 */
    int16_t param_21ca;         /* 21CA */
    RosterEntry roster[ROSTER_SIZE]; /* 220A */
} Player;

/* --------------------------------------------------------------------------------------
 * Player ships, shots and trails (outside the swapped block)
 * ------------------------------------------------------------------------------------ */

typedef struct Point16 {
    int16_t x, y;
} Point16;

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

typedef struct HiscoreEntry {   /* scores.yak record: 8 ASCII digits + 8 name chars */
    char digits[SCORE_DIGITS];
    char name[HISCORE_NAME_LENGTH];
} HiscoreEntry;

typedef struct GameState {
    /* Configuration from the launcher's command tail (game86 1000:5f6b) */
    bool vga;                           /* 058A */
    bool joystick;                      /* 058B (058D keyboard = !joystick) */
    bool sound;                         /* 0688/0689 driver present and on */
    uint8_t flash_mode;                 /* 0589 0 none, 1 pulsing, 2 pulsing and flashes */
    bool quit;                          /* leave the game (exit path 1000:5ed9) */

    Rng rng;                            /* DS:6AC9 table, 39E9 position, 0453 mixer */

    uint8_t options;                    /* 2CF6 GameOptions */
    Player player;                      /* 21A6 the player whose turn it is */
    Player player_other;                /* 226E */
    Point16 ship[PLAYER_COUNT];         /* 2CFB/2CFD P1, 2CFF/2D01 P2 */
    Point16 target;                     /* 2D03/2D05 ship the current entity aims at */
    uint8_t score[PLAYER_COUNT][SCORE_DIGITS]; /* 4441/4449 one decimal digit per byte, MSD first */
    int16_t continues;                  /* 061F 4 per game */
    int16_t respawn_timer[PLAYER_COUNT];/* 07F3 / 0634 [H] 0x64 at wave start, P1 0xFFFE = dying */
    int16_t loop_count;                 /* 233C times the level scripts wrapped to level 0 */
    int16_t level_flags;                /* 233E [H] sign selects shot sprites */
    bool demo_mode;                     /* 06C4 score_add disabled */

    Entity companion;                   /* 4881 */
    Entity entities[ENTITY_COUNT];      /* 48C1 */
    int16_t free_slots;                 /* 0927 0x45 at wave start */
    int16_t enemies_alive;              /* 062F the wave ends at 0 */
    int16_t bounced;                    /* 0925 set by entity_move_bounce */

    Shot shots[SHOT_COUNT];             /* 68C9 */
    bool shots_bounce;                  /* 0650 power-up: shots reflect at the edges */
    TrailPoint trail[PLAYER_COUNT][TRAIL_LENGTH]; /* 6CC9 / 70C9 */

    WaveResult wave_result;             /* 0810 */
    int16_t wave_intro_timer;           /* 08FC 0x32 ticks of freeze at wave start */
    /* 39EB level script position: op `script_op` of level `script_level` in the extracted
     * LevelSet (levels.dat); the byte address mapping lives in tests/adapters. */
    int16_t script_level;
    uint16_t script_op;
    uint16_t tick_count;                /* 39E6 50 Hz counter */
    bool frame_drawn;                   /* 0429 the tick hook idles until a wave frame is drawn */

    HiscoreEntry hiscores[HISCORE_COUNT]; /* 40A0 */
    /* 09DE/09E0 table row of each player's new entry, -1 = none: set by hiscore_insert
     * (1000:5da2, which also shifts the other mark), reset at 1000:0933, drawn by 5c3c. */
    int16_t hiscore_mark[PLAYER_COUNT];
    bool rude_name;                     /* 0874 set by 1000:5e6d; 1000:160d then skips sample 0x11 */
} GameState;

#ifdef __cplusplus
}
#endif

#endif
