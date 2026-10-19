/* Extraction steps run by `llamatron-extract --all`, in this order. Each reads the user's
 * original files and writes typed data through the context's output set. */
#ifndef EXTRACT_EXTRACT_H
#define EXTRACT_EXTRACT_H

#include "manifest.h"
#include "outputs.h"

/* Ghidra loads every program at segment 1000, so image offset + 0x10000 == Ghidra address. */
#define LOAD_SEGMENT 0x1000

/* Versions of our data formats (FORMATS.md); bump when a layout changes. Also emitted into
 * generated/asset_index.h as LLAMA_FMT_<MAGIC>_VERSION. */
#define FMT_LFNT_VERSION 1
#define FMT_LSPR_VERSION 1
#define FMT_LSRF_VERSION 1
#define FMT_LSMP_VERSION 1
#define FMT_LPAL_VERSION 1
#define FMT_LSCO_VERSION 1
#define FMT_LCFG_VERSION 1
#define FMT_LTXT_VERSION 2
#define FMT_LLVL_VERSION 2
#define FMT_LHUD_VERSION 1
#define FMT_LHSC_VERSION 1
#define FMT_LENM_VERSION 2

#define MAX_INDEX_DEFINES 32

/* A key count for asset_index.h, e.g. {"LEVEL_COUNT", 100}. */
typedef struct {
    const char *name;
    unsigned long value;
} IndexDefine;

typedef struct {
    const char *original_dir;
    OutputSet *outputs;
    ByteBuf game86_image; /* game86 load module, relocated for LOAD_SEGMENT */
    IndexDefine defines[MAX_INDEX_DEFINES];
    size_t define_count;
} ExtractContext;

/* Records a key count for asset_index.h (name without the LLAMA_ prefix). */
bool index_define(ExtractContext *ctx, const char *name, unsigned long value);

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
/* data/strings.dat — texts.c */
bool extract_texts(ExtractContext *ctx);
/* data/levels.dat — levels.c */
bool extract_levels(ExtractContext *ctx);
/* assets_blob.c with every data/ file as a C array — blob.c. Runs after the data steps. */
bool write_assets_blob(ExtractContext *ctx);
/* asset_index.h: per-file names and sizes, format versions, key counts — blob.c. Runs last. */
bool write_asset_index(ExtractContext *ctx);

#endif
