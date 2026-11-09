/* Per-file SHA-256 manifest of a disc: names, sizes and hashes only. */
#ifndef NUWORLD_VFS_MANIFEST_H
#define NUWORLD_VFS_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define NW_MANIFEST_SKIP 1u /* never opened by the game: not hashed or extracted */

#define NW_DISC_DATA_ELF 1u /* the data image is made from its ELF */
#define NW_DISC_ENGLISH 2u  /* English voices only */

typedef struct NwManifestEntry {
    const char *path;   /* uppercase, '/'-separated, no ";1" */
    uint64_t size;
    const char *sha256; /* 64 lowercase hex digits */
    unsigned flags;
} NwManifestEntry;

typedef struct NwManifest {
    const NwManifestEntry *entries;
    size_t count;
    const char *name;  /* "PAL v1.03" */
    const char *boot;  /* SYSTEM.CNF's BOOT2, VER and VMODE */
    const char *ver;
    const char *vmode;
    unsigned flags;    /* NW_DISC_* */
} NwManifest;

/* From src/vfs/disc_manifest*.inc. */
extern const NwManifest nw_disc_manifest, nw_disc_manifest_usa100;
/* PAL first, NULL-terminated. */
extern const NwManifest *const nw_disc_manifests[];

/* Case-insensitive lookup. */
const NwManifestEntry *nw_manifest_find(const NwManifest *m, const char *path);

#endif
