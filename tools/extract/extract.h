/* Extraction steps run by `llamatron-extract --all`, in this order. Each reads the user's
 * original files and writes typed data through the context's output set. */
#ifndef EXTRACT_EXTRACT_H
#define EXTRACT_EXTRACT_H

#include "manifest.h"
#include "outputs.h"

/* Ghidra loads every program at segment 1000, so image offset + 0x10000 == Ghidra address. */
#define LOAD_SEGMENT 0x1000

typedef struct {
    const char *original_dir;
    OutputSet *outputs;
    ByteBuf game86_image; /* game86 load module, relocated for LOAD_SEGMENT */
} ExtractContext;

/* `size` bytes of game86 at Ghidra address seg:off, or NULL (with an error) if out of range. */
const uint8_t *game86_at(const ExtractContext *ctx, uint16_t seg, uint16_t off, size_t size);

/* Reads original_dir/name and LZSS-decodes it; fails unless it has exactly expected_size bytes. */
bool load_cmp(const char *original_dir, const char *name, size_t expected_size, ByteBuf *out);

/* data/title_vga.{idx8,pal}, data/title_ega.{idx4,pal} — title.c */
bool extract_title_pictures(ExtractContext *ctx);
/* data/font.bin, data/sine.bin — cmpdata.c */
bool extract_cmp_data(ExtractContext *ctx);
/* data/sprites.bin, data/sprite_refs.bin, data/g3.bin — sprites.c */
bool extract_sprites(ExtractContext *ctx);

/* data/samples.bin, data/samples.idx — samples.c */
bool extract_samples(ExtractContext *ctx);
/* data/palettes.dat, data/default_scores.dat, data/user_scores.dat, data/config.dat — dgroup.c */
bool extract_dgroup_tables(ExtractContext *ctx);
/* assets_blob.c with every data/ file as a C array — blob.c. Runs last. */
bool write_assets_blob(ExtractContext *ctx);

#endif
