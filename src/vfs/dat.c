#include "vfs/dat.h"

#include <string.h>

#include <SDL3/SDL.h>

#define NW_DAT_MAGIC 0xffffffffu
#define NW_DAT_HEADER 8
#define NW_DAT_DIR_MAX (64u << 20) /* sanity limit for the directory */
#define NW_DAT_DEPTH_MAX 64
#define NW_DAT_PATH_MAX 256

typedef struct NwDatWalk {
    const NwDat *dat;
    uint8_t seen[NW_DAT_NODES_MAX / 8];
    char path[NW_DAT_PATH_MAX];
    void (*fn)(void *user, int index, const char *path);
    void *user;
} NwDatWalk;

static uint32_t nw_dat_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t nw_dat_s16(const uint8_t *p)
{
    return (int16_t)(uint16_t)(p[0] | p[1] << 8);
}

static bool nw_dat_seen(uint8_t *seen, int32_t node)
{
    bool was = seen[node >> 3] & (1u << (node & 7));
    seen[node >> 3] |= (uint8_t)(1u << (node & 7));
    return was;
}

static void nw_dat_free(NwDat *dat)
{
    SDL_free(dat->entries);
    SDL_free(dat->nodes);
    SDL_free(dat->names);
    dat->entries = NULL;
    dat->nodes = NULL;
    dat->names = NULL;
    dat->count = dat->node_count = 0;
}

static bool nw_dat_tables(NwDat *dat, const uint8_t *b, size_t files_at, size_t nodes_at, uint32_t names_size)
{
    for (uint32_t i = 0; i < dat->count; i++) {
        const uint8_t *p = b + files_at + (size_t)i * 16;
        NwDatEntry *e = &dat->entries[i];
        *e = (NwDatEntry){ nw_dat_le32(p), nw_dat_le32(p + 4), nw_dat_le32(p + 8), nw_dat_le32(p + 12) };
        if (e->offset < NW_DAT_HEADER || e->size > dat->file_size || e->offset > dat->file_size - e->size) {
            return SDL_SetError("entry %u lies outside the archive", i);
        }
        if (!(e->flags & NW_DAT_PACKED) && e->unpacked_size != e->size) {
            return SDL_SetError("entry %u is stored but unpacked_size %u != size %u", i, e->unpacked_size, e->size);
        }
    }
    dat->nodes[0] = (NwDatNode){ 0, 0, NULL };
    for (uint32_t i = 1; i < dat->node_count; i++) {
        const uint8_t *p = b + nodes_at + (size_t)i * 8;
        int32_t child = nw_dat_s16(p), sibling = nw_dat_s16(p + 2);
        uint32_t name = nw_dat_le32(p + 4);
        if ((child > 0 ? (uint32_t)child >= dat->node_count : (uint32_t)-child >= dat->count) || sibling < 0 ||
            (uint32_t)sibling >= dat->node_count) {
            return SDL_SetError("node %u links out of range", i);
        }
        if (name >= names_size || !memchr(dat->names + name, '\0', names_size - name)) {
            return SDL_SetError("node %u name out of range", i);
        }
        dat->nodes[i] = (NwDatNode){ child, sibling, dat->names + name };
    }
    return true;
}

bool nw_dat_parse(NwDat *dat, const void *dir, size_t dir_size, uint64_t file_size)
{
    const uint8_t *b = dir;
    size_t at = NW_DAT_HEADER;

    nw_dat_free(dat);
    dat->file_size = file_size;
    if (dir_size < NW_DAT_HEADER || nw_dat_le32(b) != NW_DAT_MAGIC) {
        return SDL_SetError("bad directory magic");
    }
    uint32_t nfiles = nw_dat_le32(b + 4);
    if (nfiles > (dir_size - at) / 16) {
        return SDL_SetError("file table overruns the directory");
    }
    size_t files_at = at;
    at += (size_t)nfiles * 16;
    if (dir_size - at < 4) {
        return SDL_SetError("directory ends before the node table");
    }
    uint32_t nnodes = nw_dat_le32(b + at);
    at += 4;
    if (nnodes < 2 || nnodes > NW_DAT_NODES_MAX || nnodes > (dir_size - at) / 8) {
        return SDL_SetError("bad node table (%u nodes)", nnodes);
    }
    size_t nodes_at = at;
    at += (size_t)nnodes * 8;
    if (dir_size - at < 4) {
        return SDL_SetError("directory ends before the names");
    }
    uint32_t names_size = nw_dat_le32(b + at);
    at += 4;
    if (names_size != dir_size - at) {
        return SDL_SetError("names are %u bytes, the directory has %llu left", names_size,
                            (unsigned long long)(dir_size - at));
    }

    dat->entries = SDL_malloc(nfiles ? nfiles * sizeof(*dat->entries) : 1);
    dat->nodes = SDL_malloc(nnodes * sizeof(*dat->nodes));
    dat->names = SDL_malloc((size_t)names_size + 1);
    if (!dat->entries || !dat->nodes || !dat->names) {
        nw_dat_free(dat);
        return false;
    }
    SDL_memcpy(dat->names, b + at, names_size);
    dat->names[names_size] = '\0';
    dat->count = nfiles;
    dat->node_count = nnodes;
    if (!nw_dat_tables(dat, b, files_at, nodes_at, names_size)) {
        nw_dat_free(dat);
        return false;
    }
    return true;
}

