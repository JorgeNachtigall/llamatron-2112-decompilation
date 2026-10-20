/*
 * entity_views.h - named views of the entity record's handler-specific words
 * (docs/entity-fields.md). Included by state.h, which lays them over the record.
 *
 * The record keeps the original's 64-byte layout (adapters and oracles stay 1:1). Two of its
 * regions mean different things per kind of entity:
 *   +08..+0b  velocity for most kinds (Entity.vx/vy), something else for four (MotionView*)
 *   +12..+31  each family's own state (the *View structs below)
 * Each region is an anonymous union of the general names (state.h), one named view per
 * family and a raw word view for the layout copies (tests/adapters). EntityWords
 * (entity_layout.h) views a whole record as 32 words, for attract_end's overrun. A record
 * keeps whatever its previous kind left in the bytes, as in the original; a view only names
 * them. entity_layout.h pins every member to its original offset.
 *
 * Two shapes recur:
 *   ByteCounter  a word used as two bytes: low byte = period, high byte = counts down
 *   Fixed16      a 16.16 value in two words, fraction first (alignment 2, so +26 / +2a work)
 */
#ifndef LLAMATRON_GAME_ENTITY_VIEWS_H
#define LLAMATRON_GAME_ENTITY_VIEWS_H

#include <stdint.h>

/* ByteCounter and the int16_t general names overlay the same bytes: low byte first. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "entity_views.h needs a little-endian host"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Point16 {
    int16_t x, y;
} Point16;

typedef struct ByteCounter {
    uint8_t period;             /* low byte: the value it restarts from */
    uint8_t left;               /* high byte: counts down */
} ByteCounter;

typedef struct Fixed16 {
    uint16_t frac;              /* low word */
    int16_t whole;              /* high word: the pixel coordinate or velocity */
} Fixed16;

static inline int32_t fixed16_get(Fixed16 f)
{
    return (int32_t)((uint32_t)(uint16_t)f.whole << 16 | f.frac);
}

static inline void fixed16_set(Fixed16 *f, int32_t value)
{
    f->frac = (uint16_t)((uint32_t)value & 0xffffu);
    f->whole = (int16_t)(uint16_t)((uint32_t)value >> 16);
}

/* ---- +08..+0b ------------------------------------------------------------------------- */

typedef struct SnakeHeadMotion {    /* think 1d */
    int16_t next_trail_slot;    /* +08 snake_trail slot written next (then + 1, & 63) */
    ByteCounter spawn;          /* +0a one segment step per period */
} SnakeHeadMotion;

typedef struct WalkerMotion {       /* think 23 */
    ByteCounter fire;           /* +08 a child or a seeker per period */
    int16_t unused_0a;          /* +0a */
} WalkerMotion;

typedef struct GrowthMotion {       /* think 1f / 28 and their segments */
    int16_t sprite_base;        /* +08 facing sprites (+1 facing right, +2 flashing) */
    int16_t unused_0a;          /* +0a */
} GrowthMotion;

typedef struct RawMotion {          /* layout copies only */
    uint16_t words[2];          /* +08, +0a */
} RawMotion;

/* ---- +12..+31: one view per family (32 bytes each) ------------------------------------- */

typedef struct LlamaView {          /* think 05 llama, 06 follower; the egg (think 00) sets sprite_base */
    int16_t unused_12;          /* +12 */
    union {                     /* +14 */
        int16_t turn_timer;     /*     05: ticks to the next random turn */
        int16_t trail_lag;      /*     06: byte offset (8 per entry) behind its ship's newest trail point */
    };
    int16_t facing;             /* +16 05: sprite offset of the direction: 0, 2, 4 or 6
                                   (wander_sprite[] while wandering, 0 / 6 towards its ship) */
    int16_t claimed;            /* +18 0 = free; herders and hunters set 1 */
    int16_t unused_1a_1c[2];    /* +1a, +1c */
    int16_t ship;               /* +1e 06: whose trail it follows (FOLLOW_P1 / P2) */
    int16_t unused_20_2e[8];    /* +20..+2e */
    int16_t sprite_base;        /* +30 */
} LlamaView;

