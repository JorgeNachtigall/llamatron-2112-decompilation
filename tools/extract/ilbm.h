/* IFF ILBM pictures (the title screens newtit.lbm, newtit2.lbm, tronega.lbm), decoded to one
 * byte per pixel. Chunk handling follows fs.exe (docs/re-notes-fs-install.md §3). */
#ifndef EXTRACT_ILBM_H
#define EXTRACT_ILBM_H

#include "common.h"

#define ILBM_MAX_COLORS 256

typedef struct {
    uint16_t width, height;
    uint8_t plane_count;
    uint8_t masking;
    uint8_t compression;
    uint16_t color_count;                  /* CMAP entries */
    uint8_t cmap[ILBM_MAX_COLORS][3];      /* 8-bit RGB as stored, unused entries zero */
    uint8_t *pixels;                       /* width * height colour indices, row-major */
} IlbmImage;

bool ilbm_decode(const uint8_t *data, size_t size, IlbmImage *out);
void ilbm_free(IlbmImage *image);

#endif
