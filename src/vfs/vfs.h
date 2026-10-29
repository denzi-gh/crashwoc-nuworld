/* Game file system: maps the PS2 devices onto the data folder. cdrom0: reads go through a mount list (the
   --overlay folders, the newest first, then the imported disc) with case-insensitive lookups; host0: is the
   writable <root>host0/ folder. Errors via SDL_GetError(). */
#ifndef NUWORLD_VFS_VFS_H
#define NUWORLD_VFS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_VFS_PATH_MAX 256 /* normalised path with the NUL */
#define NW_VFS_OVERLAYS 4   /* --overlay folders at most */

typedef enum NwVfsDevice {
    NW_VFS_CDROM, /* cdrom0:, also paths without a device */
    NW_VFS_HOST,  /* host0: */
} NwVfsDevice;

typedef enum NwVfsSlot {
    NW_VFS_OVERLAY,                /* the first of the overlay slots, the newest overlay */
    NW_VFS_DISC = NW_VFS_OVERLAYS, /* the imported disc */
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
} NwVfs;

typedef struct NwVfsFile NwVfsFile;

/* cdrom0:\LEVELS\B\HUB\HUB.GSC;1 -> NW_VFS_CDROM "LEVELS/B/HUB/HUB.GSC"; host0:LEVELS\X -> NW_VFS_HOST
   "LEVELS/X". Uppercases like NuFileOpen; refuses other devices, "." and ".." components. */
bool nw_vfs_map(const char *game_path, NwVfsDevice *device, char out[NW_VFS_PATH_MAX]);

/* Mounts <root>disc/ (an imported disc) and sets host0: to <root>host0/. */
bool nw_vfs_init(NwVfs *vfs, const char *root);
/* Indexes dir into a slot; NULL empties it. Names starting with '.' are skipped. */
bool nw_vfs_mount(NwVfs *vfs, NwVfsSlot slot, const char *dir);
void nw_vfs_shutdown(NwVfs *vfs);

/* --overlay: mounts dir in front of the overlays mounted before, so the newest wins. It only replaces: each file
   stands for a disc file, or, in a folder named after an archive (ATS.DAT/LEVELS/...), for one of its entries; the
   boot ELF, SYSTEM.CNF, the IRX modules and DUMMY.DAT are never replaced. A file an older overlay also has goes to
   conflict (the newest wins). Needs the disc mounted. */
bool nw_vfs_overlay(NwVfs *vfs, const char *dir, void (*conflict)(void *user, const char *key), void *user);
/* Calls fn with the overlays' files under prefix (an archive's key and '/'), the newest overlay's for a key, with
   the key's rest and the host path. */
void nw_vfs_overlay_each(const NwVfs *vfs, const char *prefix,
                         void (*fn)(void *user, const char *rest, const char *path), void *user);

/* Host path of a game file: the first mount that has it (cdrom0:) or the host0: folder. Free with SDL_free. */
char *nw_vfs_resolve(const NwVfs *vfs, const char *game_path);

NwVfsFile *nw_vfs_open(const NwVfs *vfs, const char *game_path, NwVfsMode mode);
/* Bytes transferred, -1 on error. */
int64_t nw_vfs_read(NwVfsFile *f, void *dst, size_t size);
int64_t nw_vfs_write(NwVfsFile *f, const void *src, size_t size);
/* whence: 0 set, 1 current, 2 end (NuFileSeek origins). Returns the new position, -1 on error. */
int64_t nw_vfs_seek(NwVfsFile *f, int64_t offset, int whence);
int64_t nw_vfs_size(NwVfsFile *f);
void nw_vfs_close(NwVfsFile *f);

#endif
