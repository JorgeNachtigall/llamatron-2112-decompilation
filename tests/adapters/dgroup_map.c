/*
 * dgroup_map.c - GameState <-> game86 DGROUP bytes. See dgroup_map.h.
 *
 * Offsets follow the field comments in src/game/state.h (from docs/re-notes-game86.md §8);
 * widths were checked against the instructions that access them in the listing.
 */
#include "dgroup_map.h"

#include <stdio.h>
#include <string.h>

#include "game_data.h"

/* ---- DGROUP layout (DS offsets) ------------------------------------------------------ */

enum {
    DS_FLASH_MODE = 0x0589,     /* byte */
    DS_VGA = 0x058A,            /* byte */
    DS_JOYSTICK = 0x058B,       /* byte g_joystick_cfg */
    DS_KEYBOARD = 0x058D,       /* byte g_keyboard_mode = !joystick */
    DS_SOUND_PRESENT = 0x0689,  /* byte */
    DS_SOUND_ON = 0x0688,       /* byte, toggled at runtime by 1000:1514 */
    DS_RNG_MIXER = 0x0453,      /* 4 words */
    DS_RNG_TABLE = 0x6AC9,
    DS_RNG_POSITION = 0x39E9,
    DS_OPTIONS = 0x2CF6,        /* byte */
    DS_PLAYER = 0x21A6,         /* 200-byte block */
    DS_PLAYER_OTHER = 0x226E,
    DS_SHIPS = 0x2CFB,          /* P1 x,y, P2 x,y */
    DS_TARGET = 0x2D03,
    DS_SCORES = 0x4441,         /* 2 x 8 digit bytes */
    DS_HUD_DIRTY = 0x05A1,      /* word flag g_hud_dirty (tested at 1000:0309) */
    DS_CONTINUES = 0x061F,
    DS_RESPAWN_P1 = 0x07F3,
    DS_RESPAWN_P2 = 0x0634,
    DS_LOOP_COUNT = 0x233C,
    DS_LEVEL_FLAGS = 0x233E,
    DS_DEMO_MODE = 0x06C4,      /* word: demo waves left */
    DS_COMPANION = 0x4881,
    DS_ENTITIES = 0x48C1,
    DS_FREE_SLOTS = 0x0927,
    DS_ENEMIES_ALIVE = 0x062F,
    DS_BOUNCED = 0x0925,
    DS_SHOTS = 0x68C9,
    DS_SHOTS_BOUNCE = 0x0650,   /* word */
    DS_TRAIL_P1 = 0x6CC9,
    DS_TRAIL_P2 = 0x70C9,
    DS_WAVE_RESULT = 0x0810,
    DS_WAVE_INTRO_TIMER = 0x08FC,
    DS_TICK_COUNT = 0x39E6,
    DS_TICK_HOOK = 0x39E4,      /* code addresses: TICK_HOOKS / DRAW_HOOKS */
    DS_DRAW_HOOK = 0x39E0,
    DS_SPEEDUP_TIMER = 0x2336,
    DS_SPEEDUP_PERIOD = 0x2338,
    DS_GAME_STARTED = 0x061D,
    DS_WAVE_START_DELAY = 0x068A,
    DS_PLAYER_LABEL_OFFSET = 0x07F5,
    DS_SPAWN_DELAY = 0x07FE,
    DS_PLAY_FLAG_0617 = 0x0617,
    DS_LEVEL_DEFAULT = 0x059F,
    DS_PULSE_ENABLE = 0x0902,   /* word flag */
    DS_PULSE_INDEX = 0x0680,    /* EGA colour-1 pulse step, game86 1000:3ea1 */
    DS_FLASH_FRAME_TIMER = 0x044C,  /* byte */
    DS_PLAYER2_LABEL_OFFSET = 0x07F7,
    DS_LEVEL_VARIANT = 0x0941,
    DS_TEXT_PEN = 0x0B74,       /* x, y, margin; colour and mode live in CS (unmapped) */
    DS_FRAME_DRAWN = 0x0429,    /* byte */
    DS_SPAWN_REMAINING = 0x08FA,
    DS_SPAWN_TYPE = 0x0900,
    DS_SPAWNING_DONE = 0x0802,  /* word flag */
    DS_UNHATCHED_LEFT = 0x08F8,
    DS_SHIELD_TIMER = 0x0907,
    DS_SMART_BOMB = 0x0615,     /* word flag */
    DS_DEATH_SOUND_TIMER = 0x0872,
    DS_SWARM_COUNT = 0x091D,
    DS_TUNING_INIT01_MODE = 0x0812,
    DS_TUNING_INIT0D_HP = 0x0816,
    DS_TUNING_SEGMENT_FAMILY = 0x0818,
    DS_TUNING_ORBIT_PHASE_STEP = 0x0820,
    DS_TUNING_P0826 = 0x0826,
    DS_TUNING_ACCUM_START = 0x0828, /* dword, low word first */
    DS_TUNING_P082C = 0x082C,
    DS_TUNING_P082E = 0x082E,
    DS_TUNING_P0830 = 0x0830,
    DS_TUNING_INIT19_VX = 0x086E,
    DS_SPAWN_SPRITE = 0x0806,
    DS_SPAWN_COUNT_0804 = 0x0804,
    DS_UNHATCHED = 0x091B,
    DS_SPAWN_PHASE = 0x0814,
    DS_ORBIT_ANGLE = 0x081A,    /* dword */
    DS_ORBIT_NEXT_PHASE = 0x081E,
    DS_SEGMENT_SPRITE = 0x0824,
    DS_SPEED_ACCUM = 0x0636,    /* dword */
    DS_KILLS_TOWARD_BONUS = 0x063A,
    DS_POWERUP_0652 = 0x0652,
    DS_DEBRIS_ALT = 0x0876,
    DS_SNAKE_HEAD = 0x0870,
    DS_TUNE_FIRE_PRESSED = 0x0625,  /* word flag */
    DS_TUNE_PERIOD = 0x0627,
    DS_TUNE_COUNTDOWN = 0x0629,
    DS_TUNE_POSITION = 0x062B,  /* DS pointers into the tune list (set to 09D5 at 1000:5c42) */
    DS_TUNE_RESTART = 0x062D,
    TUNE_LIST = 0x09D5,
    DS_OPTIONS_DEFAULT = 0x059E,    /* byte */
    DS_TITLE_OBJECTS = 0x2104,      /* TITLE_LETTERS words */
    DS_TITLE_TIMER = 0x0904,
    DS_TITLE_KEY_LATCH = 0x0906,    /* byte */
    DS_FIRE_DELAY = 0x0631,     /* byte */
    DS_FIRE_TURN = 0x0632,
    DS_STARTED_WITH_STICK_B = 0x09D3,   /* word flag */
    DS_DROID_AIM_TIMER = 0x0682,
    DS_DROID_AIM_VOTES = 0x409C,        /* 4 bytes */
    DS_DROID_QUARRY = 0x0919,
    DS_CHAIN_TAIL = 0x06A4,     /* x, y */
    DS_DEMO_WANDER = 0x06A8,    /* x, y */
    DS_TRAIL_HEAD = 0x2CF7,     /* 2 words: entry index * 8 (masked 0x3F8 by the original) */
    DS_FOLLOWER_LAG = 0x06BA,   /* 2 words: entries * 8 */
    DS_RESCUE_MARKER_PENDING = 0x091F,  /* word flag */
    DS_RESCUE_MARKER_Y = 0x0921,        /* 2 words */
    DS_SNAKE_TRAIL = 0x4481,    /* 64 x 8 bytes */
    DS_SPAWN_CURSOR = 0x39EB,   /* word: DS address of the next (type, count) pair */
    DS_FLASH_LIST = 0x06C7,     /* word: DS address of a flash list, 0 = none */
    DS_HISCORES = 0x40A0,       /* 10 x {8 ASCII digits, 8 name bytes} */
    DS_HISCORE_MARK = 0x09DE,   /* 2 words */
    DS_RUDE_NAME = 0x0874,      /* word */
    DS_CONTINUE_SECONDS = 0x0621,
    DS_CONTINUE_TICKS = 0x0623,
    DS_SOUND_KEY_LATCH = 0x0686,    /* word flag */
    DS_PAUSE_MESSAGE = 0x09EC,  /* one of the words at 09E2 (sprite bank offsets), or 0 */
    PAUSE_POINTERS = 0x09E2,

