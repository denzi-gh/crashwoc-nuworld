#include "vfs/iso9660.h"

#include <SDL3/SDL.h>

#define NW_ISO_DEPTH_MAX 32
#define NW_ISO_DIR_MAX (16u << 20) /* sanity limit for one directory extent */
#define NW_ISO_FLAG_DIR 0x02
#define NW_ISO_FLAG_MULTI 0x80

typedef struct NwIsoEntry {
    char name[256];
    uint32_t lba;
    uint32_t size;
    bool dir;
} NwIsoEntry;

typedef struct NwIsoBuild {
    NwDisc *disc;
    NwIso *iso;
    uint32_t cap;
} NwIsoBuild;

static uint32_t nw_iso_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Up to the ';' version, uppercase. Refuses names that cannot be one path component. */
static bool nw_iso_name(const uint8_t *raw, uint8_t len, char out[256])
{
    size_t n = 0;
    for (; n < len && raw[n] != ';'; n++) {
        if (raw[n] == 0 || raw[n] == '/' || raw[n] == '\\') {
            return false;
        }
        out[n] = (char)SDL_toupper(raw[n]);
    }
    out[n] = '\0';
    return n > 0 && SDL_strcmp(out, ".") != 0 && SDL_strcmp(out, "..") != 0;
}

static bool nw_iso_add(NwIsoBuild *b, const char *path, uint32_t lba, uint32_t size)
{
    NwIso *iso = b->iso;
    if (iso->count == b->cap) {
        uint32_t cap = b->cap ? b->cap * 2 : 256;
        NwIsoFile *files = SDL_realloc(iso->files, cap * sizeof(*files));
        if (!files) {
            return false;
        }
        iso->files = files;
        b->cap = cap;
    }
    char *copy = SDL_strdup(path);
    if (!copy) {
        return false;
    }
    iso->files[iso->count++] = (NwIsoFile){ copy, lba, size };
    return true;
}

static bool nw_iso_parse_dir(const uint8_t *data, uint32_t length, const char *where, NwIsoEntry **out,
                             size_t *count)
{
    NwIsoEntry *ents = NULL;
    size_t n = 0, cap = 0;
    uint32_t p = 0;

    while (p < length) {
        const uint8_t *r = data + p;
        uint32_t rec = r[0];
        NwIsoEntry e;

        if (rec == 0) { /* records never cross a sector: the rest of this one is padding */
            p = (p / NW_DISC_SECTOR + 1) * NW_DISC_SECTOR;
            continue;
        }
        if (rec < 34 || rec > length - p || 33u + r[32] > rec) {
            SDL_free(ents);
            return SDL_SetError("bad directory record in %s", where);
        }
        p += rec;
        if (r[32] == 1 && (r[33] == 0 || r[33] == 1)) { /* "." and ".." */
            continue;
        }
        if (!nw_iso_name(r + 33, r[32], e.name)) {
            SDL_free(ents);
            return SDL_SetError("bad file name in %s", where);
        }
        if (r[25] & NW_ISO_FLAG_MULTI) {
            SDL_free(ents);
            return SDL_SetError("%s in %s is a multi-extent file: not supported", e.name, where);
        }
        if (r[26] || r[27]) {
            SDL_free(ents);
            return SDL_SetError("%s in %s is interleaved: not supported", e.name, where);
        }
        for (size_t i = 0; i < n; i++) {
            if (SDL_strcmp(ents[i].name, e.name) == 0) {
                SDL_free(ents);
                return SDL_SetError("duplicate name %s in %s", e.name, where);
            }
        }
        e.lba = nw_iso_le32(r + 2);
        e.size = nw_iso_le32(r + 10);
        e.dir = (r[25] & NW_ISO_FLAG_DIR) != 0;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            NwIsoEntry *grown = SDL_realloc(ents, cap * sizeof(*ents));
            if (!grown) {
                SDL_free(ents);
                return false;
            }
            ents = grown;
        }
        ents[n++] = e;
    }
    *out = ents;
    *count = n;
    return true;
}

