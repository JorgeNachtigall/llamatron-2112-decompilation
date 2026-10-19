/*
 * init.h - the state game86 is in when its timer starts.
 *
 * game86 never resets most of its globals: they start with the values its data segment is
 * loaded with, and many are only overwritten later (by a level script, a wave start, or not at
 * all). init_image_values() gives GameState those starting values, init_load_data() what
 * data_load (1000:60aa) reads from disk.
 */
#ifndef LLAMATRON_GAME_INIT_H
#define LLAMATRON_GAME_INIT_H

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The DGROUP image's initial values of the fields the game reads before writing them. */
void init_image_values(GameState *game);

/* game86 1000:60aa: the high-score table: the host's scores.yak (saved_scores, SCORES_YAK_SIZE
 * bytes, or NULL) if the game would accept it, else the user's extracted one, else the built-in
 * table. The pictures and sprites it also loads are extracted data. */
void init_load_data(GameState *game, const uint8_t *saved_scores);

#ifdef __cplusplus
}
#endif

#endif