    ENTITY_SIZE = 0x40,
    PLAYER_BLOCK_SIZE = 0xC8,
    PLAYER_UNUSED = 0x26,       /* 21CC..2209 inside the block */
    PLAYER_UNUSED_SIZE = 0x3E,
    PLAYER_ROSTER = 0x64,       /* 220A */
    SHOT_SIZE = 0x10,
    TRAIL_POINT_SIZE = 8,
    HISCORE_ROW_SIZE = 16,
};

/* ---- byte access ----------------------------------------------------------------------- */

typedef struct Reader {
    const uint8_t *dg;
} Reader;

typedef struct Writer {
    uint8_t *dg;                /* NULL when only building the mask */
    uint8_t *mask;              /* NULL when not building it */
    unsigned *overlaps;         /* bytes written twice (mask mode) */
} Writer;

static uint8_t get8(Reader r, unsigned off) { return r.dg[off & 0xFFFF]; }
static uint16_t get16(Reader r, unsigned off)
{
    return (uint16_t)(get8(r, off) | get8(r, off + 1) << 8);
}
static int16_t gets16(Reader r, unsigned off) { return (int16_t)get16(r, off); }

static void put8k(Writer w, unsigned off, uint8_t v, uint8_t kind)
{
    off &= 0xFFFF;
    if (w.dg)
        w.dg[off] = v;
    if (w.mask) {
        if (w.mask[off] != DGROUP_MASK_UNMAPPED)
            (*w.overlaps)++;
        w.mask[off] = kind;
    }
}
static void put8(Writer w, unsigned off, uint8_t v) { put8k(w, off, v, DGROUP_MASK_FIELD); }
static void put16(Writer w, unsigned off, uint16_t v)
{
    put8(w, off, (uint8_t)v);
    put8(w, off + 1, (uint8_t)(v >> 8));
}
static void zero(Writer w, unsigned off, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        put8k(w, off + i, 0, DGROUP_MASK_DERIVED);
}

static uint32_t get32(Reader r, unsigned off)
{
    return get16(r, off) | (uint32_t)get16(r, off + 2) << 16;
}
static void put32(Writer w, unsigned off, uint32_t v)
{
    put16(w, off, (uint16_t)v);
    put16(w, off + 2, (uint16_t)(v >> 16));
}

/* Per-ship control words: not one record but scattered words, P1 and P2 apart (src/game/state.h
 * ShipControl). 0 = no location (P1's trail_flag is a local in the original). */
static const struct ShipControlLayout {
    uint16_t move, shot_velocity, shot_direction, aim, facing, trail_flag;
} SHIP_CONTROL[PLAYER_COUNT] = {
    { 0x06BE, 0x069C, 0x06AC, 0x06B6, 0x06C2, 0 },
    { 0x06B0, 0x06A0, 0x06AE, 0x06B8, 0x07F9, 0x06B4 },
};

/* Trail positions: the original keeps entry counts as byte offsets into 8-byte records. */
static int16_t getentries(Reader r, unsigned off) { return (int16_t)(gets16(r, off) / 8); }
static void putentries(Writer w, unsigned off, int16_t entries)
{
    put16(w, off, (uint16_t)(entries * 8));
}

/* Flag words: the original only tests them against 0 and stores 1. */
static bool getflag16(Reader r, unsigned off) { return get16(r, off) != 0; }
static void putflag16(Writer w, unsigned off, bool v) { put16(w, off, v ? 1 : 0); }

/* ---- pieces ----------------------------------------------------------------------------- */

static void entity_from(Entity *e, Reader r, unsigned base)
{
    e->think = (int8_t)get8(r, base + 0x00);
    e->hatch_into = (int8_t)get8(r, base + 0x01);
    e->x = gets16(r, base + 0x02);
    e->y = gets16(r, base + 0x04);
    e->anim = gets16(r, base + 0x06);
    e->vx = gets16(r, base + 0x08);
    e->vy = gets16(r, base + 0x0A);
    e->sprite = gets16(r, base + 0x0C);
    e->scale = gets16(r, base + 0x0E);
    e->solid = gets16(r, base + 0x10);
    e->aux12 = gets16(r, base + 0x12);
    e->timer = gets16(r, base + 0x14);
    e->aux16 = gets16(r, base + 0x16);
    e->hp = gets16(r, base + 0x18);
    e->aux1a = gets16(r, base + 0x1A);
    e->aux1c = gets16(r, base + 0x1C);
    e->kill_action = gets16(r, base + 0x1E);
    e->sub_mode = gets16(r, base + 0x20);
    e->reload = get8(r, base + 0x22);
    e->countdown = get8(r, base + 0x23);
    e->aux24 = gets16(r, base + 0x24);
    e->aux26 = gets16(r, base + 0x26);
    e->anim_toggle = gets16(r, base + 0x28);
    e->aux2a = gets16(r, base + 0x2A);
    e->sprite_ref = gets16(r, base + 0x2C);
    e->aux2e = gets16(r, base + 0x2E);
    e->aux30 = gets16(r, base + 0x30);
    e->overlay.x = gets16(r, base + 0x32);
    e->overlay.y = gets16(r, base + 0x34);
    e->overlay.sprite = gets16(r, base + 0x36);
    e->overlay.frames = gets16(r, base + 0x38);
    e->kind = gets16(r, base + 0x3A);
    e->death_sample = get8(r, base + 0x3C);
    e->death_rate = get8(r, base + 0x3D);
    e->hit_handler = gets16(r, base + 0x3E);
}

