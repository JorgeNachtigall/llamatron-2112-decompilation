# Entity record fields: census and named views

The census covers what every handler keeps in the 64-byte entity record (`Entity`, `src/game/state.h`, game86
DS:2CF4 + 64 * slot). It is the groundwork for replacing the offset names (`aux12` .. `aux30`) and
the general names that some types use for something else (`timer`, `hp`, `kill_action`,
`sub_mode`, `reload`/`countdown`, `anim_toggle`, `sprite_ref`, `vx`/`vy`) with named per-kind
views. Section 7 has the proposal; section 8 lists what needs agreement.

## 1. Method

- **Machine census.** A throwaway script (not checked in) parsed every function in
  `src/game/**.c` with comments and strings stripped. It recorded each read and write of an
  Entity field, per entity *parameter*: a handler's own record is tracked separately from the
  children it spawns. The script then followed the calls that pass that same record on (for
  example `think_13` -> `entity_homing_move`). The result was summed per entry of
  `entity_think_handlers`, `entity_anim_handlers`, `entity_hit_handlers`, `kill_actions` and
  `enemy_init_handlers`, and per enemy type over the init closure (literal `anim`,
  `hit_handler`, `kill_action`, `sub_mode` and `entity_set_type` values).
- **Meanings.** They come from the port's own comments and code in `src/game/entities/*.c`
  (each section names its file), checked against the game86 addresses cited there.
- **Sizes.** The code base had 41 enemy types, 46 think, 19 anim and 18 hit handlers, and
  12 kill actions at commit 9f47591 + e372e0a.
- **Accuracy.** The census can only be as right as the port's comments. Where a comment and
  the code disagreed, this file follows the code and says so.

## 2. The fixed part of the record

These fields mean the same thing for every kind. They stay plain members.

| off | field | meaning |
|-----|-------|---------|
| +00 | `think` / `hatch_into` | think handler; think it becomes on hatching (both 0xFF = free slot) |
| +02/+04 | `x`, `y` | sprite centre |
| +06 | `anim` | animate handler |
| +0c | `sprite` | sprite number, < 0 = drawn from `sprite_ref` |
| +0e | `scale` | draw scale; the hatch countdown of a spawn marker (think 00/04/07/0a), the growth of think 02 |
| +10 | `solid` | shots and ships collide only when non-zero |
| +32..+38 | `overlay` | overlay sprite |
| +3a | `kind` | roster kind |
| +3c/+3d | `death_sample`, `death_rate` | death sound |
| +3e | `hit_handler` | hit handler |

The variable part has two regions:

- **+08..+0b.** Velocity for every mover (`entity_move_bounce`, the shots, debris). Four kinds
  keep something else there: the snake head, think 23, think 1f/28 and their segments, and
  think 2a. See §4.
- **+12..+31** (32 bytes). Each family keeps its own state here, including the general names
  `timer`, `hp`, `kill_action`, `sub_mode`, `reload`/`countdown`, `anim_toggle` and
  `sprite_ref`.

`hp` (+18) and `kill_action` (+1e) are real hit points and kill-action index only for kinds
whose hit handler is 03 or 07 (`ent_hit_03` decrements `hp`, then runs `kill_action`). Other
kinds use those words freely.

## 3. Enemy types (tbl_enemy_init, DS:2CA4)

`spawn -> hatch`: every type except 0x10 starts as a spawn marker (think 04/07/0a = think_00:
`scale` counts the hatch) and hatches into `hatch_into`. Types 05..09, 0e, 0f, 11..20 and
22..28 start directly.

