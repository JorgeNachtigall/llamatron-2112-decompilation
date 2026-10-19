/* LZSS as used by game86 for f.cmp, g3.cmp and s.cmp (Okumura-style, 4 KB ring buffer).
 * game86 1000:e6bb (decoder), 1000:e65e (file loader) */
#ifndef EXTRACT_LZSS_H
#define EXTRACT_LZSS_H

#include "common.h"

/* Decodes a whole .cmp stream; fails unless it ends with the terminator exactly at the end
 * of the input. */
bool lzss_decode(const uint8_t *data, size_t size, ByteBuf *out);

#endif