static bool nw_iso_walk(NwIsoBuild *b, uint32_t lba, uint32_t length, const char *prefix, int depth)
{
    const char *where = prefix[0] ? prefix : "/";
    char path[NW_ISO_PATH_MAX + 1];
    NwIsoEntry *ents = NULL;
    size_t count = 0;

    if (depth > NW_ISO_DEPTH_MAX) {
        return SDL_SetError("directories nested too deep at %s", where);
    }
    if (length == 0) {
        length = NW_DISC_SECTOR;
    }
    if (length > NW_ISO_DIR_MAX) {
        return SDL_SetError("directory %s is too large", where);
    }
    uint32_t nsect = (length + NW_DISC_SECTOR - 1) / NW_DISC_SECTOR;
    uint8_t *data = SDL_malloc((size_t)nsect * NW_DISC_SECTOR);
    bool ok = data && nw_disc_read(b->disc, lba, nsect, data) &&
              nw_iso_parse_dir(data, length, where, &ents, &count);
    SDL_free(data);

    for (size_t i = 0; ok && i < count; i++) {
        const NwIsoEntry *e = &ents[i];
        int len = SDL_snprintf(path, sizeof(path), "%s%s%s", prefix, e->name, e->dir ? "/" : "");
        if (len < 0 || len >= NW_ISO_PATH_MAX) {
            ok = SDL_SetError("path too long in %s", where);
        } else if (e->dir) {
            ok = nw_iso_walk(b, e->lba, e->size, path, depth + 1);
        } else if (e->size > 0 && (e->lba > b->disc->sectors ||
                                   (e->size - 1) / NW_DISC_SECTOR >= b->disc->sectors - e->lba)) {
            ok = SDL_SetError("%s lies past the end of the image: truncated dump?", path);
        } else {
            ok = nw_iso_add(b, path, e->lba, e->size);
        }
    }
    SDL_free(ents);
    return ok;
}

static int SDLCALL nw_iso_cmp(const void *a, const void *b)
{
    return SDL_strcmp(((const NwIsoFile *)a)->path, ((const NwIsoFile *)b)->path);
}

bool nw_iso_index(NwIso *iso, NwDisc *disc)
{
    uint8_t pvd[NW_DISC_SECTOR];

    SDL_zerop(iso);
    if (!nw_disc_read(disc, 16, 1, pvd)) {
        return false;
    }
    if (pvd[0] != 1 || SDL_memcmp(pvd + 1, "CD001", 5) != 0) {
        return SDL_SetError("not an ISO9660 image");
    }
    unsigned block = pvd[128] | pvd[129] << 8;
    if (block != NW_DISC_SECTOR) {
        return SDL_SetError("logical block size %u is not supported", block);
    }
    SDL_memcpy(iso->volume_id, pvd + 40, 32);
    for (int i = 31; i >= 0 && (iso->volume_id[i] == ' ' || iso->volume_id[i] == '\0'); i--) {
        iso->volume_id[i] = '\0';
    }

    const uint8_t *root = pvd + 156;
    NwIsoBuild b = { disc, iso, 0 };
    if (!nw_iso_walk(&b, nw_iso_le32(root + 2), nw_iso_le32(root + 10), "", 0)) {
        nw_iso_free(iso);
        return false;
    }
    SDL_qsort(iso->files, iso->count, sizeof(*iso->files), nw_iso_cmp);
    return true;
}

void nw_iso_free(NwIso *iso)
{
    for (uint32_t i = 0; i < iso->count; i++) {
        SDL_free(iso->files[i].path);
    }
    SDL_free(iso->files);
    SDL_zerop(iso);
}

const NwIsoFile *nw_iso_find(const NwIso *iso, const char *path)
{
    char key[NW_ISO_PATH_MAX];
    size_t n = 0;

    while (*path == '/' || *path == '\\') {
        path++;
    }
    for (; *path && *path != ';'; path++) {
        if (n + 1 >= sizeof(key)) {
            return NULL;
        }
        key[n++] = *path == '\\' ? '/' : (char)SDL_toupper((unsigned char)*path);
    }
    key[n] = '\0';

    uint32_t lo = 0, hi = iso->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = SDL_strcmp(iso->files[mid].path, key);
        if (c == 0) {
            return &iso->files[mid];
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

bool nw_iso_read(NwDisc *disc, const NwIsoFile *file, uint64_t offset, void *dst, size_t size)
{
    uint8_t sector[NW_DISC_SECTOR];
    uint8_t *out = dst;

    if (offset > file->size || size > file->size - offset) {
        return SDL_SetError("read past the end of %s", file->path);
    }
    while (size > 0) {
        uint32_t lsn = file->lba + (uint32_t)(offset / NW_DISC_SECTOR);
        size_t skip = (size_t)(offset % NW_DISC_SECTOR);
        size_t n;
        if (skip == 0 && size >= NW_DISC_SECTOR) {
            uint32_t count = (uint32_t)(size / NW_DISC_SECTOR);
            n = (size_t)count * NW_DISC_SECTOR;
            if (!nw_disc_read(disc, lsn, count, out)) {
                return false;
            }
        } else {
            n = NW_DISC_SECTOR - skip < size ? NW_DISC_SECTOR - skip : size;
            if (!nw_disc_read(disc, lsn, 1, sector)) {
                return false;
            }
            SDL_memcpy(out, sector + skip, n);
        }
        out += n;
        offset += n;
        size -= n;
    }
    return true;
}