| type | think (hatched) | anim | hit | kill | view (§5) | notes |
|------|-----------------|------|-----|------|-----------|-------|
| 00 | 04 -> 05 | 0f | 00 | - | egg -> llama | the egg hands its sprite base to the llama in +30 |
| 01 | 03 | 01 | 03 / script | script | swarm | sprite from `sprite_ref` (llama by wave) |
| 02 | 07 -> 08 | 0c | 01 | - | hunter | |
| 03 | 0a -> 0b | 0c | 02 | - | mother | `init_think0a_spawn(0x3c, 0x12c, 0x60, 3)` |
| 04 | 07 -> 0e | 0c | 03 | 01 | herder | leaves +20 (mode) stale: DEVIATIONS #26 |
| 05..07 | 10 | 0d | 04/05/00 | 0 | homing (shot) | `init_as_projectile`: an enemy shot record (§6) |
| 08 | 11 | 02 | 06 | 0 | homing + beam (horizontal) | drifter: start at rest, accel = speed |
| 09 | 12 | 03 | 06 | 0 | homing + beam (vertical) | |
| 0a | 07 -> 13 | 0c | 00 | accel | homing + dropper | accel x = tuning accumulator |
| 0b | 14 | 0d | 07 | - | bouncer | |
| 0c | 07 -> 15 | 0c | 03 | 02 | herder (hums) | |
| 0d | 07 -> 16 | 0c | 03 | 0 | herder (leaves chasers) | mode 1 |
| 0e | 17 | 0d | 00 | - | chaser | +1e/+20 are the trail's first x/y |
| 0f | 18 | 0d | 03 | 03 | blinker | releases think 19 bouncers |
| 10 | 1d + 1e x n | 0d | 08 / - | - | snake head + segments | `enemy_init_10` builds the snake |
| 11..14 | 1f | 0d | 0a | 0 | homing + growth (segment family) | `init_segment` |
| 15 | 20 | 0d | 0b | - | orbit core | |
| 16 | 21 | 0d | 0b | - | orbit satellite | |
| 17 | 22 | 0d | 0b | x | edge runner | |
| 18, 26 | 0e | 0d | 03 | 0 | herder | modes 2 / 3 |
| 19 | 23 | 0d | 0c | 0 | homing + walker | `vx` is the fire counter |
| 1a, 27 | 24 | 0d | 00 | - | seeker | `enemy_setup_type24`; also laid by think 23 and kill action 0a |
| 1b, 1e | 25 | 0d | 0d | - | charger | callback 33E3 / 32D7 in +1c |
| 1c | 26 | 0d | - | - | dropper | |
| 1d | 27 | 0d | - | - | edge spawner | |
| 1f | 1f / 28 | 0d | 0a | 0 | homing + growth | |
| 20 | 29 | 0d | 00 | - | charger | callback 3119 (`init_think25`, then think 29 and hit 00) |
| 21 | 0a -> 0b | 0c | 02 | - | mother | `init_think0a_spawn(0x122, 0x1e, 0x30, 0x14)` |
| 22..25 | 2a | 0d | 0f | accel | homing + gun | `init_edge_runner` (enemy_init.c calls them edge runners; not think 22): start on an edge, `vx`/`vy` = the inward direction, which is also the child velocity |
| 28 | 2b -> 2c (hit 11) | 11 | 11 | - | storm | think 2b does nothing until enough hits |

Further think handlers that no init sets directly:

- 01: think 01, static.
- 02: fade.
- 06: follower (a rescued llama).
- 09: timer-lived.
- 0d: straight shot.
- 0f / 19 / 1a / 1b: bouncers, debris and rocks.
- 1c: pickup.
- 1e: snake segment.
- 2d: lightning bolt.

## 4. Region +08..+0b

| view | +08 | +0a | written by | read by |
|------|-----|-----|-----------|---------|
| default (`vx`, `vy`) | velocity x | velocity y | everywhere | `entity_move_bounce`, think 0d/0f/14/19/1a/1b/1c, shots (`fire_back`, hit 01/06/07/0d) |
| snake head (think 1d) | `next_trail_slot`: the `snake_trail` slot written next (then + 1, & 63) | `spawn`: byte counter (lo period, hi left), one segment step | `enemy_init_10` | `think_1d` (think_snake.c) |
| walker (think 23) | `fire`: byte counter | - | `enemy_init_19` | `think_23` |
| growth (think 1f/28) | `sprite_base`: the facing sprites (vx = `e->sprite` at init) | - | `init_segment`, `ent_hit_0e` | `think_1f` |
| gun (think 2a) | `child_v.x` | `child_v.y`: the velocity its child is fired with | `init_edge_runner` | `think_2a` |

