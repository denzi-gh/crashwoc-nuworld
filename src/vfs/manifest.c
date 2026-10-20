#include "vfs/manifest.h"

#include <SDL3/SDL.h>

static const NwManifestEntry nw_disc_entries[] = {
#include "vfs/disc_manifest.inc"
};

const NwManifest nw_disc_manifest = { nw_disc_entries, SDL_arraysize(nw_disc_entries) };

const NwManifestEntry *nw_manifest_find(const NwManifest *m, const char *path)
{
    for (size_t i = 0; i < m->count; i++) {
        if (SDL_strcasecmp(m->entries[i].path, path) == 0) {
            return &m->entries[i];
        }
    }
    return NULL;
}
