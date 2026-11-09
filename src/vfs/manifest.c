#include "vfs/manifest.h"

#include <SDL3/SDL.h>

static const NwManifestEntry nw_disc_entries[] = {
#include "vfs/disc_manifest.inc"
};

static const NwManifestEntry nw_disc_entries_usa100[] = {
#include "vfs/disc_manifest_usa100.inc"
};

const NwManifest nw_disc_manifest = { nw_disc_entries, SDL_arraysize(nw_disc_entries), "PAL v1.03", "SLES_503.86",
                                      "1.03", "PAL", NW_DISC_DATA_ELF };

const NwManifest nw_disc_manifest_usa100 = { nw_disc_entries_usa100, SDL_arraysize(nw_disc_entries_usa100),
                                             "USA v1.00", "SLUS_202.38", "1.00", "NTSC", NW_DISC_ENGLISH };

const NwManifest *const nw_disc_manifests[] = { &nw_disc_manifest, &nw_disc_manifest_usa100, NULL };

const NwManifestEntry *nw_manifest_find(const NwManifest *m, const char *path)
{
    for (size_t i = 0; i < m->count; i++) {
        if (SDL_strcasecmp(m->entries[i].path, path) == 0) {
            return &m->entries[i];
        }
    }
    return NULL;
}