static void entity_to(const Entity *e, Writer w, unsigned base)
{
    put8(w, base + 0x00, (uint8_t)e->think);
    put8(w, base + 0x01, (uint8_t)e->hatch_into);
    put16(w, base + 0x02, (uint16_t)e->x);
    put16(w, base + 0x04, (uint16_t)e->y);
    put16(w, base + 0x06, (uint16_t)e->anim);
    put16(w, base + 0x08, (uint16_t)e->vx);
    put16(w, base + 0x0A, (uint16_t)e->vy);
    put16(w, base + 0x0C, (uint16_t)e->sprite);
    put16(w, base + 0x0E, (uint16_t)e->scale);
    put16(w, base + 0x10, (uint16_t)e->solid);
    put16(w, base + 0x12, (uint16_t)e->aux12);
    put16(w, base + 0x14, (uint16_t)e->timer);
    put16(w, base + 0x16, (uint16_t)e->aux16);
    put16(w, base + 0x18, (uint16_t)e->hp);
    put16(w, base + 0x1A, (uint16_t)e->aux1a);
    put16(w, base + 0x1C, (uint16_t)e->aux1c);
    put16(w, base + 0x1E, (uint16_t)e->kill_action);
    put16(w, base + 0x20, (uint16_t)e->sub_mode);
    put8(w, base + 0x22, e->reload);
    put8(w, base + 0x23, e->countdown);
    put16(w, base + 0x24, (uint16_t)e->aux24);
    put16(w, base + 0x26, (uint16_t)e->aux26);
    put16(w, base + 0x28, (uint16_t)e->anim_toggle);
    put16(w, base + 0x2A, (uint16_t)e->aux2a);
    put16(w, base + 0x2C, (uint16_t)e->sprite_ref);
    put16(w, base + 0x2E, (uint16_t)e->aux2e);
    put16(w, base + 0x30, (uint16_t)e->aux30);
    put16(w, base + 0x32, (uint16_t)e->overlay.x);
    put16(w, base + 0x34, (uint16_t)e->overlay.y);
    put16(w, base + 0x36, (uint16_t)e->overlay.sprite);
    put16(w, base + 0x38, (uint16_t)e->overlay.frames);
    put16(w, base + 0x3A, (uint16_t)e->kind);
    put8(w, base + 0x3C, e->death_sample);
    put8(w, base + 0x3D, e->death_rate);
    put16(w, base + 0x3E, (uint16_t)e->hit_handler);
}

static void player_from(Player *p, Reader r, unsigned base)
{
    p->lives = gets16(r, base + 0x00);
    p->wave = gets16(r, base + 0x02);
    p->level = gets16(r, base + 0x04);
    p->enemy_speed_x = gets16(r, base + 0x06);
    p->enemy_speed_y = gets16(r, base + 0x08);
    p->wave_type = gets16(r, base + 0x0A);
    p->swarm_reaim_period = gets16(r, base + 0x0C);
    p->multi_shot = gets16(r, base + 0x0E);
    for (int i = 0; i < 7; i++)
        p->script_param[i] = gets16(r, base + 0x10 + 2 * (unsigned)i);
    p->powerup = gets16(r, base + 0x1E);
    p->pickup_pending = gets16(r, base + 0x20);
    p->bonus_countdown = gets16(r, base + 0x22);
    p->level_loops = gets16(r, base + 0x24);
    for (int i = 0; i < ROSTER_SIZE; i++) {
        p->roster[i].kind = (int8_t)get8(r, base + PLAYER_ROSTER + 2 * (unsigned)i);
        p->roster[i].count = get8(r, base + PLAYER_ROSTER + 2 * (unsigned)i + 1);
    }
}

static void player_to(const Player *p, Writer w, unsigned base)
{
    put16(w, base + 0x00, (uint16_t)p->lives);
    put16(w, base + 0x02, (uint16_t)p->wave);
    put16(w, base + 0x04, (uint16_t)p->level);
    put16(w, base + 0x06, (uint16_t)p->enemy_speed_x);
    put16(w, base + 0x08, (uint16_t)p->enemy_speed_y);
    put16(w, base + 0x0A, (uint16_t)p->wave_type);
    put16(w, base + 0x0C, (uint16_t)p->swarm_reaim_period);
    put16(w, base + 0x0E, (uint16_t)p->multi_shot);
    for (int i = 0; i < 7; i++)
        put16(w, base + 0x10 + 2 * (unsigned)i, (uint16_t)p->script_param[i]);
    put16(w, base + 0x1E, (uint16_t)p->powerup);
    put16(w, base + 0x20, (uint16_t)p->pickup_pending);
    put16(w, base + 0x22, (uint16_t)p->bonus_countdown);
    put16(w, base + 0x24, (uint16_t)p->level_loops);
    zero(w, base + PLAYER_UNUSED, PLAYER_UNUSED_SIZE);   /* never referenced */
    for (int i = 0; i < ROSTER_SIZE; i++) {
        put8(w, base + PLAYER_ROSTER + 2 * (unsigned)i, (uint8_t)p->roster[i].kind);
        put8(w, base + PLAYER_ROSTER + 2 * (unsigned)i + 1, p->roster[i].count);
    }
}

/* Score: 8 decimal digits, one per byte, most significant first (4441/4449). */
static uint32_t score_from(Reader r, unsigned base)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < SCORE_DIGITS; i++)
        v = v * 10 + get8(r, base + i);     /* digits > 9 are not representable */
    return v % 100000000u;
}

static void score_to(uint32_t score, Writer w, unsigned base)
{
    score %= 100000000u;
    for (int i = SCORE_DIGITS - 1; i >= 0; i--) {
        put8(w, base + (unsigned)i, (uint8_t)(score % 10));
        score /= 10;
    }
}

/* High-score row: 8 ASCII digits then 8 name bytes (as scores.yak). */
static void hiscore_from(HiscoreEntry *h, Reader r, unsigned base)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 8; i++)
        v = v * 10 + (uint32_t)((get8(r, base + i) - '0') & 0xFF);
    h->score = v % 100000000u;
    for (unsigned i = 0; i < HIGH_SCORE_NAME_SIZE; i++)
        h->name[i] = (char)get8(r, base + 8 + i);
    h->name[HIGH_SCORE_NAME_SIZE] = '\0';
}

static void hiscore_to(const HiscoreEntry *h, Writer w, unsigned base)
{
    char digits[9];
    snprintf(digits, sizeof digits, "%08u", (unsigned)(h->score % 100000000u));
    for (unsigned i = 0; i < 8; i++)
        put8(w, base + i, (uint8_t)digits[i]);
    for (unsigned i = 0; i < HIGH_SCORE_NAME_SIZE; i++)
        put8(w, base + 8 + i, (uint8_t)h->name[i]);
}

/* ---- pointers into the extracted data ---------------------------------------------------
 * [0x39EB] and [0x06C7] hold DS addresses of data that the port keeps in the extracted files,
 * so resolving them needs game_data_load(); without the data they stay unmapped. */

static bool have_data(void)
{
    static int loaded = -1;
    if (loaded < 0)
        loaded = game_data_load() ? 1 : 0;
    return loaded == 1;
}

/* DS address of the first spawn pair of a level: the script's ops (opcode byte + operands),
 * then the wave-type byte (game86 1000:020b). */
static unsigned level_spawn_list(const LevelScript *level)
{
    unsigned off = level->ds_offset;
    for (unsigned i = 0; i < level->op_count; i++)
        off += 1u + level->ops[i].operand_count;
    return off + 1;
}