typedef struct HunterView {         /* think 08 */
    int16_t unused_12;          /* +12 */
    int16_t quarry;             /* +14 slot of its llama; -1 = pick one, -2 = go to the companion */
    int16_t hit_frames;         /* +16 frames of the "hit" sprite left */
    int16_t unused_18_30[13];   /* +18..+30 */
} HunterView;

typedef struct MotherView {         /* think 0b mother, 0c shooter */
    int16_t unused_12;          /* +12 */
    int16_t turn_timer;         /* +14 ticks to the next random direction */
    union {                     /* +16 */
        int16_t start_delay;    /*     0b: counts down while >= 0 before the first drop */
        ByteCounter fire;       /*     0c: aimed shots */
    };
    ByteCounter drop;           /* +18 0b: between drops */
    int16_t drops_left;         /* +1a 0b */
    int16_t sprite_base;        /* +1c 0b */
    int16_t unused_1e_30[10];   /* +1e..+30 */
} MotherView;

typedef struct HerderView {         /* think 0e, 15, 16 */
    int16_t unused_12;          /* +12 */
    int16_t target;             /* +14 llama slot; HERDER_SEEK / HERDER_RETURN */
    int16_t unused_16;          /* +16 */
    int16_t hp;                 /* +18 hit points (hit 03) */
    ByteCounter step;           /* +1a one step per period */
    int16_t sprite_base;        /* +1c 8 directions x 2 frames */
    int16_t kill_action;        /* +1e */
    int16_t mode;               /* +20 what it does to the llama (CS:3AFF; DEVIATIONS #26) */
    ByteCounter spawn;          /* +22 16: leaves a think 17 chaser */
    int16_t unused_24_30[7];    /* +24..+30 */
} HerderView;

typedef struct HomingView {         /* the mover: think 10..13, 1d, 1f, 23, 28, 2a, enemy shots */
    int16_t unused_12;          /* +12 */
    Fixed16 velocity_x;         /* +14 (think 1d picks the head's frame by the sign of
                                   (int16_t)velocity_x.frac, as the original tests +14) */
    Fixed16 velocity_y;         /* +18 */
    Fixed16 accel_x;            /* +1c */
    Fixed16 accel_y;            /* +20 */
    union {                     /* +24 */
        int16_t sprite_base;    /*     10: 4-frame animation */
        ByteCounter beam;       /*     11 / 12: beam on while left != 0 */
        ByteCounter drop;       /*     13: between projectiles */
        ByteCounter egg_hits;   /*     1d: hits per segment turned into an egg */
        int16_t flash_frames;   /*     1f / 28: counts down while >= 0 */
        ByteCounter hits;       /*     23: left = hits left */
        int16_t gun_hits_left;  /*     2a */
    };
    Fixed16 position_x;         /* +26 whole = x */
    Fixed16 position_y;         /* +2a whole = y */
    union {                     /* +2e */
        ByteCounter pause;      /*     11 / 12: between beams */
        int16_t drops_left;     /*     13 */
        int16_t hits_left;      /*     1f / 28 */
        int16_t segment_family; /*     1f segments */
        ByteCounter fire;       /*     2a: between children */
        int16_t unused_2e;      /*     1d, 23: cleared at init, never read */
    };
    int16_t start_delay;        /* +30 no steering while >= 0 */
} HomingView;

typedef struct ChaserSteer {
    int8_t axis;                /* 1 = x, 0 = y, < 0 = start with x */
    uint8_t keep_chance;        /* out of 256, per tick */
} ChaserSteer;

typedef struct ChaserView {         /* think 17; anim 0e draws the trail */
    ChaserSteer steer;          /* +12 */
    int16_t newest;             /* +14 byte offset 0/4/8/12 of the newest trail entry */
    Point16 trail[4];           /* +16..+25 */
    int16_t unused_26_30[6];    /* +26..+30 */
} ChaserView;

typedef struct OrbitAngle {
    uint8_t angle;              /* low byte */
    uint8_t speed;              /* high byte, added to angle every tick */
} OrbitAngle;

