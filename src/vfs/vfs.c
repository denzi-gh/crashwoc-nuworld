#include "vfs/vfs.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "platform/paths.h"
#include "vfs/extract.h"

struct NwVfsFile {
    SDL_IOStream *io;
};

typedef struct NwVfsScan {
    NwVfsMount *mount;
    size_t cap;
    char rel[NW_VFS_PATH_MAX]; /* directory being scanned, relative to the mount */
    size_t rel_len;
} NwVfsScan;

bool nw_vfs_map(const char *game_path, NwVfsDevice *device, char out[NW_VFS_PATH_MAX])
{
    static const struct {
        const char *prefix;
        NwVfsDevice device;
    } devices[] = {
        { "cdrom0:", NW_VFS_CDROM },
        { "host0:", NW_VFS_HOST },
    };
    const char *p = game_path;
    const char *colon = strchr(p, ':');
    size_t n = 0;

    *device = NW_VFS_CDROM;
    if (colon) {
        size_t i = 0;
        while (i < SDL_arraysize(devices) && !((size_t)(colon + 1 - p) == SDL_strlen(devices[i].prefix) &&
                                               SDL_strncasecmp(p, devices[i].prefix, (size_t)(colon + 1 - p)) == 0)) {
            i++;
        }
        if (i == SDL_arraysize(devices)) {
            return SDL_SetError("%s: unknown device", game_path);
        }
        *device = devices[i].device;
        p = colon + 1;
    }

    /* Components between '\' or '/'; empty ones are dropped. cdrom0: names lose their ";1" version. */
    for (;;) {
        p += strspn(p, "\\/");
        size_t len = strcspn(p, "\\/");
        if (len == 0) {
            break;
        }
        size_t keep = len;
        const char *semi = memchr(p, ';', len);
        if (*device == NW_VFS_CDROM && p[len] == '\0' && semi) {
            keep = (size_t)(semi - p);
        }
        if (keep == 0 || (keep == 1 && p[0] == '.') || (keep == 2 && p[0] == '.' && p[1] == '.')) {
            return SDL_SetError("%s: bad path component", game_path);
        }
        if (n + keep + 1 >= NW_VFS_PATH_MAX) {
            return SDL_SetError("%s: path too long", game_path);
        }
        if (n > 0) {
            out[n++] = '/';
        }
        for (size_t i = 0; i < keep; i++) {
            out[n++] = (char)SDL_toupper((unsigned char)p[i]);
        }
        p += len;
    }
    out[n] = '\0';
    return n > 0 || SDL_SetError("%s: no file name", game_path);
}

static int SDLCALL nw_vfs_cmp(const void *a, const void *b)
{
    return SDL_strcmp(((const NwVfsEntry *)a)->key, ((const NwVfsEntry *)b)->key);
}

static bool nw_vfs_add(NwVfsScan *s, const char *name)
{
    NwVfsMount *m = s->mount;
    if (m->count == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 512;
        NwVfsEntry *entries = SDL_realloc(m->entries, cap * sizeof(*entries));
        if (!entries) {
            return false;
        }
        m->entries = entries;
        s->cap = cap;
    }
    NwVfsEntry *e = &m->entries[m->count];
    if (SDL_asprintf(&e->path, "%s%s", s->rel, name) < 0) {
        return false;
    }
    e->key = SDL_strdup(e->path);
    if (!e->key) {
        SDL_free(e->path);
        return false;
    }
    for (char *c = e->key; *c; c++) {
        *c = (char)SDL_toupper((unsigned char)*c);
    }
    m->count++;
    return true;
}

