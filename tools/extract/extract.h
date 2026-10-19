/* Extraction steps run by `llamatron-extract --all`, in this order. Each reads the user's
 * original files from `original_dir` and writes typed data through `outputs`. */
#ifndef EXTRACT_EXTRACT_H
#define EXTRACT_EXTRACT_H

#include "manifest.h"
#include "outputs.h"

/* data/title_vga.{idx8,pal}, data/title_ega.{idx4,pal} — see title.c */
bool extract_title_pictures(const char *original_dir, OutputSet *outputs);

/* Reads original_dir/name and LZSS-decodes it; fails unless it has exactly expected_size bytes. */
bool load_cmp(const char *original_dir, const char *name, size_t expected_size, ByteBuf *out);

/* data/font.bin, data/sine.bin — see cmpdata.c */
bool extract_cmp_data(const char *original_dir, OutputSet *outputs);

#endif
