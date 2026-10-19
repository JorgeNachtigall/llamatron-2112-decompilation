/*
 * keymap.h - SDL scancode -> IBM PC/AT keyboard scan code set 1 (make codes).
 * Break code = make code | 0x80. Codes are the standard XT/AT values, nothing game specific.
 */
#ifndef LLAMATRON_KEYMAP_H
#define LLAMATRON_KEYMAP_H

#include <SDL_scancode.h>
#include <stdint.h>

/* Returns 0 for keys the PC keyboard did not have. */
uint8_t keymap_sdl_to_pc(SDL_Scancode scancode);

#endif /* LLAMATRON_KEYMAP_H */
