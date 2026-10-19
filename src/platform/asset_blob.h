/*
 * asset_blob.h - layout of the build-time asset blob.
 *
 * The extractor writes <build>/generated/assets_blob.c (CMake: LLAMATRON_ASSETS_BLOB_C) from
 * the user's own original files: every file of generated/data as a byte array, listed in
 * sorted name order. That file is generated and gitignored, never committed; it only exists in
 * a build tree and makes the native binary independent of build/generated at run time.
 * Agreed format between W1 (extractor) and W2 (platform); change only together.
 */
#ifndef LLAMATRON_ASSET_BLOB_H
#define LLAMATRON_ASSET_BLOB_H

typedef struct LlamaBlobEntry {
    const char *name;            /* file name inside generated/data, e.g. "title_vga.idx8" */
    const unsigned char *data;
    unsigned long size;
} LlamaBlobEntry;

extern const LlamaBlobEntry llama_blob_entries[];
extern const unsigned llama_blob_count;

#endif /* LLAMATRON_ASSET_BLOB_H */
