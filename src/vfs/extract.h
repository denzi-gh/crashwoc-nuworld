/* Disc import into <root>disc/: identity check, copy with hashing, then a swap. Errors via SDL_GetError(). */
#ifndef NUWORLD_VFS_EXTRACT_H
#define NUWORLD_VFS_EXTRACT_H

#include <stdbool.h>

#include "vfs/disc.h"
#include "vfs/iso9660.h"
#include "vfs/manifest.h"
#include "vfs/verify.h"

#define NW_DISC_DIR "disc"
#define NW_DISC_STAMP ".nuworld-disc"

/* Copies every non-skipped manifest file to <root>disc.partial/, hashing as it goes, then swaps that in as
   <root>disc/ with a stamp. An existing disc/ is only replaced on success; a failure leaves no disc.partial/. */
bool nw_extract_disc(NwDisc *disc, const NwIso *iso, const NwManifest *m, const char *root, NwProgressFn progress,
                     void *user);

/* <root>disc/ holds a finished import (its stamp exists); nothing is hashed. */
bool nw_extract_has_disc(const char *root);

/* Re-hashes an imported <root>disc/ against the manifest. */
bool nw_extract_verify(const char *root, const NwManifest *m, NwProgressFn progress, void *user);

#endif
