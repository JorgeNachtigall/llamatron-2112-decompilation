/* SHA-256 (FIPS 180-4), used to verify the original files and to stamp the outputs. */
#ifndef EXTRACT_SHA256_H
#define EXTRACT_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE (SHA256_DIGEST_SIZE * 2 + 1)

typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t block_used;
} Sha256;

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const void *data, size_t size);
void sha256_final(Sha256 *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/* One-shot helper: lowercase hex digest of `data`, NUL-terminated. */
void sha256_hex(const void *data, size_t size, char hex[SHA256_HEX_SIZE]);

#endif
