/* The set of files written by `--all`; each write is recorded for generated/extract.stamp. */
#ifndef EXTRACT_OUTPUTS_H
#define EXTRACT_OUTPUTS_H

#include "common.h"
#include "sha256.h"

#define MAX_OUTPUTS 128

typedef struct {
    char path[256];
    char sha256[SHA256_HEX_SIZE];
} OutputRecord;

typedef struct {
    const char *root;
    OutputRecord records[MAX_OUTPUTS];
    size_t count;
} OutputSet;

/* Writes `relative` (e.g. "data/font.bin") under the output root, creating directories. */
bool output_write(OutputSet *set, const char *relative, const uint8_t *data, size_t size);
bool output_write_text(OutputSet *set, const char *relative, const char *text);
bool output_write_stamp(const OutputSet *set);

#endif
