/*
 * assets.h - access to the data the extractor produced (build/generated/data).
 *
 * Two interchangeable implementations, chosen when linking the executable:
 *   llama_assets_embedded - the files are compiled into the binary (assets_blob.c, see
 *                           asset_blob.h); no files needed at run time (default for native);
 *   llama_assets_files    - the files are read from a directory (web: the preloaded /data;
 *                           dev/RE builds; tests that check the extractor's output).
 * Pure C, no SDL.
 */
#ifndef LLAMATRON_ASSETS_H
#define LLAMATRON_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Directory used by the file-backed implementation (ignored when embedded). */
void assets_set_directory(const char *dir);
/* True when the data is compiled into the binary. */
bool assets_embedded(void);

/* Contents of the named data file ("title_vga.idx8"), or NULL (with a message on stderr) if
 * it does not exist. The memory stays valid until the program ends; do not free it. */
const uint8_t *asset_get(const char *name, size_t *size);

/* asset_get() that also requires an exact size; NULL (with a message) otherwise. */
const uint8_t *asset_get_exact(const char *name, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_ASSETS_H */
