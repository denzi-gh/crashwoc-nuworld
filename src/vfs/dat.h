/* .DAT archives (ATS.DAT, CHARS.DAT, SFX.DAT), the Nu engine's packed file trees. Little-endian:
   +0 u32 dir_offset, +4 u32 dir_size, payload from +8. At dir_offset: u32 0xFFFFFFFF, u32 nfiles,
   nfiles x {u32 offset, size, unpacked_size, flags}, u32 nnodes, nnodes x {s16 child, s16 sibling, u32 name},
   u32 names_size, NUL-terminated names. Errors via SDL_GetError(). */
#ifndef NUWORLD_VFS_DAT_H
#define NUWORLD_VFS_DAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs/vfs.h"

#define NW_DAT_PACKED 1u      /* RNC ProPack: unpacked_size is the real size (NuPPUnpack) */
#define NW_DAT_NODES_MAX 32768 /* node links are s16 */

typedef struct NwDatEntry {
    uint32_t offset; /* from the start of the archive */
    uint32_t size;   /* stored bytes */
    uint32_t unpacked_size;
    uint32_t flags;
} NwDatEntry;

typedef struct NwDatNode {
    int32_t child;    /* > 0: directory, first child node; <= 0: file entry -child */
    int32_t sibling;  /* next node, 0 ends the chain */
    const char *name; /* NULL for node 0 */
} NwDatNode;

typedef struct NwDat {
    NwVfsFile *file;
    uint64_t file_size;
    NwDatEntry *entries;
    uint32_t count;
    NwDatNode *nodes; /* the tree starts at node 1 */
    uint32_t node_count;
    char *names;
} NwDat;

/* Parses the directory (dir_size bytes from dir_offset) of an archive of file_size bytes. */
bool nw_dat_parse(NwDat *dat, const void *dir, size_t dir_size, uint64_t file_size);
/* Opens an archive through the VFS. DUMMY.DAT is refused. */
bool nw_dat_open(NwDat *dat, const NwVfs *vfs, const char *game_path);
void nw_dat_close(NwDat *dat);

/* NuDatFileFindTree: '\'-separated, case-insensitive, from node 1. Entry index, or -1 when missing or a directory. */
int nw_dat_find(const NwDat *dat, const char *path);

/* Stored bytes of an entry (still RNC-packed when flags & NW_DAT_PACKED). Free with SDL_free. */
void *nw_dat_load(NwDat *dat, int index);

/* Calls fn for every file reachable from node 1 with its '\'-separated path. */
bool nw_dat_list(const NwDat *dat, void (*fn)(void *user, int index, const char *path), void *user);

#endif
