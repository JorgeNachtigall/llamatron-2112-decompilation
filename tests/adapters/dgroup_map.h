/*
 * dgroup_map.h - GameState <-> game86 DGROUP bytes (docs/PLAN.md §0.1, §3.1).
 *
 * Test-only: compiled into llamatron_game_testlib, never into the game. This is the one
 * place in C that knows the original's byte layout; game code stays free of it.
 *
 * `dgroup` is always a full 64 KB image of the segment (DS offsets 0000..FFFF).
 *  - gamestate_from_dgroup fills every GameState field that has a DGROUP location.
 *    Fields without one (quit) are left as they are. spawn ([0x39EB]) and flash_list
 *    ([0x06C7]) are DS addresses into data the port loads from the extracted files: they are
 *    mapped only when game_data_load() succeeds (dgroup_map_has_data), and a cursor address
 *    outside every spawn list (0 before the first wave) leaves spawn as it was.
 *    spawn_pairs_clobbered is the state of the level-script bytes levels.dat names
 *    (spawn_pairs_clobber): set when they are all zero; to_dgroup zeroes them when set and leaves
 *    them otherwise (they still count as mapped in the mask).
 *  - gamestate_to_dgroup writes exactly the bytes dgroup_map_mask marks and leaves all
 *    other bytes untouched.
 * Some encodings are canonical rather than exact (a flag word reads as != 0 and is written
 * back as 1, score digits are reduced to 0..9, unused areas are written as zeros), so
 * to(from(x)) == x holds for canonical images; to(from(to(from(x)))) == to(from(x)) always.
 * Tests canonicalise random images with one round trip before using them.
 */
#ifndef LLAMATRON_TESTS_DGROUP_MAP_H
#define LLAMATRON_TESTS_DGROUP_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DGROUP_SIZE = 0x10000,
    DGROUP_MASK_UNMAPPED = 0,   /* not represented in GameState; never written */
    DGROUP_MASK_FIELD = 1,      /* a GameState field (exact for canonical values) */
    DGROUP_MASK_DERIVED = 2,    /* written but not stored: fixed zeros, mirrored flags */
};

void gamestate_from_dgroup(GameState *game, const uint8_t *dgroup);
void gamestate_to_dgroup(const GameState *game, uint8_t *dgroup);

/* Fills mask[DGROUP_SIZE] with DGROUP_MASK_* per byte. Returns the number of bytes two
 * fields both claim (0 unless the layout table is wrong). */
unsigned dgroup_map_mask(uint8_t *mask);

/* Whether the extracted data loaded, i.e. spawn and flash_list are mapped. */
bool dgroup_map_has_data(void);

/* For ctypes callers that do not mirror the struct. */
size_t gamestate_size(void);
/* Entity `index` (0..ENTITY_COUNT-1), or the companion for index -1. */
Entity *gamestate_entity(GameState *game, int index);

#ifdef __cplusplus
}
#endif

#endif
