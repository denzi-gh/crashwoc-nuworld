/* cdrom0: the --overlay folders (newest first), then the imported disc, case-insensitively; host0: <root>host0/. */
#ifndef NUWORLD_VFS_VFS_H
#define NUWORLD_VFS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs/canon.h"
#include "vfs/manifest.h"

#define NW_VFS_PATH_MAX 256 /* with the NUL */
#define NW_VFS_OVERLAYS 4

typedef enum NwVfsDevice {
    NW_VFS_CDROM, /* cdrom0:, also paths without a device */
    NW_VFS_HOST,
} NwVfsDevice;

typedef enum NwVfsSlot {
    NW_VFS_OVERLAY,                /* the first overlay slot, the newest */
    NW_VFS_DISC = NW_VFS_OVERLAYS,
    NW_VFS_SLOTS,
} NwVfsSlot;

/* Same order as nufilemode_e. */
typedef enum NwVfsMode {
    NW_VFS_READ,
    NW_VFS_WRITE,
    NW_VFS_APPEND,
} NwVfsMode;

typedef struct NwVfsEntry {
    char *key;  /* uppercase, '/'-separated */
    char *path; /* as on the host, relative to the mount */
} NwVfsEntry;

typedef struct NwVfsMount {
    char *root;          /* with a trailing separator; NULL when empty */
    NwVfsEntry *entries; /* sorted by key */
    uint32_t count;
} NwVfsMount;

typedef struct NwVfs {
    NwVfsMount mounts[NW_VFS_SLOTS];
    char *host0;
    const NwManifest *manifest;
    const NwCanonTable *canon;
} NwVfs;

typedef struct NwVfsFile NwVfsFile;

/* Uppercases like NuFileOpen; refuses other devices, "." and ".." components. */
bool nw_vfs_map(const char *game_path, NwVfsDevice *device, char out[NW_VFS_PATH_MAX]);

bool nw_vfs_init(NwVfs *vfs, const char *root);
/* NULL empties the slot; names starting with '.' are skipped. */
bool nw_vfs_mount(NwVfs *vfs, NwVfsSlot slot, const char *dir);
void nw_vfs_shutdown(NwVfs *vfs);

/* The newest overlay wins and shadowed files go to conflict; OPENING/ can add a frontend profile and its assets, and
   an X.LOOKS beside a disc model. Needs the disc mounted. */
bool nw_vfs_overlay(NwVfs *vfs, const char *dir, void (*conflict)(void *user, const char *key), void *user);
/* prefix: an archive's key and '/'; fn gets the newest overlay's file per key. */
void nw_vfs_overlay_each(const NwVfs *vfs, const char *prefix,
                         void (*fn)(void *user, const char *rest, const char *path), void *user);

/* Free with SDL_free. */
char *nw_vfs_resolve(const NwVfs *vfs, const char *game_path);

NwVfsFile *nw_vfs_open(const NwVfs *vfs, const char *game_path, NwVfsMode mode);
int64_t nw_vfs_read(NwVfsFile *f, void *dst, size_t size);
int64_t nw_vfs_write(NwVfsFile *f, const void *src, size_t size);
/* whence: 0 set, 1 current, 2 end (NuFileSeek origins). */
int64_t nw_vfs_seek(NwVfsFile *f, int64_t offset, int whence);
int64_t nw_vfs_size(NwVfsFile *f);
void nw_vfs_close(NwVfsFile *f);

#endif
