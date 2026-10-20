/*
 * entity_layout.h - pins every member of Entity and of its views (entity_views.h) to the
 * original record offset, so a drifted union fails the build, and defines EntityWords.
 * Included by state.h right after Entity.
 */
#ifndef LLAMATRON_GAME_ENTITY_LAYOUT_H
#define LLAMATRON_GAME_ENTITY_LAYOUT_H

#include <stddef.h>

#define ENTITY_AT(member, offset) \
    _Static_assert(offsetof(Entity, member) == (offset), "Entity." #member " at +" #offset)

_Static_assert(sizeof(Entity) == 64, "the original's 64-byte record");
_Static_assert(sizeof(RawMotion) == 4 && sizeof(RawView) == 32, "raw views");

/* fixed part */
ENTITY_AT(think, 0x00);
ENTITY_AT(hatch_into, 0x01);
ENTITY_AT(x, 0x02);
ENTITY_AT(y, 0x04);
ENTITY_AT(anim, 0x06);
ENTITY_AT(sprite, 0x0c);
ENTITY_AT(scale, 0x0e);
ENTITY_AT(solid, 0x10);
ENTITY_AT(overlay, 0x32);
ENTITY_AT(kind, 0x3a);
ENTITY_AT(death_sample, 0x3c);
ENTITY_AT(death_rate, 0x3d);
ENTITY_AT(hit_handler, 0x3e);

/* +08..+0b */
ENTITY_AT(vx, 0x08);
ENTITY_AT(vy, 0x0a);
ENTITY_AT(raw_motion.words, 0x08);
ENTITY_AT(snake_head.next_trail_slot, 0x08);
ENTITY_AT(snake_head.spawn, 0x0a);
ENTITY_AT(walker.fire, 0x08);
ENTITY_AT(growth.sprite_base, 0x08);
ENTITY_AT(child_velocity, 0x08);

/* +12..+31: the general names */
ENTITY_AT(raw.words, 0x12);
ENTITY_AT(aux12, 0x12);
ENTITY_AT(timer, 0x14);
ENTITY_AT(aux16, 0x16);
ENTITY_AT(hp, 0x18);
ENTITY_AT(aux1a, 0x1a);
ENTITY_AT(aux1c, 0x1c);
ENTITY_AT(kill_action, 0x1e);
ENTITY_AT(sub_mode, 0x20);
ENTITY_AT(reload, 0x22);
ENTITY_AT(countdown, 0x23);
ENTITY_AT(aux24, 0x24);
ENTITY_AT(aux26, 0x26);
ENTITY_AT(anim_toggle, 0x28);
ENTITY_AT(aux2a, 0x2a);
ENTITY_AT(sprite_ref, 0x2c);
ENTITY_AT(aux2e, 0x2e);
ENTITY_AT(aux30, 0x30);

/* +12..+31: the views (docs/entity-fields.md §5) */
ENTITY_AT(llama.turn_timer, 0x14);
ENTITY_AT(llama.trail_lag, 0x14);
ENTITY_AT(llama.facing, 0x16);
ENTITY_AT(llama.claimed, 0x18);
ENTITY_AT(llama.ship, 0x1e);
ENTITY_AT(llama.sprite_base, 0x30);

ENTITY_AT(hunter.quarry, 0x14);
ENTITY_AT(hunter.hit_frames, 0x16);

ENTITY_AT(mother.turn_timer, 0x14);
ENTITY_AT(mother.start_delay, 0x16);
ENTITY_AT(mother.fire, 0x16);
ENTITY_AT(mother.drop, 0x18);
ENTITY_AT(mother.drops_left, 0x1a);
ENTITY_AT(mother.sprite_base, 0x1c);

ENTITY_AT(herder.target, 0x14);
ENTITY_AT(herder.hp, 0x18);
ENTITY_AT(herder.step, 0x1a);
ENTITY_AT(herder.sprite_base, 0x1c);
ENTITY_AT(herder.kill_action, 0x1e);
ENTITY_AT(herder.mode, 0x20);
ENTITY_AT(herder.spawn, 0x22);

ENTITY_AT(homing.velocity_x, 0x14);
ENTITY_AT(homing.velocity_y, 0x18);
ENTITY_AT(homing.accel_x, 0x1c);
ENTITY_AT(homing.accel_y, 0x20);
ENTITY_AT(homing.sprite_base, 0x24);
ENTITY_AT(homing.beam, 0x24);
ENTITY_AT(homing.drop, 0x24);
ENTITY_AT(homing.egg_hits, 0x24);
ENTITY_AT(homing.flash_frames, 0x24);
ENTITY_AT(homing.hits, 0x24);
ENTITY_AT(homing.gun_hits_left, 0x24);
ENTITY_AT(homing.position_x, 0x26);
ENTITY_AT(homing.position_y, 0x2a);
ENTITY_AT(homing.pause, 0x2e);
ENTITY_AT(homing.drops_left, 0x2e);
ENTITY_AT(homing.hits_left, 0x2e);
ENTITY_AT(homing.segment_family, 0x2e);
ENTITY_AT(homing.fire, 0x2e);
ENTITY_AT(homing.start_delay, 0x30);

ENTITY_AT(chaser.steer, 0x12);
ENTITY_AT(chaser.newest, 0x14);
ENTITY_AT(chaser.trail, 0x16);
ENTITY_AT(chaser.trail[3].x, 0x22);

ENTITY_AT(orbit.orbit, 0x14);
ENTITY_AT(orbit.hits_left, 0x16);
ENTITY_AT(orbit.launch, 0x18);
ENTITY_AT(orbit.fire, 0x18);
ENTITY_AT(orbit.hit_frames, 0x1a);

ENTITY_AT(edge_runner.axis, 0x14);
ENTITY_AT(edge_runner.backwards, 0x18);
ENTITY_AT(edge_runner.intro_frames, 0x1a);
ENTITY_AT(edge_runner.position_x, 0x1c);
ENTITY_AT(edge_runner.position_y, 0x20);
ENTITY_AT(edge_runner.speed, 0x24);
ENTITY_AT(edge_runner.decel, 0x28);

ENTITY_AT(seeker.direction, 0x16);
ENTITY_AT(seeker.turn, 0x18);
ENTITY_AT(seeker.sprite_base, 0x1a);

ENTITY_AT(charger.charging, 0x14);
ENTITY_AT(charger.dash_reach, 0x16);
ENTITY_AT(charger.charge, 0x18);
ENTITY_AT(charger.spawn, 0x1a);
ENTITY_AT(charger.callback, 0x1c);
ENTITY_AT(charger.sprite_base, 0x20);
ENTITY_AT(charger.move, 0x22);
ENTITY_AT(charger.dash_frames, 0x24);

ENTITY_AT(dropper.height, 0x14);
ENTITY_AT(dropper.fall_speed, 0x18);
ENTITY_AT(dropper.state, 0x1c);

ENTITY_AT(edge_spawner.stopped, 0x14);
ENTITY_AT(edge_spawner.throw_child, 0x16);

ENTITY_AT(pickup.pickup_kind, 0x14);
ENTITY_AT(pickup.sprite_base, 0x16);
ENTITY_AT(pickup.life, 0x18);

ENTITY_AT(segment.steps_behind, 0x14);
ENTITY_AT(segment.flash_frames, 0x18);

ENTITY_AT(storm.strikes_left, 0x14);
ENTITY_AT(storm.frame, 0x14);
ENTITY_AT(storm.hits_left, 0x16);
ENTITY_AT(storm.strike, 0x16);

#undef ENTITY_AT

/* A whole record as the original's 32 words (C11 union punning), for code that copies
 * records by layout: attract_end's overrun of the title llamas and tests/adapters only
 * (cmake/tests/NoDsOffsets.cmake keeps it there). */
typedef union EntityWords {
    Entity entity;
    uint16_t words[32];
} EntityWords;
_Static_assert(sizeof(EntityWords) == sizeof(Entity), "EntityWords is the record");

#endif /* LLAMATRON_GAME_ENTITY_LAYOUT_H */
