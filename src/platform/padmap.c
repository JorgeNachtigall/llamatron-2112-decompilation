/*
 * padmap.c - host gamepads as the original's controls (see padmap.h).
 */
#include "padmap.h"

#include <stddef.h>

enum PadKey {
    PAD_KEY_UP = 1u << 0,
    PAD_KEY_DOWN = 1u << 1,
    PAD_KEY_LEFT = 1u << 2,
    PAD_KEY_RIGHT = 1u << 3,
    PAD_KEY_FIRE = 1u << 4,
    PAD_KEY_PAUSE = 1u << 5,
    PAD_KEY_SOUND = 1u << 6,
    PAD_KEY_ENTER = 1u << 7,
    PAD_KEY_OPTIONS = 1u << 8,
    PAD_KEY_MENU = PAD_KEY_PAUSE | PAD_KEY_SOUND | PAD_KEY_ENTER | PAD_KEY_OPTIONS,
};

/* PC scan code set 1 make codes, in PadKey bit order. */
static const uint8_t PAD_KEY_CODES[] = { 0x48, 0x50, 0x4b, 0x4d, 0x39, 0x19, 0x1f, 0x1c, 0x18 };

static const unsigned FIRE_BUTTONS =
    PLAT_PAD_BUTTON_A | PLAT_PAD_BUTTON_B | PLAT_PAD_BUTTON_X | PLAT_PAD_BUTTON_Y;

static unsigned menu_keys(const PadMap *map, const PlatGamepad *pad)
{
    unsigned keys = 0;
    if (pad->buttons & PLAT_PAD_BUTTON_START)
        keys |= map->title ? PAD_KEY_ENTER : PAD_KEY_PAUSE;
    if (pad->buttons & PLAT_PAD_BUTTON_BACK)
        keys |= map->title ? PAD_KEY_OPTIONS : PAD_KEY_SOUND;
    return keys;
}

/* Send the difference between what is held and `want`, limited to `scope`: releases first,
 * then presses, each in PadKey order. */
static void send_changes(PadMap *map, unsigned want, unsigned scope, PadKeyFn on_key, void *user)
{
    unsigned released = map->held & ~want & scope;
    unsigned pressed = want & ~map->held & scope;
    for (size_t i = 0; i < sizeof PAD_KEY_CODES; i++)
        if (released & 1u << i)
            on_key(PAD_KEY_CODES[i], false, user);
    for (size_t i = 0; i < sizeof PAD_KEY_CODES; i++)
        if (pressed & 1u << i)
            on_key(PAD_KEY_CODES[i], true, user);
    map->held = (map->held & ~scope) | (want & scope);
}

void padmap_keys(PadMap *map, const PlatGamepad *pad, PadKeyFn on_key, void *user)
{
    unsigned want = 0;
    if (pad->connected) {
        if (pad->y <= -PADMAP_THRESHOLD)
            want |= PAD_KEY_UP;
        else if (pad->y >= PADMAP_THRESHOLD)
            want |= PAD_KEY_DOWN;
        if (pad->x <= -PADMAP_THRESHOLD)
            want |= PAD_KEY_LEFT;
        else if (pad->x >= PADMAP_THRESHOLD)
            want |= PAD_KEY_RIGHT;
        bool right_stick = pad->rx * pad->rx + pad->ry * pad->ry >= PADMAP_THRESHOLD * PADMAP_THRESHOLD;
        if (right_stick || (pad->buttons & FIRE_BUTTONS))
            want |= PAD_KEY_FIRE;
        want |= menu_keys(map, pad);
    }
    send_changes(map, want, ~0u, on_key, user);
}

void padmap_menu_keys(PadMap *map, const PlatGamepad *pad, PadKeyFn on_key, void *user)
{
    send_changes(map, pad->connected ? menu_keys(map, pad) : 0, PAD_KEY_MENU, on_key, user);
}

void padmap_sticks(const PlatGamepad *pad1, const PlatGamepad *pad2, PadStick sticks[2])
{
    sticks[0] = (PadStick){ 0, 0, false, false };
    sticks[1] = sticks[0];
    if (pad1->connected) {
        sticks[0] = (PadStick){ pad1->x, pad1->y, (pad1->buttons & PLAT_PAD_BUTTON_A) != 0,
                                (pad1->buttons & PLAT_PAD_BUTTON_B) != 0 };
        if (!pad2->connected)
            sticks[1] = (PadStick){ pad1->rx, pad1->ry, (pad1->buttons & PLAT_PAD_BUTTON_X) != 0,
                                    (pad1->buttons & PLAT_PAD_BUTTON_Y) != 0 };
    }
    if (pad2->connected)
        sticks[1] = (PadStick){ pad2->x, pad2->y, (pad2->buttons & PLAT_PAD_BUTTON_A) != 0,
                                (pad2->buttons & PLAT_PAD_BUTTON_B) != 0 };
}