## 5. Region +12..+31, by view

Notation:

- **byte counter** = a word whose low byte is the period and whose high byte counts down. The
  helpers `pair_count_down`, `pair_passed_zero`, `byte_pair_expired` and `pair_elapsed` differ
  in whether they fire at < 0 or at 0. Keep each one's rule.
- **16.16** = a 32-bit fixed-point value in two words, low word (fraction) at the lower offset.
- Offsets the view does not use are left out of its table.

### 5.1 egg -> llama -> follower (think 00/04, 05, 06; think_llamas.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `turn_timer` (05) / `trail_lag` (06) | 05: ticks to the next random turn. 06: byte offset (8 per entry) behind its ship's newest trail point |
| +16 | `facing` | 05: sprite offset of the direction: 0, 2, 4 or 6 (`wander_sprite[]` while wandering, 0 / 6 while walking to its ship) |
| +18 | `claimed` (= `hp`) | 0 = free; herders and hunters claim a llama by setting 1 |
| +1e | `ship` (06) | which ship's trail it follows (FOLLOW_P1/P2; the original stores the trail's DS address) |
| +30 | `sprite_base` | the llama's sprites; the egg (`enemy_setup_00`) stores `spawn_sprite` here and the llama keeps it |

### 5.2 hunter (think 08; think_llamas.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `quarry` | slot of the llama it is after; -1 = pick one, QUARRY_NONE (-2) = go to the companion |
| +16 | `hit_frames` | frames of the "hit" sprite left (`ent_hit_01` sets 5) |

### 5.3 mother and shooter (think 0b, 0c; think_spawners.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `turn_timer` | ticks to the next random direction |
| +16 | `start_delay` (0b) / `fire` (0c) | 0b: counts down while >= 0 before the first drop. 0c: byte counter of aimed shots |
| +18 | `drop` (0b) | byte counter between drops |
| +1a | `drops_left` (0b) | shooters still to drop |
| +1c | `sprite_base` (0b) | 4-frame animation base |