/* game86 code offsets of the hooks, indexed by TickHook / DrawHook (state.h). */
static const uint16_t TICK_HOOKS[] = {
    [TICK_HOOK_NONE] = 0x3fde,  [TICK_HOOK_TITLE_TUNE] = 0x084e, [TICK_HOOK_TITLE] = 0x3f6f,
    [TICK_HOOK_PLAY] = 0x1562,  [TICK_HOOK_PAUSE] = 0x14d0,      [TICK_HOOK_CONTINUE] = 0x0816,
    [TICK_HOOK_NAME_ENTRY] = 0x5b06,
};
static const uint16_t DRAW_HOOKS[] = {
    [DRAW_HOOK_NONE] = 0x0000,  [DRAW_HOOK_TITLE] = 0x402c,      [DRAW_HOOK_PLAY] = 0x1a3b,
    [DRAW_HOOK_CONTINUE] = 0x0894, [DRAW_HOOK_NAME_ENTRY] = 0x5ac4,
};
enum {
    TICK_HOOK_COUNT = sizeof TICK_HOOKS / sizeof TICK_HOOKS[0],
    DRAW_HOOK_COUNT = sizeof DRAW_HOOKS / sizeof DRAW_HOOKS[0],
};

/* Index of `address` in `table`, or 0 (NONE) for an address the port does not name. */
static int hook_from(const uint16_t *table, int count, uint16_t address)
{
    for (int i = 0; i < count; i++)
        if (table[i] == address)
            return i;
    return 0;
}

static uint16_t hook_to(const uint16_t *table, int count, int hook)
{
    return table[hook >= 0 && hook < count ? hook : 0];
}

static void spawn_from(SpawnCursor *c, uint16_t off)
{
    if (off == 0) {             /* null before the first level starts */
        c->source = SPAWN_NONE;
        c->level = 0;
        c->index = 0;
        return;
    }
    unsigned roster = DS_PLAYER + PLAYER_ROSTER;
    if (off >= roster && off < roster + 2 * ROSTER_SIZE + 2 && (off - roster) % 2 == 0) {
        c->source = SPAWN_FROM_ROSTER;
        c->level = 0;
        c->index = (uint16_t)((off - roster) / 2);
        return;
    }
    for (unsigned l = 0; l < LEVEL_COUNT; l++) {
        const LevelScript *level = asset_level(l);
        if (!level)
            continue;
        unsigned start = level_spawn_list(level);
        if (off >= start && off <= start + 2u * level->spawn_count && (off - start) % 2 == 0) {
            c->source = SPAWN_FROM_LEVEL;
            c->level = (int16_t)l;
            c->index = (uint16_t)((off - start) / 2);
            return;
        }
    }
    /* not inside any list (e.g. 0 before the first wave): left as it was */
}

/* The spawn pairs a think_13 drop zeroes (game86 1000:123c..1241 via 10b0, levels.dat
 * clobber_10b0): DS address, length and the intact pairs, false if this extraction has none. */
static bool clobber_bytes(unsigned *off, unsigned *n, const LevelSpawn **intact)
{
    const LevelSet *set = asset_levels();
    if (!set || set->clobber_10b0.level == LEVEL_NONE)
        return false;
    const LevelScript *level = asset_level(set->clobber_10b0.level);
    if (!level || set->clobber_10b0.first_spawn + set->clobber_10b0.spawn_count
                      > level->spawn_count)
        return false;
    *off = level_spawn_list(level) + 2u * set->clobber_10b0.first_spawn;
    *n = 2u * set->clobber_10b0.spawn_count;
    *intact = level->spawns + set->clobber_10b0.first_spawn;
    return true;
}

/* Read as set when every byte is zero (pairs that are (0, 0) anyway read the same either
 * way). Written as zeros when set, else as the intact pairs from levels.dat, so a state
 * written into a blank image (the replay driver) still holds the script bytes. */
static bool clobber_from(Reader r)
{
    unsigned off, n;
    const LevelSpawn *intact;
    if (!clobber_bytes(&off, &n, &intact))
        return false;
    for (unsigned i = 0; i < n; i++)
        if (get8(r, off + i) != 0)
            return false;
    return true;
}

static void clobber_to(bool clobbered, Writer w)
{
    unsigned off, n;
    const LevelSpawn *intact;
    if (!clobber_bytes(&off, &n, &intact))
        return;
    for (unsigned i = 0; i < n; i++) {
        const LevelSpawn *pair = &intact[i / 2];
        uint8_t v = clobbered ? 0 : (i % 2 == 0 ? pair->enemy_type : pair->count);
        put8(w, off + i, v);
    }
}

/* The word the original stores in [09EC] for pause message k (1..PAUSE_MESSAGES): the bank
 * offset of pause picture k-1 (game86 1000:15aa reads it from DS:09E2). From the extracted
 * sprites when they are loaded, else from the image's own table. */
static uint16_t pause_pointer(int k, const uint8_t *image)
{
    if (have_data()) {
        int index = asset_sprite_id_index((SpriteId)(SPRITE_ID_PAUSE_FIRST + k - 1));
        const SpriteFrame *f = index >= 0 ? asset_sprite_frame(SPRITE_BANK_GAME, (unsigned)index)
                                          : NULL;
        if (f)
            return f->bank_offset;
    }
    if (!image)
        return 0;
    unsigned off = PAUSE_POINTERS + 2u * (unsigned)(k - 1);
    return (uint16_t)(image[off] | image[off + 1] << 8);
}

static int16_t pause_from(Reader r)
{
    uint16_t v = get16(r, DS_PAUSE_MESSAGE);
    for (int k = 1; v && k <= PAUSE_MESSAGES; k++)
        if (pause_pointer(k, r.dg) == v)
            return (int16_t)k;
    return 0;   /* 0, or not one of the pictures (never stored by the original) */
}

static uint16_t pause_to(int16_t k, const uint8_t *image)
{
    return k >= 1 && k <= PAUSE_MESSAGES ? pause_pointer(k, image) : 0;
}

/* [06C7] points into a flash list, one word further per frame, up to its 0 terminator
 * (= word_count): the list containing the address and the words already shown. */
static void flash_from(GameState *g, uint16_t ptr)
{
    g->flash_list = NULL;
    g->flash_pos = 0;
    if (ptr == 0)
        return;
    const FlashList *exact = asset_flash_list_at(ptr);
    if (exact) {
        g->flash_list = exact;
        return;
    }
    const PaletteSet *set = asset_palettes();
    for (unsigned i = 0; set && i < set->flash_list_count; i++) {
        const FlashList *l = &set->flash_lists[i];
        if (ptr > l->ds_offset && ptr <= l->ds_offset + 2u * l->word_count
            && (ptr - l->ds_offset) % 2 == 0) {
            g->flash_list = l;
            g->flash_pos = (uint16_t)((ptr - l->ds_offset) / 2);
            return;
        }
    }
    /* not inside any list: reads as none */
}

static uint16_t flash_to(const GameState *g)
{
    return g->flash_list ? (uint16_t)(g->flash_list->ds_offset + 2u * g->flash_pos) : 0;
}

