/* Per-file SHA-256 manifest of a disc: names, sizes and hashes only. */
#ifndef NUWORLD_VFS_MANIFEST_H
#define NUWORLD_VFS_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define NW_MANIFEST_SKIP 1u /* never opened by the game: not hashed or extracted */

typedef struct NwManifestEntry {
    const char *path;   /* uppercase, '/'-separated, no ";1" */
    uint64_t size;
    const char *sha256; /* 64 lowercase hex digits */
    unsigned flags;
} NwManifestEntry;

typedef struct NwManifest {
    const NwManifestEntry *entries;
    size_t count;
} NwManifest;

/* PAL v1.03 (SLES-50386), from src/vfs/disc_manifest.inc. */
extern const NwManifest nw_disc_manifest;

/* Case-insensitive lookup. */
const NwManifestEntry *nw_manifest_find(const NwManifest *m, const char *path);

#endif
