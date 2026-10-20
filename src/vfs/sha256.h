/* SHA-256 (FIPS 180-4). */
#ifndef NUWORLD_VFS_SHA256_H
#define NUWORLD_VFS_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define NW_SHA256_SIZE 32
#define NW_SHA256_HEX 65 /* 64 lowercase digits + NUL */

typedef struct NwSha256 {
    uint32_t state[8];
    uint64_t length; /* bytes hashed so far */
    uint8_t block[64];
} NwSha256;

void nw_sha256_init(NwSha256 *s);
void nw_sha256_update(NwSha256 *s, const void *data, size_t size);
void nw_sha256_final(NwSha256 *s, uint8_t digest[NW_SHA256_SIZE]);
void nw_sha256(const void *data, size_t size, uint8_t digest[NW_SHA256_SIZE]);
void nw_sha256_hex(const uint8_t digest[NW_SHA256_SIZE], char hex[NW_SHA256_HEX]);

#endif