static uint16_t spawn_to(const SpawnCursor *c)
{
    if (c->source == SPAWN_NONE)
        return 0;
    if (c->source == SPAWN_FROM_ROSTER)
        return (uint16_t)(DS_PLAYER + PLAYER_ROSTER + 2u * c->index);
    const LevelScript *level = c->level >= 0 ? asset_level((unsigned)c->level) : NULL;
    return level ? (uint16_t)(level_spawn_list(level) + 2u * c->index) : 0;
}

/* Tune pointers: DS addresses in the list at 09D5, null (TUNE_NO_LIST) until 1000:5c42. */
static uint16_t tune_from(uint16_t ptr) { return ptr ? (uint16_t)(ptr - TUNE_LIST) : TUNE_NO_LIST; }
static uint16_t tune_to(uint16_t offset)
{
    return offset == TUNE_NO_LIST ? 0 : (uint16_t)(offset + TUNE_LIST);
}

/* ---- whole state ---------------------------------------------------------------------- */

void gamestate_from_dgroup(GameState *g, const uint8_t *dgroup)
{
    Reader r = { dgroup };

    g->vga = get8(r, DS_VGA) != 0;
    g->joystick = get8(r, DS_JOYSTICK) != 0;
    g->sound_present = get8(r, DS_SOUND_PRESENT) != 0;
    g->sound_on = get8(r, DS_SOUND_ON) != 0;
    g->flash_mode = get8(r, DS_FLASH_MODE);

    memcpy(g->rng.table, dgroup + DS_RNG_TABLE, RNG_TABLE_SIZE);
    g->rng.position = get16(r, DS_RNG_POSITION);
    for (int i = 0; i < 3; i++)
        g->rng.mixer.history[i] = get16(r, DS_RNG_MIXER + 2 * (unsigned)i);
    g->rng.mixer.salt = get16(r, DS_RNG_MIXER + 6);

    g->options = get8(r, DS_OPTIONS);
    player_from(&g->player, r, DS_PLAYER);
    player_from(&g->player_other, r, DS_PLAYER_OTHER);
    for (int i = 0; i < PLAYER_COUNT; i++) {
        g->ship[i].x = gets16(r, DS_SHIPS + 4 * (unsigned)i);
        g->ship[i].y = gets16(r, DS_SHIPS + 4 * (unsigned)i + 2);
        g->score[i] = score_from(r, DS_SCORES + SCORE_DIGITS * (unsigned)i);
    }
    g->target.x = gets16(r, DS_TARGET);
    g->target.y = gets16(r, DS_TARGET + 2);
    g->hud_dirty = getflag16(r, DS_HUD_DIRTY);
    g->continues = gets16(r, DS_CONTINUES);
    g->respawn_timer[0] = gets16(r, DS_RESPAWN_P1);
    g->respawn_timer[1] = gets16(r, DS_RESPAWN_P2);
    g->loop_count = gets16(r, DS_LOOP_COUNT);
    g->level_flags = gets16(r, DS_LEVEL_FLAGS);
    g->speedup_timer = gets16(r, DS_SPEEDUP_TIMER);
    g->speedup_period = gets16(r, DS_SPEEDUP_PERIOD);
    g->game_started = gets16(r, DS_GAME_STARTED);
    g->wave_start_delay = gets16(r, DS_WAVE_START_DELAY);
    g->player_label_offset = gets16(r, DS_PLAYER_LABEL_OFFSET);
    g->spawn_delay = gets16(r, DS_SPAWN_DELAY);
    g->play_ticked = gets16(r, DS_PLAY_FLAG_0617);
    g->level_default = gets16(r, DS_LEVEL_DEFAULT);
    g->pulse_enable = getflag16(r, DS_PULSE_ENABLE);
    g->pulse_index = gets16(r, DS_PULSE_INDEX);
    g->flash_frame_timer = get8(r, DS_FLASH_FRAME_TIMER);
    g->player2_label_offset = gets16(r, DS_PLAYER2_LABEL_OFFSET);
    g->level_variant = gets16(r, DS_LEVEL_VARIANT);
    g->text.x = gets16(r, DS_TEXT_PEN);
    g->text.y = gets16(r, DS_TEXT_PEN + 2);
    g->text.margin = gets16(r, DS_TEXT_PEN + 4);
    g->demo_mode = gets16(r, DS_DEMO_MODE);

    entity_from(&g->companion, r, DS_COMPANION);
    for (int i = 0; i < ENTITY_COUNT; i++)
        entity_from(&g->entities[i], r, DS_ENTITIES + ENTITY_SIZE * (unsigned)i);
    g->free_slots = gets16(r, DS_FREE_SLOTS);
    g->enemies_alive = gets16(r, DS_ENEMIES_ALIVE);
    g->bounced = gets16(r, DS_BOUNCED);

    for (int i = 0; i < SHOT_COUNT; i++) {
        unsigned b = DS_SHOTS + SHOT_SIZE * (unsigned)i;
        Shot *s = &g->shots[i];
        s->x = gets16(r, b + 0x0);
        s->y = gets16(r, b + 0x2);
        s->vx = gets16(r, b + 0x4);
        s->vy = gets16(r, b + 0x6);
        s->direction = gets16(r, b + 0x8);
        s->unused_0a = gets16(r, b + 0xA);
        s->bounce_life = gets16(r, b + 0xC);
        s->unused_0e = gets16(r, b + 0xE);
    }
    g->shots_bounce = getflag16(r, DS_SHOTS_BOUNCE);
    for (int p = 0; p < PLAYER_COUNT; p++) {
        unsigned base = p == 0 ? DS_TRAIL_P1 : DS_TRAIL_P2;
        for (int i = 0; i < TRAIL_LENGTH; i++) {
            unsigned b = base + TRAIL_POINT_SIZE * (unsigned)i;
            TrailPoint *t = &g->trail[p][i];
            t->x = gets16(r, b + 0);
            t->y = gets16(r, b + 2);
            t->flag = gets16(r, b + 4);
            t->aux = gets16(r, b + 6);
        }
    }

    g->wave_result = (WaveResult)get16(r, DS_WAVE_RESULT);
    g->wave_intro_timer = gets16(r, DS_WAVE_INTRO_TIMER);
    if (have_data()) {
        spawn_from(&g->spawn, get16(r, DS_SPAWN_CURSOR));
        flash_from(g, get16(r, DS_FLASH_LIST));
        g->spawn_pairs_clobbered = clobber_from(r);
    }
    g->spawn_remaining = gets16(r, DS_SPAWN_REMAINING);
    g->spawn_type = gets16(r, DS_SPAWN_TYPE);
    g->spawning_done = getflag16(r, DS_SPAWNING_DONE);
    g->unhatched_left = gets16(r, DS_UNHATCHED_LEFT);
    g->tuning.init01_mode = gets16(r, DS_TUNING_INIT01_MODE);
    g->tuning.init0d_hp = gets16(r, DS_TUNING_INIT0D_HP);
    g->tuning.segment_family = gets16(r, DS_TUNING_SEGMENT_FAMILY);
    g->tuning.orbit_phase_step = gets16(r, DS_TUNING_ORBIT_PHASE_STEP);
    g->tuning.growth_step = gets16(r, DS_TUNING_P0826);
    g->tuning.accum_start = get32(r, DS_TUNING_ACCUM_START);
    g->tuning.runner_fire = gets16(r, DS_TUNING_P082C);
    g->tuning.runner_hits = gets16(r, DS_TUNING_P082E);
    g->tuning.dropper_creep_mask = gets16(r, DS_TUNING_P0830);
    g->tuning.init19_vx = gets16(r, DS_TUNING_INIT19_VX);

    g->shield_timer = gets16(r, DS_SHIELD_TIMER);
    g->smart_bomb = getflag16(r, DS_SMART_BOMB);
    g->death_sound_timer = gets16(r, DS_DEATH_SOUND_TIMER);
    g->swarm_count = gets16(r, DS_SWARM_COUNT);

    g->spawn_sprite = gets16(r, DS_SPAWN_SPRITE);
    g->eggs_spawned = gets16(r, DS_SPAWN_COUNT_0804);
    g->unhatched = gets16(r, DS_UNHATCHED);
    g->spawn_phase = gets16(r, DS_SPAWN_PHASE);
    g->orbit_angle = get32(r, DS_ORBIT_ANGLE);
    g->orbit_next_phase = gets16(r, DS_ORBIT_NEXT_PHASE);
    g->segment_sprite = gets16(r, DS_SEGMENT_SPRITE);
    g->speed_accum = get32(r, DS_SPEED_ACCUM);
    g->kills_toward_bonus = gets16(r, DS_KILLS_TOWARD_BONUS);
    g->shot_power_kept = gets16(r, DS_POWERUP_0652);
    g->debris_alt = gets16(r, DS_DEBRIS_ALT);
    g->snake_head = gets16(r, DS_SNAKE_HEAD);
    for (int i = 0; i < PLAYER_COUNT; i++) {
        g->trail_head[i] = getentries(r, DS_TRAIL_HEAD + 2 * (unsigned)i);
        g->follower_lag[i] = getentries(r, DS_FOLLOWER_LAG + 2 * (unsigned)i);
        g->rescue_marker_y[i] = gets16(r, DS_RESCUE_MARKER_Y + 2 * (unsigned)i);
    }
    g->rescue_marker_pending = getflag16(r, DS_RESCUE_MARKER_PENDING);

    for (int i = 0; i < PLAYER_COUNT; i++) {
        const struct ShipControlLayout *l = &SHIP_CONTROL[i];
        ShipControl *c = &g->ship_control[i];
        c->move.x = gets16(r, l->move);
        c->move.y = gets16(r, l->move + 2u);
        c->shot_velocity.x = gets16(r, l->shot_velocity);
        c->shot_velocity.y = gets16(r, l->shot_velocity + 2u);
        c->shot_direction = gets16(r, l->shot_direction);
        c->aim = gets16(r, l->aim);
        c->facing = gets16(r, l->facing);
        if (l->trail_flag)
            c->trail_flag = gets16(r, l->trail_flag);
    }
    g->tune.fire_pressed = getflag16(r, DS_TUNE_FIRE_PRESSED);
    g->tune.period = gets16(r, DS_TUNE_PERIOD);
    g->tune.countdown = gets16(r, DS_TUNE_COUNTDOWN);
    g->tune.position = tune_from(get16(r, DS_TUNE_POSITION));
    g->tune.restart = tune_from(get16(r, DS_TUNE_RESTART));
    g->options_default = get8(r, DS_OPTIONS_DEFAULT);
    for (int i = 0; i < TITLE_LETTERS; i++)
        g->title_objects[i] = gets16(r, DS_TITLE_OBJECTS + 2 * (unsigned)i);
    g->title_timer = gets16(r, DS_TITLE_TIMER);
    g->title_key_latch = get8(r, DS_TITLE_KEY_LATCH);
    g->fire_delay = get8(r, DS_FIRE_DELAY);
    g->fire_turn = gets16(r, DS_FIRE_TURN);
    g->started_with_stick_b = getflag16(r, DS_STARTED_WITH_STICK_B);
    g->droid_aim_timer = gets16(r, DS_DROID_AIM_TIMER);
    for (int i = 0; i < 4; i++)
        g->droid_aim_votes[i] = get8(r, DS_DROID_AIM_VOTES + (unsigned)i);
    g->droid_quarry = gets16(r, DS_DROID_QUARRY);
    g->chain_tail.x = gets16(r, DS_CHAIN_TAIL);
    g->chain_tail.y = gets16(r, DS_CHAIN_TAIL + 2);
    g->demo_wander.x = gets16(r, DS_DEMO_WANDER);
    g->demo_wander.y = gets16(r, DS_DEMO_WANDER + 2);
    for (int i = 0; i < SNAKE_TRAIL_LENGTH; i++) {
        unsigned b = DS_SNAKE_TRAIL + 8 * (unsigned)i;
        g->snake_trail[i].x = gets16(r, b + 0);
        g->snake_trail[i].y = gets16(r, b + 2);
        g->snake_trail[i].sprite = gets16(r, b + 4);
        g->snake_trail[i].unused = gets16(r, b + 6);
    }
    g->tick_count = get16(r, DS_TICK_COUNT);
    g->tick_hook = (TickHook)hook_from(TICK_HOOKS, TICK_HOOK_COUNT, get16(r, DS_TICK_HOOK));
    g->draw_hook = (DrawHook)hook_from(DRAW_HOOKS, DRAW_HOOK_COUNT, get16(r, DS_DRAW_HOOK));
    g->frame_drawn = get8(r, DS_FRAME_DRAWN) != 0;

    for (int i = 0; i < HISCORE_COUNT; i++)
        hiscore_from(&g->hiscores[i], r, DS_HISCORES + HISCORE_ROW_SIZE * (unsigned)i);
    g->hiscore_mark[0] = gets16(r, DS_HISCORE_MARK);
    g->hiscore_mark[1] = gets16(r, DS_HISCORE_MARK + 2);
    g->rude_name = getflag16(r, DS_RUDE_NAME);
    g->continue_seconds = gets16(r, DS_CONTINUE_SECONDS);
    g->continue_ticks = gets16(r, DS_CONTINUE_TICKS);
    g->sound_key_latch = getflag16(r, DS_SOUND_KEY_LATCH);
    g->pause_message = pause_from(r);
}

