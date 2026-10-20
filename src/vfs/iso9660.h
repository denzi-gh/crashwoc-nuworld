/* ISO9660 file index: uppercase '/'-separated paths without the ";1" version. Errors via SDL_GetError(). */
#ifndef NUWORLD_VFS_ISO9660_H
#define NUWORLD_VFS_ISO9660_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs/disc.h"

#define NW_ISO_PATH_MAX 256 /* with the NUL */

typedef struct NwIsoFile {
    char *path; /* e.g. "LEVELS/B/HUB/HUB.GSC" */
    uint32_t lba;
    uint32_t size;
} NwIsoFile;

typedef struct NwIso {
    NwIsoFile *files; /* sorted by path */
    uint32_t count;
    char volume_id[33];
} NwIso;

/* Walks the whole tree. Multi-extent and interleaved files and duplicate names are refused. */
bool nw_iso_index(NwIso *iso, NwDisc *disc);
void nw_iso_free(NwIso *iso);

/* Case-insensitive; accepts '\' separators and a ";1" suffix. */
const NwIsoFile *nw_iso_find(const NwIso *iso, const char *path);

/* Reads size bytes of a file from offset. */
bool nw_iso_read(NwDisc *disc, const NwIsoFile *file, uint64_t offset, void *dst, size_t size);

#endif
