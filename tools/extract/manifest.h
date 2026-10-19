/* tools/manifest/original-files.json: the supported release's file names, sizes and SHA-256
 * hashes, and the verification of a user's original folder against it. */
#ifndef EXTRACT_MANIFEST_H
#define EXTRACT_MANIFEST_H

#include "common.h"
#include "sha256.h"

#define MANIFEST_MAX_FILES 64
#define MANIFEST_NAME_SIZE 64

typedef struct {
    char name[MANIFEST_NAME_SIZE];
    uint64_t size;
    char sha256[SHA256_HEX_SIZE];
    bool verify; /* false: user-modifiable file, optional and only reported */
} ManifestFile;

typedef struct {
    char release[256];
    ManifestFile files[MANIFEST_MAX_FILES];
    size_t file_count;
} Manifest;

bool manifest_load(const char *path, Manifest *out);
const ManifestFile *manifest_find(const Manifest *manifest, const char *name);

/* Finds `name` in `dir`, also trying the all-uppercase spelling used by DOS copies.
 * Writes the full path into `path`; returns false if neither exists. */
bool find_original(const char *dir, const char *name, char *path, size_t path_size);

/* Checks every manifest entry against `dir`, printing one line per file. Returns true when
 * every `verify` file is present and matches. */
bool manifest_verify(const Manifest *manifest, const char *dir);

#endif
