/*
 * keymap.h - SDL scancode -> IBM PC/AT keyboard scan code set 1 (make codes).
 * Break code = make code | 0x80. Codes are the standard XT/AT values, nothing game specific.
 */
#ifndef LLAMATRON_KEYMAP_H
#define LLAMATRON_KEYMAP_H

#include <SDL_keycode.h>
#include <SDL_scancode.h>
#include <stdint.h>

/* Returns 0 for keys the PC keyboard did not have. */
uint8_t keymap_sdl_to_pc(SDL_Scancode scancode);

/* Keys the host keeps for itself (the game never sees them). */
typedef enum HostKey {
    HOST_KEY_NONE,
    HOST_KEY_FULLSCREEN,    /* F11, Alt+Enter; on macOS also Cmd+F and Ctrl+Cmd+F */
    HOST_KEY_SCALE_MODE,    /* F10; on macOS also Cmd+I */
} HostKey;

HostKey keymap_host_key(SDL_Scancode scancode, SDL_Keymod mod);

#endif /* LLAMATRON_KEYMAP_H */