static void write_state(const GameState *g, Writer w)
{
    put8(w, DS_VGA, g->vga ? 1 : 0);
    put8(w, DS_JOYSTICK, g->joystick ? 1 : 0);
    put8k(w, DS_KEYBOARD, g->joystick ? 0 : 1, DGROUP_MASK_DERIVED);
    put8(w, DS_SOUND_PRESENT, g->sound_present ? 1 : 0);
    put8(w, DS_SOUND_ON, g->sound_on ? 1 : 0);
    put8(w, DS_FLASH_MODE, g->flash_mode);

    for (int i = 0; i < RNG_TABLE_SIZE; i++)
        put8(w, DS_RNG_TABLE + (unsigned)i, g->rng.table[i]);
    put16(w, DS_RNG_POSITION, g->rng.position);
    for (int i = 0; i < 3; i++)
        put16(w, DS_RNG_MIXER + 2 * (unsigned)i, g->rng.mixer.history[i]);
    put16(w, DS_RNG_MIXER + 6, g->rng.mixer.salt);

    put8(w, DS_OPTIONS, g->options);
    player_to(&g->player, w, DS_PLAYER);
    player_to(&g->player_other, w, DS_PLAYER_OTHER);
    for (int i = 0; i < PLAYER_COUNT; i++) {
        put16(w, DS_SHIPS + 4 * (unsigned)i, (uint16_t)g->ship[i].x);
        put16(w, DS_SHIPS + 4 * (unsigned)i + 2, (uint16_t)g->ship[i].y);
        score_to(g->score[i], w, DS_SCORES + SCORE_DIGITS * (unsigned)i);
    }
    put16(w, DS_TARGET, (uint16_t)g->target.x);
    put16(w, DS_TARGET + 2, (uint16_t)g->target.y);
    putflag16(w, DS_HUD_DIRTY, g->hud_dirty);
    put16(w, DS_CONTINUES, (uint16_t)g->continues);
    put16(w, DS_RESPAWN_P1, (uint16_t)g->respawn_timer[0]);
    put16(w, DS_RESPAWN_P2, (uint16_t)g->respawn_timer[1]);
    put16(w, DS_LOOP_COUNT, (uint16_t)g->loop_count);
    put16(w, DS_LEVEL_FLAGS, (uint16_t)g->level_flags);
    put16(w, DS_SPEEDUP_TIMER, (uint16_t)g->speedup_timer);
    put16(w, DS_SPEEDUP_PERIOD, (uint16_t)g->speedup_period);
    put16(w, DS_GAME_STARTED, (uint16_t)g->game_started);
    put16(w, DS_WAVE_START_DELAY, (uint16_t)g->wave_start_delay);
    put16(w, DS_PLAYER_LABEL_OFFSET, (uint16_t)g->player_label_offset);
    put16(w, DS_SPAWN_DELAY, (uint16_t)g->spawn_delay);
    put16(w, DS_PLAY_FLAG_0617, (uint16_t)g->play_ticked);
    put16(w, DS_LEVEL_DEFAULT, (uint16_t)g->level_default);
    putflag16(w, DS_PULSE_ENABLE, g->pulse_enable);
    put16(w, DS_PULSE_INDEX, (uint16_t)g->pulse_index);
    put8(w, DS_FLASH_FRAME_TIMER, g->flash_frame_timer);
    put16(w, DS_PLAYER2_LABEL_OFFSET, (uint16_t)g->player2_label_offset);
    put16(w, DS_LEVEL_VARIANT, (uint16_t)g->level_variant);
    put16(w, DS_TEXT_PEN, (uint16_t)g->text.x);
    put16(w, DS_TEXT_PEN + 2, (uint16_t)g->text.y);
    put16(w, DS_TEXT_PEN + 4, (uint16_t)g->text.margin);
    put16(w, DS_DEMO_MODE, (uint16_t)g->demo_mode);

    entity_to(&g->companion, w, DS_COMPANION);
    for (int i = 0; i < ENTITY_COUNT; i++)
        entity_to(&g->entities[i], w, DS_ENTITIES + ENTITY_SIZE * (unsigned)i);
    put16(w, DS_FREE_SLOTS, (uint16_t)g->free_slots);
    put16(w, DS_ENEMIES_ALIVE, (uint16_t)g->enemies_alive);
    put16(w, DS_BOUNCED, (uint16_t)g->bounced);

    for (int i = 0; i < SHOT_COUNT; i++) {
        unsigned b = DS_SHOTS + SHOT_SIZE * (unsigned)i;
        const Shot *s = &g->shots[i];
        put16(w, b + 0x0, (uint16_t)s->x);
        put16(w, b + 0x2, (uint16_t)s->y);
        put16(w, b + 0x4, (uint16_t)s->vx);
        put16(w, b + 0x6, (uint16_t)s->vy);
        put16(w, b + 0x8, (uint16_t)s->direction);
        put16(w, b + 0xA, (uint16_t)s->unused_0a);
        put16(w, b + 0xC, (uint16_t)s->bounce_life);
        put16(w, b + 0xE, (uint16_t)s->unused_0e);
    }
    putflag16(w, DS_SHOTS_BOUNCE, g->shots_bounce);
    for (int p = 0; p < PLAYER_COUNT; p++) {
        unsigned base = p == 0 ? DS_TRAIL_P1 : DS_TRAIL_P2;
        for (int i = 0; i < TRAIL_LENGTH; i++) {
            unsigned b = base + TRAIL_POINT_SIZE * (unsigned)i;
            const TrailPoint *t = &g->trail[p][i];
            put16(w, b + 0, (uint16_t)t->x);
            put16(w, b + 2, (uint16_t)t->y);
            put16(w, b + 4, (uint16_t)t->flag);
            put16(w, b + 6, (uint16_t)t->aux);
        }
    }

    put16(w, DS_WAVE_RESULT, (uint16_t)g->wave_result);
    put16(w, DS_WAVE_INTRO_TIMER, (uint16_t)g->wave_intro_timer);
    put16(w, DS_SPAWN_REMAINING, (uint16_t)g->spawn_remaining);
    put16(w, DS_SPAWN_TYPE, (uint16_t)g->spawn_type);
    putflag16(w, DS_SPAWNING_DONE, g->spawning_done);
    put16(w, DS_UNHATCHED_LEFT, (uint16_t)g->unhatched_left);
    put16(w, DS_TUNING_INIT01_MODE, (uint16_t)g->tuning.init01_mode);
    put16(w, DS_TUNING_INIT0D_HP, (uint16_t)g->tuning.init0d_hp);
    put16(w, DS_TUNING_SEGMENT_FAMILY, (uint16_t)g->tuning.segment_family);
    put16(w, DS_TUNING_ORBIT_PHASE_STEP, (uint16_t)g->tuning.orbit_phase_step);
    put16(w, DS_TUNING_P0826, (uint16_t)g->tuning.growth_step);
    put32(w, DS_TUNING_ACCUM_START, g->tuning.accum_start);
    put16(w, DS_TUNING_P082C, (uint16_t)g->tuning.runner_fire);
    put16(w, DS_TUNING_P082E, (uint16_t)g->tuning.runner_hits);
    put16(w, DS_TUNING_P0830, (uint16_t)g->tuning.dropper_creep_mask);
    put16(w, DS_TUNING_INIT19_VX, (uint16_t)g->tuning.init19_vx);

    put16(w, DS_SHIELD_TIMER, (uint16_t)g->shield_timer);
    putflag16(w, DS_SMART_BOMB, g->smart_bomb);
    put16(w, DS_DEATH_SOUND_TIMER, (uint16_t)g->death_sound_timer);
    put16(w, DS_SWARM_COUNT, (uint16_t)g->swarm_count);

    put16(w, DS_SPAWN_SPRITE, (uint16_t)g->spawn_sprite);
    put16(w, DS_SPAWN_COUNT_0804, (uint16_t)g->eggs_spawned);
    put16(w, DS_UNHATCHED, (uint16_t)g->unhatched);
    put16(w, DS_SPAWN_PHASE, (uint16_t)g->spawn_phase);
    put32(w, DS_ORBIT_ANGLE, g->orbit_angle);
    put16(w, DS_ORBIT_NEXT_PHASE, (uint16_t)g->orbit_next_phase);
    put16(w, DS_SEGMENT_SPRITE, (uint16_t)g->segment_sprite);
    put32(w, DS_SPEED_ACCUM, g->speed_accum);
    put16(w, DS_KILLS_TOWARD_BONUS, (uint16_t)g->kills_toward_bonus);
    put16(w, DS_POWERUP_0652, (uint16_t)g->shot_power_kept);
    put16(w, DS_DEBRIS_ALT, (uint16_t)g->debris_alt);
    put16(w, DS_SNAKE_HEAD, (uint16_t)g->snake_head);
    for (int i = 0; i < PLAYER_COUNT; i++) {
        putentries(w, DS_TRAIL_HEAD + 2 * (unsigned)i, g->trail_head[i]);
        putentries(w, DS_FOLLOWER_LAG + 2 * (unsigned)i, g->follower_lag[i]);
        put16(w, DS_RESCUE_MARKER_Y + 2 * (unsigned)i, (uint16_t)g->rescue_marker_y[i]);
    }
    putflag16(w, DS_RESCUE_MARKER_PENDING, g->rescue_marker_pending);

    for (int i = 0; i < PLAYER_COUNT; i++) {
        const struct ShipControlLayout *l = &SHIP_CONTROL[i];
        const ShipControl *c = &g->ship_control[i];
        put16(w, l->move, (uint16_t)c->move.x);
        put16(w, l->move + 2u, (uint16_t)c->move.y);
        put16(w, l->shot_velocity, (uint16_t)c->shot_velocity.x);
        put16(w, l->shot_velocity + 2u, (uint16_t)c->shot_velocity.y);
        put16(w, l->shot_direction, (uint16_t)c->shot_direction);
        put16(w, l->aim, (uint16_t)c->aim);
        put16(w, l->facing, (uint16_t)c->facing);
        if (l->trail_flag)
            put16(w, l->trail_flag, (uint16_t)c->trail_flag);
    }
    putflag16(w, DS_TUNE_FIRE_PRESSED, g->tune.fire_pressed);
    put16(w, DS_TUNE_PERIOD, (uint16_t)g->tune.period);
    put16(w, DS_TUNE_COUNTDOWN, (uint16_t)g->tune.countdown);
    put16(w, DS_TUNE_POSITION, tune_to(g->tune.position));
    put16(w, DS_TUNE_RESTART, tune_to(g->tune.restart));
    put8(w, DS_OPTIONS_DEFAULT, g->options_default);
    for (int i = 0; i < TITLE_LETTERS; i++)
        put16(w, DS_TITLE_OBJECTS + 2 * (unsigned)i, (uint16_t)g->title_objects[i]);
    put16(w, DS_TITLE_TIMER, (uint16_t)g->title_timer);
    put8(w, DS_TITLE_KEY_LATCH, g->title_key_latch);
    put8(w, DS_FIRE_DELAY, g->fire_delay);
    put16(w, DS_FIRE_TURN, (uint16_t)g->fire_turn);
    putflag16(w, DS_STARTED_WITH_STICK_B, g->started_with_stick_b);
    put16(w, DS_DROID_AIM_TIMER, (uint16_t)g->droid_aim_timer);
    for (int i = 0; i < 4; i++)
        put8(w, DS_DROID_AIM_VOTES + (unsigned)i, g->droid_aim_votes[i]);
    put16(w, DS_DROID_QUARRY, (uint16_t)g->droid_quarry);
    put16(w, DS_CHAIN_TAIL, (uint16_t)g->chain_tail.x);
    put16(w, DS_CHAIN_TAIL + 2, (uint16_t)g->chain_tail.y);
    put16(w, DS_DEMO_WANDER, (uint16_t)g->demo_wander.x);
    put16(w, DS_DEMO_WANDER + 2, (uint16_t)g->demo_wander.y);
    for (int i = 0; i < SNAKE_TRAIL_LENGTH; i++) {
        unsigned b = DS_SNAKE_TRAIL + 8 * (unsigned)i;
        put16(w, b + 0, (uint16_t)g->snake_trail[i].x);
        put16(w, b + 2, (uint16_t)g->snake_trail[i].y);
        put16(w, b + 4, (uint16_t)g->snake_trail[i].sprite);
        put16(w, b + 6, (uint16_t)g->snake_trail[i].unused);
    }
    if (have_data()) {
        put16(w, DS_SPAWN_CURSOR, spawn_to(&g->spawn));
        put16(w, DS_FLASH_LIST, flash_to(g));
        clobber_to(g->spawn_pairs_clobbered, w);
    }
    put16(w, DS_TICK_COUNT, g->tick_count);
    put16(w, DS_TICK_HOOK, hook_to(TICK_HOOKS, TICK_HOOK_COUNT, g->tick_hook));
    put16(w, DS_DRAW_HOOK, hook_to(DRAW_HOOKS, DRAW_HOOK_COUNT, g->draw_hook));
    put8(w, DS_FRAME_DRAWN, g->frame_drawn ? 1 : 0);

    for (int i = 0; i < HISCORE_COUNT; i++)
        hiscore_to(&g->hiscores[i], w, DS_HISCORES + HISCORE_ROW_SIZE * (unsigned)i);
    put16(w, DS_HISCORE_MARK, (uint16_t)g->hiscore_mark[0]);
    put16(w, DS_HISCORE_MARK + 2, (uint16_t)g->hiscore_mark[1]);
    putflag16(w, DS_RUDE_NAME, g->rude_name);
    put16(w, DS_CONTINUE_SECONDS, (uint16_t)g->continue_seconds);
    put16(w, DS_CONTINUE_TICKS, (uint16_t)g->continue_ticks);
    putflag16(w, DS_SOUND_KEY_LATCH, g->sound_key_latch);
    put16(w, DS_PAUSE_MESSAGE, pause_to(g->pause_message, w.dg));
}

void gamestate_to_dgroup(const GameState *game, uint8_t *dgroup)
{
    Writer w = { dgroup, NULL, NULL };
    write_state(game, w);
}

unsigned dgroup_map_mask(uint8_t *mask)
{
    static GameState blank;
    unsigned overlaps = 0;
    memset(mask, DGROUP_MASK_UNMAPPED, DGROUP_SIZE);
    Writer w = { NULL, mask, &overlaps };
    write_state(&blank, w);
    return overlaps;
}

bool dgroup_map_has_data(void)
{
    return have_data();
}

size_t gamestate_size(void)
{
    return sizeof(GameState);
}

Entity *gamestate_entity(GameState *game, int index)
{
    if (index < 0)
        return &game->companion;
    return index < ENTITY_COUNT ? &game->entities[index] : NULL;
}