static SDL_EnumerationResult SDLCALL nw_vfs_scan(void *user, const char *dir, const char *name)
{
    NwVfsScan *s = user;
    SDL_PathInfo info;
    char *full = NULL;
    size_t len = SDL_strlen(name);
    bool ok = true;

    if (name[0] == '.') {
        return SDL_ENUM_CONTINUE;
    }
    if (s->rel_len + len + 2 > NW_VFS_PATH_MAX) {
        SDL_SetError("path too long: %s%s", s->rel, name);
        return SDL_ENUM_FAILURE;
    }
    if (SDL_asprintf(&full, "%s%s", dir, name) < 0 || !SDL_GetPathInfo(full, &info)) {
        SDL_free(full);
        return SDL_ENUM_FAILURE;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        size_t saved = s->rel_len;
        SDL_memcpy(s->rel + saved, name, len);
        s->rel[saved + len] = '/';
        s->rel[saved + len + 1] = '\0';
        s->rel_len = saved + len + 1;
        ok = SDL_EnumerateDirectory(full, nw_vfs_scan, s);
        s->rel_len = saved;
        s->rel[saved] = '\0';
    } else if (info.type == SDL_PATHTYPE_FILE) {
        ok = nw_vfs_add(s, name);
    }
    SDL_free(full);
    return ok ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

static void nw_vfs_unmount(NwVfsMount *m)
{
    for (uint32_t i = 0; i < m->count; i++) {
        SDL_free(m->entries[i].key);
        SDL_free(m->entries[i].path);
    }
    SDL_free(m->entries);
    SDL_free(m->root);
    SDL_zerop(m);
}

bool nw_vfs_mount(NwVfs *vfs, NwVfsSlot slot, const char *dir)
{
    SDL_PathInfo info;
    if ((unsigned)slot >= NW_VFS_SLOTS) {
        return SDL_SetError("bad mount slot %d", (int)slot);
    }
    NwVfsMount *m = &vfs->mounts[slot];
    nw_vfs_unmount(m);
    if (!dir) {
        return true;
    }
    if (!SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        return SDL_SetError("%s is not a folder", dir);
    }

    NwVfsScan s = { m, 0, "", 0 };
    m->root = nw_paths_dir(dir, NULL, false);
    bool ok = m->root && SDL_EnumerateDirectory(m->root, nw_vfs_scan, &s);
    if (ok) {
        SDL_qsort(m->entries, m->count, sizeof(*m->entries), nw_vfs_cmp);
        for (uint32_t i = 1; ok && i < m->count; i++) {
            if (SDL_strcmp(m->entries[i - 1].key, m->entries[i].key) == 0) {
                ok = SDL_SetError("%s and %s differ only in case", m->entries[i - 1].path, m->entries[i].path);
            }
        }
    }
    if (!ok) {
        nw_vfs_unmount(m);
    }
    return ok;
}

bool nw_vfs_init(NwVfs *vfs, const char *root)
{
    char *disc = nw_paths_dir(root, NW_DISC_DIR, false), *stamp = NULL;
    bool ok = disc && SDL_asprintf(&stamp, "%s" NW_DISC_STAMP, disc) >= 0;

    SDL_zerop(vfs);
    if (ok && !SDL_GetPathInfo(stamp, NULL)) {
        ok = SDL_SetError("no imported disc in %s", disc);
    }
    ok = ok && nw_vfs_mount(vfs, NW_VFS_DISC, disc);
    vfs->host0 = ok ? nw_paths_dir(root, "host0", false) : NULL;
    ok = ok && vfs->host0;
    SDL_free(stamp);
    SDL_free(disc);
    if (!ok) {
        nw_vfs_shutdown(vfs);
    }
    return ok;
}

void nw_vfs_shutdown(NwVfs *vfs)
{
    for (int i = 0; i < NW_VFS_SLOTS; i++) {
        nw_vfs_unmount(&vfs->mounts[i]);
    }
    SDL_free(vfs->host0);
    SDL_zerop(vfs);
}

static const NwVfsEntry *nw_vfs_lookup(const NwVfsMount *m, const char *key)
{
    uint32_t lo = 0, hi = m->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = SDL_strcmp(m->entries[mid].key, key);
        if (c == 0) {
            return &m->entries[mid];
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

char *nw_vfs_resolve(const NwVfs *vfs, const char *game_path)
{
    NwVfsDevice device;
    char key[NW_VFS_PATH_MAX];
    char *path = NULL;

    if (!nw_vfs_map(game_path, &device, key)) {
        return NULL;
    }
    if (device == NW_VFS_HOST) {
        if (!vfs->host0) {
            SDL_SetError("%s: host0: is not set up", game_path);
            return NULL;
        }
        return SDL_asprintf(&path, "%s%s", vfs->host0, key) < 0 ? NULL : path;
    }
    for (int i = 0; i < NW_VFS_SLOTS; i++) {
        const NwVfsEntry *e = nw_vfs_lookup(&vfs->mounts[i], key);
        if (e) {
            return SDL_asprintf(&path, "%s%s", vfs->mounts[i].root, e->path) < 0 ? NULL : path;
        }
    }
    SDL_SetError("%s: not found", game_path);
    return NULL;
}

NwVfsFile *nw_vfs_open(const NwVfs *vfs, const char *game_path, NwVfsMode mode)
{
    static const char *const modes[] = { "rb", "wb", "ab" };
    NwVfsDevice device;
    char key[NW_VFS_PATH_MAX];

    if (!nw_vfs_map(game_path, &device, key)) {
        return NULL;
    }
    if ((unsigned)mode > NW_VFS_APPEND) {
        SDL_SetError("%s: bad mode %d", game_path, (int)mode);
        return NULL;
    }
    if (device == NW_VFS_CDROM && mode != NW_VFS_READ) {
        SDL_SetError("%s: cdrom0: is read-only", game_path);
        return NULL;
    }
    char *path = nw_vfs_resolve(vfs, game_path);
    if (!path) {
        return NULL;
    }
    if (mode != NW_VFS_READ) {
        char *slash = SDL_strrchr(path, '/');
        *slash = '\0';
        bool made = SDL_CreateDirectory(path);
        *slash = '/';
        if (!made) {
            SDL_free(path);
            return NULL;
        }
    }
    SDL_IOStream *io = SDL_IOFromFile(path, modes[mode]);
    SDL_free(path);
    NwVfsFile *f = io ? SDL_malloc(sizeof(*f)) : NULL;
    if (!f) {
        if (io) {
            SDL_CloseIO(io);
        }
        return NULL;
    }
    f->io = io;
    return f;
}

int64_t nw_vfs_read(NwVfsFile *f, void *dst, size_t size)
{
    size_t n = SDL_ReadIO(f->io, dst, size);
    return n < size && SDL_GetIOStatus(f->io) == SDL_IO_STATUS_ERROR ? -1 : (int64_t)n;
}

int64_t nw_vfs_write(NwVfsFile *f, const void *src, size_t size)
{
    size_t n = SDL_WriteIO(f->io, src, size);
    return n < size ? -1 : (int64_t)n;
}

int64_t nw_vfs_seek(NwVfsFile *f, int64_t offset, int whence)
{
    static const SDL_IOWhence origins[] = { SDL_IO_SEEK_SET, SDL_IO_SEEK_CUR, SDL_IO_SEEK_END };
    if (whence < 0 || whence > 2) {
        SDL_SetError("bad seek origin %d", whence);
        return -1;
    }
    return SDL_SeekIO(f->io, offset, origins[whence]);
}

int64_t nw_vfs_size(NwVfsFile *f)
{
    return SDL_GetIOSize(f->io);
}

void nw_vfs_close(NwVfsFile *f)
{
    if (f) {
        SDL_CloseIO(f->io);
        SDL_free(f);
    }
}
