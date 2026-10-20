/* Disc identity and per-file checks against a manifest. Errors via SDL_GetError(). */
#ifndef NUWORLD_VFS_VERIFY_H
#define NUWORLD_VFS_VERIFY_H

#include <stdbool.h>
#include <stdint.h>

#include "vfs/disc.h"
#include "vfs/iso9660.h"
#include "vfs/manifest.h"
#include "vfs/sha256.h"

#define NW_DISC_BOOT "SLES_503.86"
#define NW_DISC_VER "1.03"
#define NW_DISC_VMODE "PAL"

#define NW_VERIFY_SKIPPED 1u /* also hash NW_MANIFEST_SKIP entries */

/* Bytes hashed so far of the total. */
typedef void (*NwProgressFn)(void *user, uint64_t done, uint64_t total);

/* SYSTEM.CNF names BOOT2 cdrom0:\SLES_503.86;1, VER 1.03, VMODE PAL; it and the boot ELF match the manifest. */
bool nw_verify_identity(NwDisc *disc, const NwIso *iso, const NwManifest *m);

/* The disc holds exactly the manifest's files, with its sizes and hashes. */
bool nw_verify_files(NwDisc *disc, const NwIso *iso, const NwManifest *m, unsigned flags, NwProgressFn progress,
                     void *user);

/* Compares a digest with a manifest entry; on mismatch sets an error naming the path and both hashes. */
bool nw_verify_digest(const NwManifestEntry *e, const uint8_t digest[NW_SHA256_SIZE]);

#endif