### 5.4 herder (think 0e, 15, 16; think_herders.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `target` | slot of the llama being fetched; HERDER_SEEK (-1) = pick one; HERDER_RETURN = back to the companion |
| +18 | `hp` | hit points (hit 03), the general meaning |
| +1a | `step` | byte counter: one step per period |
| +1c | `sprite_base` | 8 directions x 2 frames |
| +1e | `kill_action` | general meaning |
| +20 | `mode` | what it does with the llama (CS:3AFF; 0..3; stale from the slot for type 04, DEVIATIONS #26) |
| +22 | `spawn` (16) | byte counter (`reload` = period from `script_param[5]`, `countdown` = left) for leaving a think 17 chaser |

### 5.5 homing mover (think 10, 11, 12, 13, 1d, 1f, 23, 28, 2a; think_homing.c and the users)

This is the largest family. It covers enemy shots, drifters, segments, the snake head, the
walker and the gun.

| off | name | meaning |
|-----|------|---------|
| +14/+16 | `velocity_x` | 16.16 (old names `timer`, `aux16`) |
| +18/+1a | `velocity_y` | 16.16 (`hp`, `aux1a`) |
| +1c/+1e | `accel_x` | 16.16 (`aux1c`, `kill_action`); think 1f/28 grow it by `tuning.p0826` every tick |
| +20/+22 | `accel_y` | 16.16 (`sub_mode`, `reload`/`countdown`) |
| +24 | per think, see below | |
| +26/+28 | `position_x` | 16.16, whole part = x (`aux26`, `anim_toggle`) |
| +2a/+2c | `position_y` | 16.16, whole part = y (`aux2a`, `sprite_ref`) |
| +2e | per think, see below | |
| +30 | `start_delay` | no steering while >= 0, counting down (shots: 3..10; `ent_hit_06` sets 5 after a knock-back) |

The +24 and +2e words, per think:

| think | +24 | +2e |
|-------|-----|-----|
| 10 (shots 05..07) | `sprite_base` (4-frame animation) | - |
| 11 / 12 (beams 08/09) | `beam`: byte counter, beam on while the high byte != 0 | `pause`: byte counter between beams |
| 13 (type 0a) | `drop`: byte counter between projectiles | `drops_left` |
| 1d (snake head) | `egg_hits`: byte counter, hits per segment turned into an egg (`ent_hit_08`). The head's frame comes from the sign of `(int16_t)velocity_x.frac` (+14), as the original tests it; do not turn that into `.whole` | - |
| 1f / 28 (types 11..14, 1f) | `flash_frames`: counts down while >= 0 (hit 0a sets 0x14) | `hits_left` (`ent_hit_0a`); on a segment the segment family (`tuning.segment_family`) |
| 23 (type 19) | `hits`: byte counter, high byte = hits left (`ent_hit_0c`) | - |
| 2a (types 22..25) | `hits_left`: word (`ent_hit_0f`) | `fire`: byte counter between children |

Hit handlers on this family:

- **`ent_hit_06`** (beams) sets `velocity_x.whole` and `velocity_y.whole` to 4 x the shot's
  velocity, and `start_delay` to 5.
- **`ent_hit_0a`** restarts the mover:
  - `velocity_x.frac` = 4 x the shot's **y**;
  - `velocity_y.frac` = 4 x the shot's vx. That is the original's own choice (Thorn, from the
    listing): 2da7 stores [SI+2] (shot y) x 4 to +14 and [SI+4] (shot vx) x 4 to +18, probably
    meaning +4/+6 (vx/vy). Both land in the fraction words, so the knock-back is under
    1 px/tick. Diff-tested by test_hit and test_w5a_types;
  - both accelerations = 0x200;
  - +24 = 0x14 (flash).
- **`ent_hit_0e`** clears every word of the left-behind segment and stores the family in +2e.

### 5.6 chaser (think 17, anim 0e; think_chasers.c, anim.c)

| off | name | meaning |
|-----|------|---------|
| +12 | `steer` | low byte `axis` (1 = x, 0 = y, < 0 = start with x); high byte `keep_chance` (out of 256) |
| +14 | `newest` | byte offset 0/4/8/12 of the newest trail entry (index x 4) |
| +16..+25 | `trail[4]` | four `Point16` positions (x, y), newest at `newest / 4`; anim 0e (`anim_satellites`) draws a satellite at each. The fourth x is the `reload`/`countdown` byte pair |

`enemy_init_0e` writes the first entry (+1e/+20 = x, y, through `kill_action`/`sub_mode`).

### 5.7 blinker (think 18)

`timer` (+14) = 0/1, which of the two sprites shows (`sprite = timer + 0xb3`). There is no
other variable state.

### 5.8 orbit core and satellite (think 20, 21; think_orbiters.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `orbit` | low byte `angle`, high byte `speed` (angle += speed every tick) |
| +16 | `hits_left` | `ent_hit_0b` |
| +18 | `launch` (20) / `fire` (21) | byte counter: 20 launches a think 01 spawn; 21 fires at one phase of it |
| +1a | `hit_frames` | frames of the "hit" sprite left (`ent_hit_0b` sets 0x10) |

### 5.9 edge runner (think 22; think_orbiters.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `axis` | 0 = moving in x, 1 = in y (`timer ^= 1`) |
| +16 | `hits_left` | `ent_hit_0b` (2ae1): dies when it goes below 0; `enemy_init_17` (27e7) starts it at 1, so a runner dies on its second hit. The same hit restarts `intro_frames` (0x10) |
| +18 | `backwards` | 0 = towards larger coordinates (`hp`) |
| +1a | `intro_frames` | counts down while >= 0 |
| +1c/+1e | `position_x` | 16.16 (`kill_action` = whole x; `enemy_init_17` sets x there) |
| +20/+22 | `position_y` | 16.16 (`sub_mode`, `reload`/`countdown`) |
| +24/+26 | `speed` | 16.16, falls by `decel` every tick |
| +28 | `decel` | (`anim_toggle`) |

### 5.10 seeker (think 24; think_w5a.c)

| off | name | meaning |
|-----|------|---------|
| +16 | `direction` | 0..7 |
| +18 | `turn` | byte counter: one 45-degree turn per period (`hp` low = period) |
| +1a | `sprite_base` | `sprite = direction + sprite_base` (0x136) |

### 5.11 charger (think 25, 29; think_chargers.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `charging` | 1 while charging, 0 while moving |
| +16 | `dash_reach` | how far along the line to the target the dash velocity is taken (`line_walk`) |
| +18 | `charge` | byte counter: length of the charge |
| +1a | `spawn` | byte counter between spawns |
| +1c | `callback` | what a spawn becomes (ENTITY_CALLBACK_3119 / 32D7 / 33E3, CS addresses as values) |
| +20 | `sprite_base` | |
| +22 | `move` | byte counter (`reload` = period, `countdown` = left) of the movement phase |
| +24 | `dash_frames` | frames of the dash sprite left, counts down while >= 0 |

### 5.12 dropper (think 26; think_w5a.c)

| off | name | meaning |
|-----|------|---------|
| +14/+16 | `height` | 16.16, whole part = y (`timer`, `aux16`) |
| +18/+1a | `fall_speed` | 16.16 (`hp`, `aux1a`), + gravity 0x2000 per tick |
| +1c | `state` | DROPPER_FALLING / RISING / WAITING; any value other than FALLING and WAITING counts as rising |

On ticks that `tuning.p0830` masks out, the dropper also creeps one pixel towards the target's
x, except when `height.whole` == 1.

### 5.13 edge spawner (think 27; think_w5a.c)

`timer` (+14) is `stopped`: set to 1 once it touches a ship. +16 is `throw`, a byte counter
between children.

### 5.14 pickup (think 1c; think_basic.c, hit.c `become_pickup`, think_chargers.c 3119)

| off | name | meaning |
|-----|------|---------|
| +14 | `pickup_kind` | effect on touch (`pickup_effects[]`; 9 = deadly) |
| +16 | `sprite_base` | 2-frame blink base |
| +18 | `life` | ticks before it disappears (200, or 0x64 from callback 3119) |

### 5.15 snake segment (think 1e; think_snake.c)

| off | name | meaning |
|-----|------|---------|
| +14 | `steps_behind` | how far back on `snake_trail` it sits |
| +18 | `flash_frames` | counts down while >= 0 |

### 5.16 storm and bolt (think 2b -> 2c, 2d; think_orbiters.c, hit.c `ent_hit_11`)

| off | name | meaning |
|-----|------|---------|
| +14 | `unused_14` (2b) / `strikes_left` (2c) / `frame` (2d) | 2b: `enemy_init_28` (26c5) writes 0x2828, never read (think 2b is a bare RET and `ent_hit_11` overwrites it). 2c: strikes until the storm ends (0x96 from `ent_hit_11`). 2d: byte counter, one sprite step per period |
| +16 | `hits_left` (2b) then `strike` (2c) | 2b: hits until it becomes a storm. 2c: byte counter between strikes (0x404) |

### 5.17 the rest

These use no variable state beyond the default region +08:

- think 01;
- think 02 (`scale` grows);
- think 03, the swarm: `timer` = re-aim countdown, `anim_toggle` = frame;
- think 09 (`timer` = life);
- think 0d, 0f, 14, 19, 1a, 1b;
- think 2b, until hit.

Type 01 also keeps the llama frame's bank offset in `sprite_ref` (§7.4).

## 6. Records that are not wave types

- **Enemy shots** (`enemy_shot_init`, spawn.c; also types 05..07). They use the homing view
  with think 10:
  - `EnemyShot.aim_y` (0419) goes to +16 = `velocity_x.whole`, and `aim_x` (041B) to +1a =
    `velocity_y.whole`. The names describe the source, not where the mover uses them:
    `fire_back` (1108/11e2) stores the shot's vy x 2 in 0419 and its vx x 2 in 041B, so a
    fire-back shot flies with its axes transposed (the original reflects across the
    diagonal). Thorn renames them to `velocity_x` / `velocity_y` and comments the swap in
    `fire_back`, together with his view conversion.
  - `speed` goes to both accelerations' low words, and `sprite` to +24 (`sprite_base`).
  - `start_delay` = 3..10.
  - `enemy_shot_drop` (think 13) makes the same record.
- **Straight shots and children** (`entity_spawn_child`, `entity_init_child`): only `vx`, `vy`,
  `scale` and the fixed part.
- **Debris and rocks** (kill actions 01..0b, `entity_spawn_debris`): `vx`, `vy`, `scale`.
  - Rocks (kill action 03) get `hp` 3 and `kill_action` 4, the general meanings.
  - Kill action 08 re-runs `enemy_setup_0c` on the record (the mother view).
  - Kill action 0a lays seekers.
- **Title llamas** (attract.c `entity_record_word_set` / `attract_end`): the original's
  overrun writes the title-llama records word by word over entity slots 0..8. The port
  reproduces that through a switch over word numbers. It is the one place that needs a word
  view of the record (§7.3).
- **Players and player shots** are not Entity records. `Ship` and `Shot` (16 bytes,
  DS:68C9) live in GameState. `Shot.aux0a` is written only by the stray `enemy_shot_init`
  write in `fire_back` (zeroed), and `Shot.aux0e` is never used. Both are Keel's, in state.h:
  name them `unused_0a` / `unused_0e` with that note.

## 7. Proposed design

### 7.1 Shape

The record keeps its fixed 64-byte layout (`sizeof(Entity) == 64`, every member at its
original offset). The two variable regions become C11 anonymous unions:

- One member of each union is an anonymous struct with today's general names, so existing
  code keeps compiling unchanged.
- The other members are named per-kind views.

```c
typedef struct ByteCounter {    /* a word used as two bytes (low byte first, as in memory) */
    uint8_t period;             /* reload value */
    uint8_t left;               /* counts down */
} ByteCounter;

typedef struct Fixed16 {        /* 16.16 kept in two words, fraction first */
    uint16_t frac;
    int16_t whole;
} Fixed16;                      /* alignment 2: sits at +26 / +2a as well */
static inline int32_t fixed16_get(Fixed16 f);
static inline void fixed16_set(Fixed16 *f, int32_t v);

typedef struct HomingView {     /* think 10..13, 1d, 1f, 23, 28, 2a: +12..+31 */
    int16_t unused_12;
    Fixed16 velocity_x, velocity_y, accel_x, accel_y;          /* +14 +18 +1c +20 */
    union {                                                    /* +24 */
        int16_t sprite_base;    /* 10 */
        ByteCounter beam;       /* 11/12 */
        ByteCounter drop;       /* 13 */
        ByteCounter egg_hits;   /* 1d */
        int16_t flash_frames;   /* 1f/28 */
        ByteCounter hits;       /* 23 */
        int16_t gun_hits_left;  /* 2a */
    };
    Fixed16 position_x, position_y;                            /* +26 +2a */
    union {                                                    /* +2e */
        ByteCounter pause;      /* 11/12 */
        int16_t drops_left;     /* 13 */
        int16_t hits_left;      /* 1f/28 */
        int16_t segment_family; /* 1f segments */
        ByteCounter fire;       /* 2a */
    };
    int16_t start_delay;                                       /* +30 */
} HomingView;
/* likewise LlamaView, HunterView, MotherView, HerderView, ChaserView, OrbitView,
 * EdgeRunnerView, SeekerView, ChargerView, DropperView, EdgeSpawnerView, PickupView,
 * SegmentView, StormView: one struct per section of §5, each exactly 32 bytes */

typedef struct Entity {
    int8_t think, hatch_into;
    int16_t x, y, anim;
    union {                                                     /* +08 */
        struct { int16_t vx, vy; };
        struct { int16_t next_trail_slot; ByteCounter spawn; } snake_head;
        struct { ByteCounter fire; int16_t unused_0a; } walker;
        struct { int16_t sprite_base; int16_t unused_0a; } growth;
        Point16 child_velocity;                                 /* think 2a */
    };
    int16_t sprite, scale, solid;
    union {                                                     /* +12..+31 */
        struct {                /* the general names, until nothing needs them */
            int16_t aux12, timer, aux16, hp, aux1a, aux1c, kill_action, sub_mode;
            uint8_t reload, countdown;
            int16_t aux24, aux26, anim_toggle, aux2a, sprite_ref, aux2e, aux30;
        };
        LlamaView llama;  HunterView hunter;  MotherView mother;  HerderView herder;
        HomingView homing; ChaserView chaser; OrbitView orbit; EdgeRunnerView edge_runner;
        SeekerView seeker; ChargerView charger; DropperView dropper;
        EdgeSpawnerView edge_spawner; PickupView pickup; SegmentView segment; StormView storm;
    };
    struct { int16_t x, y, sprite, frames; } overlay;
    int16_t kind;
    uint8_t death_sample, death_rate;
    int16_t hit_handler;
} Entity;

_Static_assert(sizeof(Entity) == 64, "the original's record");
_Static_assert(offsetof(Entity, homing.position_x) == 0x26, "+26");
/* ... one assert per named field, generated from the tables of §5 */
```

Usage reads like this:

```c
e->homing.start_delay--;
fixed16_set(&e->homing.velocity_x, v);
byte_counter_tick(&e->charger.spawn);
e->herder.mode;
e->chaser.trail[i];
e->llama.sprite_base;
```

### 7.2 Why it stays 1:1

- **Same bytes.** Every view overlays the same 32 (or 4) bytes, and the union keeps whatever
  the previous kind left. The stale-field behaviour the port reproduces on purpose (an init
  that "keeps whatever the slot held", DEVIATIONS #26, the egg handing +30 to the llama,
  kill action 08 re-initialising in place) therefore stays exact.
- **Two-byte members only.** Every view member is `int16_t`, `uint16_t`, two bytes or
  `Fixed16`/`ByteCounter` (alignment 2). No view adds padding, and the asserts pin each
  member's offset.
- **Byte order.** Reading a `ByteCounter` and the general `int16_t` at the same offset agree
  only on a little-endian host. That covers every target (x86-64, arm64, wasm32). state.h
  should say so with
  `#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__ #error`.
  The general struct goes away once every user has switched (§7.5).
- **The adapter** (agreed with Sentinel). `tests/adapters/dgroup_map.c` is a layout copy and
  must not depend on the kind: a slot's bytes often belong to a kind it no longer has. Two
  examples are the title llamas' leftovers (DEVIATIONS #26) and a reused slot that keeps a
  follower's stale trail (warp 625). Picking a view by the current kind would lose or misread
  those. So each union also gets a raw member:
  - `raw_motion.words[2]` over +08..+0b;
  - `raw.words[16]` over +12..+31.

  The adapter keeps the general names until they are scheduled to go, then maps the raw
  words, never the views. A raw-words round trip stays exact, byte pairs included.

### 7.3 Word access

attract.c `entity_record_word_set` and the adapter write records by word number on purpose:
one reproduces the original's overrun, the other is a memory map.
- The adapter maps the raw members (`raw_motion.words[2]`, `raw.words[16]`) and the fixed fields
  by name (Sentinel).
- attract.c uses `EntityWords` (Moss), a union of `Entity` and `uint16_t words[32]`, defined in
  entity_layout.h after Entity with a size assert. C11 union punning makes it one loop, without
  a 30-case switch.
- `offsetof` asserts cover the raw members (`raw_motion` at +08, size 4; `raw` at +12, size 32),
  so a drifted union fails the build, not the adapter's round-trip test.
- The source guard (`cmake/tests/NoDsOffsets.cmake`) fails on `raw.words`, `raw_motion.words`
  and `EntityWords` anywhere in src/game except their definitions and attract.c.

### 7.4 sprite_ref

`sprite_ref` at +2c has two lives:

- **A drawing input.** When `sprite < 0` the anim handlers draw the frame at bank offset
  `sprite_ref` (anim 00/04/05..07/09/0a/0c/0d/0f/10). Only type 01 (`enemy_setup_01`) sets
  it, to the llama-by-wave frame.
- **Scratch.** It is the high word of `homing.position_y`, and `remember_home` stores y there.

In the views the second life becomes `homing.position_y.whole`, so `sprite_ref` remains
only as the type 01 drawing input. Converting that from a bank offset to a frame or ref
index is Thorn's and Prism's allow-listed item in `NoDsOffsets.cmake`. It is independent of
this change, but it touches the same field.

### 7.5 Migration

1. W1 (me), one commit:
   - state.h gets `ByteCounter`, `Fixed16` with its two inline helpers, the views (in a new
     `src/game/entity_views.h` that state.h includes, so state.h stays readable), the two
     anonymous unions with the general struct, and the asserts.
   - No behaviour change and no caller changes.
   - A ctest builds a probe that checks every view offset (the asserts already do that at
     compile time) and that a round trip through `dgroup_map` is unchanged.
2. Owners switch their own files, one family per commit, with the oracle tests as the net:
   - Thorn: enemy_init.c, spawn.c, hit.c, kill_actions.c, think_w5a.c, pickups.c.
   - Moss: the think_* files listed in §5.
   - Prism: anim.c.
   - Keel: entity_loop.c, tick.c, main_sm.c where they touch the region.
   - Moss: attract.c.
   - The shared helpers `join32`/`split32` and the four byte-pair helpers move to
     `fixed16_*` / `byte_counter_*` in entity_views.h. Each keeps its own expiry rule
     (`_expired` < 0, `_elapsed` == 0) by name.
3. When no `aux*` name is left, the general struct loses them: its members become the views'
   padding plus `timer`/`hp`/`kill_action` for the kinds that use their general meaning.
   The guard can then fail on any `aux` member access.

## 8. Agreement needed

- **Keel** (state.h owner): agreed to §7 as written.
  - The little-endian guard goes in entity_views.h next to the unions, with its reason.
  - The header commit leaves state.h's general names untouched; Keel retires his own uses
    later.
  - `Shot.aux0a`/`aux0e` -> `unused_0a`/`unused_0e` is Keel's, in his DS-name audit commit.
- **Thorn**: agreed (§5.5, 5.10, 5.12..5.14 checked against the listing; one HomingView with
  per-think unions; the +08 views). He converts enemy_init.c, spawn.c, hit.c, kill_actions.c,
  pickups.c, think_w5a.c and bullets.c.
- **Moss**: agreed.
  - One HomingView, since `entity_homing_move` touches only the shared prefix.
  - The +08 views, with `next_trail_slot`.
  - The facing values and the snake head's frame test, noted in §5.1 / §5.5.
  - `EntityWords` for attract.c (§7.3).
  - He converts think_llamas, think_spawners, think_herders, think_homing, think_chasers,
    think_snake, think_orbiters, think_chargers, think_basic, movement.c and attract.c.
  - ent_hit_08's egg_hits is Thorn's (hit.c).
- **Sentinel** (adapter): agreed. Keep the field-by-field map through the general names for
  now. When the aux names are scheduled to go, Sentinel switches to `raw_motion.words[]` /
  `raw.words[]` in the same series, with `test_dgroup_map` and the golden replays as the net.
  llama_residue.py's labels get the view names when the header lands.
- **Open points** from the census, both settled by Thorn from the listing:
  - `ent_hit_0a` is the original's own choice (§5.5);
  - the `EnemyShot` field names get renamed (§6).