static bool nw_dat_fail(const char *game_path)
{
    char why[256];
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    return SDL_SetError("%s: %s", game_path, why);
}

bool nw_dat_open(NwDat *dat, const NwVfs *vfs, const char *game_path)
{
    NwVfsDevice device;
    char key[NW_VFS_PATH_MAX];
    uint8_t head[NW_DAT_HEADER];

    SDL_zerop(dat);
    if (!nw_vfs_map(game_path, &device, key)) {
        return false;
    }
    const char *base = SDL_strrchr(key, '/');
    if (SDL_strcmp(base ? base + 1 : key, "DUMMY.DAT") == 0) {
        return SDL_SetError("%s is disc padding, not an archive", game_path);
    }
    dat->file = nw_vfs_open(vfs, game_path, NW_VFS_READ);
    if (!dat->file) {
        return false;
    }

    int64_t size = nw_vfs_size(dat->file);
    bool ok = size >= NW_DAT_HEADER && nw_vfs_read(dat->file, head, sizeof(head)) == NW_DAT_HEADER;
    uint32_t dir_offset = ok ? nw_dat_le32(head) : 0, dir_size = ok ? nw_dat_le32(head + 4) : 0;
    if (!ok) {
        SDL_SetError("%s is too small for an archive", game_path);
    } else if (dir_offset < NW_DAT_HEADER || dir_offset > size || dir_size > size - dir_offset ||
               dir_size > NW_DAT_DIR_MAX) {
        ok = SDL_SetError("%s: directory (offset %u, size %u) runs past the file (%lld bytes)", game_path,
                          dir_offset, dir_size, (long long)size);
    }
    uint8_t *dir = ok ? SDL_malloc(dir_size ? dir_size : 1) : NULL;
    ok = ok && dir;
    if (ok && (nw_vfs_seek(dat->file, dir_offset, 0) < 0 || nw_vfs_read(dat->file, dir, dir_size) != dir_size)) {
        ok = SDL_SetError("%s: read error", game_path);
    }
    if (ok && !nw_dat_parse(dat, dir, dir_size, (uint64_t)size)) {
        ok = nw_dat_fail(game_path);
    }
    SDL_free(dir);
    if (!ok) {
        nw_dat_close(dat);
    }
    return ok;
}

void nw_dat_close(NwDat *dat)
{
    nw_vfs_close(dat->file);
    nw_dat_free(dat);
    SDL_zerop(dat);
}

int nw_dat_find(const NwDat *dat, const char *path)
{
    uint8_t seen[NW_DAT_NODES_MAX / 8];
    int32_t node = 1;

    if (dat->node_count < 2) {
        return -1;
    }
    SDL_memset(seen, 0, (dat->node_count + 7) / 8);
    while (node != 0 && !nw_dat_seen(seen, node)) { /* the guard stops at the node-2 link back to the root */
        const NwDatNode *n = &dat->nodes[node];
        size_t len = strcspn(path, "\\");
        if (SDL_strlen(n->name) == len && SDL_strncasecmp(n->name, path, len) == 0) {
            if (path[len] == '\0') {
                return n->child <= 0 ? -n->child : -1;
            }
            if (n->child <= 0) {
                return -1;
            }
            path += len + 1;
            node = n->child;
        } else {
            node = n->sibling;
        }
    }
    return -1;
}

void *nw_dat_load(NwDat *dat, int index)
{
    if (!dat->file || index < 0 || (uint32_t)index >= dat->count) {
        SDL_SetError("no .DAT entry %d", index);
        return NULL;
    }
    const NwDatEntry *e = &dat->entries[index];
    uint8_t *data = SDL_malloc(e->size ? e->size : 1);
    if (data && (nw_vfs_seek(dat->file, e->offset, 0) < 0 || nw_vfs_read(dat->file, data, e->size) != e->size)) {
        SDL_free(data);
        SDL_SetError("read error in .DAT entry %d", index);
        return NULL;
    }
    return data;
}

static bool nw_dat_walk(NwDatWalk *w, int32_t node, size_t len, int depth)
{
    if (depth > NW_DAT_DEPTH_MAX) {
        return SDL_SetError(".DAT tree is too deep");
    }
    for (; node != 0 && !nw_dat_seen(w->seen, node); node = w->dat->nodes[node].sibling) {
        const NwDatNode *n = &w->dat->nodes[node];
        size_t nlen = SDL_strlen(n->name);
        if (len + nlen + 2 > NW_DAT_PATH_MAX) {
            return SDL_SetError(".DAT path too long");
        }
        SDL_memcpy(w->path + len, n->name, nlen + 1);
        if (n->child > 0) {
            w->path[len + nlen] = '\\';
            if (!nw_dat_walk(w, n->child, len + nlen + 1, depth + 1)) {
                return false;
            }
        } else {
            w->fn(w->user, -n->child, w->path);
        }
    }
    return true;
}

bool nw_dat_list(const NwDat *dat, void (*fn)(void *user, int index, const char *path), void *user)
{
    NwDatWalk *w = SDL_calloc(1, sizeof(*w));
    if (!w) {
        return false;
    }
    w->dat = dat;
    w->fn = fn;
    w->user = user;
    bool ok = dat->node_count >= 2 && nw_dat_walk(w, 1, 0, 0);
    SDL_free(w);
    return ok;
}
