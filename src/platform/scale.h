/*
 * scale.h - where an image goes in the window for each PlatScaleMode (platform.h). Pure
 * arithmetic, no SDL, so display_selftest can check it.
 *
 * The original's modes all filled a 4:3 monitor: 320x200 (game), 320x400 (VGA title) and
 * 640x350 (EGA title) each have their own pixel aspect, (4/3) / (width/height).
 */
#ifndef LLAMATRON_SCALE_H
#define LLAMATRON_SCALE_H

#include "platform.h"

typedef struct ScaleRect {
    int x, y, w, h;
} ScaleRect;

/* The destination rectangle of a src_w x src_h image in an out_w x out_h drawable, centred. */
ScaleRect scale_fit(PlatScaleMode mode, int src_w, int src_h, int out_w, int out_h);

/* PLAT_SCALE_SHARP: the whole multiples (kx, ky) to enlarge the image by with nearest
 * filtering before it is smoothed down to `dst`: the smallest that are not below dst. */
void scale_sharp_factors(int src_w, int src_h, ScaleRect dst, int *kx, int *ky);

#endif
