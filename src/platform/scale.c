/*
 * scale.c - image placement per scale mode (scale.h).
 */
#include "scale.h"

enum { DISPLAY_ASPECT_W = 4, DISPLAY_ASPECT_H = 3 };

/* An integer-scaled pixel may be this far off the 4:3 pixel aspect (320x200: 4x5 is 1.25
 * against 1.2, 4x4 would be 1.0); otherwise the integer mode falls back to the 4:3 fit. */
#define INTEGER_ASPECT_TOLERANCE 0.1

static ScaleRect centred(int w, int h, int out_w, int out_h)
{
    return (ScaleRect){ (out_w - w) / 2, (out_h - h) / 2, w, h };
}

/* Largest 4:3 rectangle in the drawable. */
static ScaleRect fit_4_3(int out_w, int out_h)
{
    if (out_w * DISPLAY_ASPECT_H > out_h * DISPLAY_ASPECT_W)
        return centred(out_h * DISPLAY_ASPECT_W / DISPLAY_ASPECT_H, out_h, out_w, out_h);
    return centred(out_w, out_w * DISPLAY_ASPECT_H / DISPLAY_ASPECT_W, out_w, out_h);
}

static double aspect_error(double got, double want)
{
    return got > want ? got / want - 1.0 : want / got - 1.0;
}

/* Whole pixel multiples (sx across, sy down) with sy / sx near the 4:3 pixel aspect, as large
 * as fit; the larger area wins, then the closer aspect. */
static ScaleRect fit_integer(int src_w, int src_h, int out_w, int out_h)
{
    /* A pixel's height over its width on a 4:3 screen: 1.2 for 320x200. */
    double pixel_aspect = (double)DISPLAY_ASPECT_H * src_w / ((double)DISPLAY_ASPECT_W * src_h);
    int best_sx = 0, best_sy = 0;
    long best_area = 0;
    double best_error = 0;
    for (int sx = 1; sx * src_w <= out_w; sx++) {
        for (int sy = 1; sy * src_h <= out_h; sy++) {
            double error = aspect_error((double)sy / sx, pixel_aspect);
            if (error > INTEGER_ASPECT_TOLERANCE)
                continue;
            long area = (long)sx * sy;
            if (area > best_area || (area == best_area && error < best_error)) {
                best_sx = sx;
                best_sy = sy;
                best_area = area;
                best_error = error;
            }
        }
    }
    if (best_area == 0)
        return fit_4_3(out_w, out_h);
    return centred(src_w * best_sx, src_h * best_sy, out_w, out_h);
}

ScaleRect scale_fit(PlatScaleMode mode, int src_w, int src_h, int out_w, int out_h)
{
    if (src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0)
        return (ScaleRect){ 0, 0, 0, 0 };
    if (mode == PLAT_SCALE_INTEGER)
        return fit_integer(src_w, src_h, out_w, out_h);
    return fit_4_3(out_w, out_h);
}

void scale_sharp_factors(int src_w, int src_h, ScaleRect dst, int *kx, int *ky)
{
    *kx = src_w > 0 && dst.w > src_w ? (dst.w + src_w - 1) / src_w : 1;
    *ky = src_h > 0 && dst.h > src_h ? (dst.h + src_h - 1) / src_h : 1;
}