typedef struct OrbitView {          /* think 20 core, 21 satellite */
    int16_t unused_12;          /* +12 */
    OrbitAngle orbit;           /* +14 */
    int16_t hits_left;          /* +16 */
    union {                     /* +18 */
        ByteCounter launch;     /*     20: a think 01 spawn per period */
        ByteCounter fire;       /*     21: fires at one phase */
    };
    int16_t hit_frames;         /* +1a */
    int16_t unused_1c_30[11];   /* +1c..+30 */
} OrbitView;

typedef struct EdgeRunnerView {     /* think 22 */
    int16_t unused_12;          /* +12 */
    int16_t axis;               /* +14 0 = x, 1 = y */
    int16_t unused_16;          /* +16 */
    int16_t backwards;          /* +18 0 = towards larger coordinates */
    int16_t intro_frames;       /* +1a counts down while >= 0 */
    Fixed16 position_x;         /* +1c */
    Fixed16 position_y;         /* +20 */
    Fixed16 speed;              /* +24 falls by decel every tick */
    int16_t decel;              /* +28 */
    int16_t unused_2a_30[4];    /* +2a..+30 */
} EdgeRunnerView;

typedef struct SeekerView {         /* think 24 */
    int16_t unused_12_14[2];    /* +12, +14 */
    int16_t direction;          /* +16 0..7 */
    ByteCounter turn;           /* +18 one 45-degree turn per period */
    int16_t sprite_base;        /* +1a */
    int16_t unused_1c_30[11];   /* +1c..+30 */
} SeekerView;

typedef struct ChargerView {        /* think 25, 29 */
    int16_t unused_12;          /* +12 */
    int16_t charging;           /* +14 1 while charging, 0 while moving */
    int16_t dash_reach;         /* +16 how far along the line to the target the dash aims */
    ByteCounter charge;         /* +18 length of the charge */
    ByteCounter spawn;          /* +1a between spawns */
    int16_t callback;           /* +1c what a spawn becomes (ENTITY_CALLBACK_*) */
    int16_t unused_1e;          /* +1e */
    int16_t sprite_base;        /* +20 */
    ByteCounter move;           /* +22 length of a movement phase */
    int16_t dash_frames;        /* +24 counts down while >= 0 */
    int16_t unused_26_30[6];    /* +26..+30 */
} ChargerView;

typedef struct DropperView {        /* think 26 */
    int16_t unused_12;          /* +12 */
    Fixed16 height;             /* +14 whole = y */
    Fixed16 fall_speed;         /* +18 */
    int16_t state;              /* +1c DROPPER_FALLING / RISING / WAITING */
    int16_t unused_1e_30[10];   /* +1e..+30 */
} DropperView;

typedef struct EdgeSpawnerView {    /* think 27 */
    int16_t unused_12;          /* +12 */
    int16_t stopped;            /* +14 1 once it touched a ship */
    ByteCounter throw_child;    /* +16 between children */
    int16_t unused_18_30[13];   /* +18..+30 */
} EdgeSpawnerView;

typedef struct PickupView {         /* think 1c */
    int16_t unused_12;          /* +12 */
    int16_t pickup_kind;        /* +14 effect on touch (9 = deadly) */
    int16_t sprite_base;        /* +16 2-frame blink */
    int16_t life;               /* +18 ticks before it disappears */
    int16_t unused_1a_30[12];   /* +1a..+30 */
} PickupView;

typedef struct SegmentView {        /* think 1e, the snake's body */
    int16_t unused_12;          /* +12 */
    int16_t steps_behind;       /* +14 on snake_trail */
    int16_t unused_16;          /* +16 */
    int16_t flash_frames;       /* +18 counts down while >= 0 */
    int16_t unused_1a_30[12];   /* +1a..+30 */
} SegmentView;

typedef struct StormView {          /* think 2b -> 2c storm, 2d bolt */
    int16_t unused_12;          /* +12 */
    union {                     /* +14 */
        int16_t strikes_left;   /*     2c */
        ByteCounter frame;      /*     2d: one sprite step per period */
    };
    union {                     /* +16 */
        int16_t hits_left;      /*     2b: until it becomes a storm */
        ByteCounter strike;     /*     2c: between strikes */
    };
    int16_t unused_18_30[13];   /* +18..+30 */
} StormView;

typedef struct RawView {            /* layout copies only */
    uint16_t words[16];         /* +12..+31 */
} RawView;

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_ENTITY_VIEWS_H */
