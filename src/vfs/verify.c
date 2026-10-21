#include "vfs/verify.h"

#include <string.h>

#include <SDL3/SDL.h>

#define NW_VERIFY_CHUNK (256u << 10)
#define NW_CNF_MAX 4096

typedef struct NwCnf {
    char boot2[128]; /* PS2 */
    char boot[128];  /* PS1 */
    char ver[32];
    char vmode[32];
} NwCnf;

typedef struct NwProgress {
    NwProgressFn fn;
    void *user;
    uint64_t done;
    uint64_t total;
} NwProgress;

/* [p, end) without surrounding blanks, truncated to size. */
static void nw_cnf_copy(char *out, size_t size, const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    size_t n = (size_t)(end - p) < size - 1 ? (size_t)(end - p) : size - 1;
    SDL_memcpy(out, p, n);
    out[n] = '\0';
}

/* KEY = VALUE lines. */
static void nw_cnf_parse(const char *text, NwCnf *cnf)
{
    SDL_zerop(cnf);
    for (const char *line = text; *line;) {
        size_t len = strcspn(line, "\r\n");
        const char *eq = memchr(line, '=', len);
        if (eq) {
            char key[16];
            nw_cnf_copy(key, sizeof(key), line, eq);
            const char *value = eq + 1, *end = line + len;
            if (SDL_strcasecmp(key, "BOOT2") == 0) {
                nw_cnf_copy(cnf->boot2, sizeof(cnf->boot2), value, end);
            } else if (SDL_strcasecmp(key, "BOOT") == 0) {
                nw_cnf_copy(cnf->boot, sizeof(cnf->boot), value, end);
            } else if (SDL_strcasecmp(key, "VER") == 0) {
                nw_cnf_copy(cnf->ver, sizeof(cnf->ver), value, end);
            } else if (SDL_strcasecmp(key, "VMODE") == 0) {
                nw_cnf_copy(cnf->vmode, sizeof(cnf->vmode), value, end);
            }
        }
        line += len;
        line += strspn(line, "\r\n");
    }
}

/* Region from the product code (SLUS_202.38 -> US). */
static const char *nw_cnf_region(const char *boot)
{
    static const struct {
        const char *code;
        const char *region;
    } regions[] = {
        { "US", "US disc" },
        { "PS", "Japanese disc" },
        { "PM", "Japanese disc" },
        { "AJ", "Asian disc" },
        { "KA", "Korean disc" },
    };
    if (SDL_strlen(boot) >= 4) {
        for (size_t i = 0; i < SDL_arraysize(regions); i++) {
            if (SDL_strncasecmp(boot + 2, regions[i].code, 2) == 0) {
                return regions[i].region;
            }
        }
    }
    return "a different game";
}

static bool nw_verify_hash(NwDisc *disc, const NwIsoFile *f, uint8_t *buf, uint8_t digest[NW_SHA256_SIZE],
                           NwProgress *progress)
{
    NwSha256 s;
    nw_sha256_init(&s);
    for (uint64_t offset = 0; offset < f->size;) {
        size_t n = f->size - offset < NW_VERIFY_CHUNK ? (size_t)(f->size - offset) : NW_VERIFY_CHUNK;
        if (!nw_iso_read(disc, f, offset, buf, n)) {
            return false;
        }
        nw_sha256_update(&s, buf, n);
        offset += n;
        if (progress) {
            progress->done += n;
            if (progress->fn && !progress->fn(progress->user, progress->done, progress->total)) {
                return SDL_SetError("cancelled");
            }
        }
    }
    nw_sha256_final(&s, digest);
    return true;
}

bool nw_verify_digest(const NwManifestEntry *e, const uint8_t digest[NW_SHA256_SIZE])
{
    char hex[NW_SHA256_HEX];
    nw_sha256_hex(digest, hex);
    if (SDL_strcmp(hex, e->sha256) != 0) {
        return SDL_SetError("%s: SHA-256 %s, expected %s (bad dump or modified file)", e->path, hex, e->sha256);
    }
    return true;
}

bool nw_verify_identity(NwDisc *disc, const NwIso *iso, const NwManifest *m)
{
    char text[NW_CNF_MAX + 1], boot[64];
    uint8_t digest[NW_SHA256_SIZE];
    NwCnf cnf;

    const NwIsoFile *f = nw_iso_find(iso, "SYSTEM.CNF");
    if (!f) {
        return SDL_SetError("no SYSTEM.CNF: not a PS2 game disc");
    }
    if (f->size > NW_CNF_MAX) {
        return SDL_SetError("SYSTEM.CNF is %u bytes: not a PS2 game disc", f->size);
    }
    if (!nw_iso_read(disc, f, 0, text, f->size)) {
        return false;
    }
    text[f->size] = '\0';
    nw_cnf_parse(text, &cnf);

    if (!cnf.boot2[0]) {
        if (cnf.boot[0]) {
            return SDL_SetError("BOOT = %s: a PS1 disc, not PS2", cnf.boot);
        }
        return SDL_SetError("SYSTEM.CNF has no BOOT2 line: not a PS2 game disc");
    }
    if (SDL_strncasecmp(cnf.boot2, "cdrom0:\\", 8) != 0) {
        return SDL_SetError("BOOT2 = %s: not a disc boot path", cnf.boot2);
    }
    nw_cnf_copy(boot, sizeof(boot), cnf.boot2 + 8, cnf.boot2 + 8 + strcspn(cnf.boot2 + 8, ";"));
    if (SDL_strcmp(boot, NW_DISC_BOOT) != 0) {
        return SDL_SetError("%s: %s; only the PAL disc " NW_DISC_BOOT " v" NW_DISC_VER " is supported", boot,
                            nw_cnf_region(boot));
    }
    if (SDL_strcmp(cnf.ver, NW_DISC_VER) != 0) {
        return SDL_SetError("VER %s: only PAL v" NW_DISC_VER " is supported", cnf.ver[0] ? cnf.ver : "missing");
    }
    if (SDL_strcmp(cnf.vmode, NW_DISC_VMODE) != 0) {
        return SDL_SetError("VMODE %s: only the PAL disc is supported", cnf.vmode[0] ? cnf.vmode : "missing");
    }

    const NwManifestEntry *e = nw_manifest_find(m, "SYSTEM.CNF");
    if (!e) {
        return SDL_SetError("the manifest has no SYSTEM.CNF");
    }
    nw_sha256(text, f->size, digest);
    if (f->size != e->size || !nw_verify_digest(e, digest)) {
        return SDL_SetError("SYSTEM.CNF differs from PAL v" NW_DISC_VER);
    }

    e = nw_manifest_find(m, boot);
    f = nw_iso_find(iso, boot);
    if (!e) {
        return SDL_SetError("the manifest has no %s", boot);
    }
    if (!f) {
        return SDL_SetError("%s is missing", boot);
    }
    if (f->size != e->size) {
        return SDL_SetError("%s is %u bytes, expected %llu: not PAL v" NW_DISC_VER " or a bad dump", boot, f->size,
                            (unsigned long long)e->size);
    }
    uint8_t *buf = SDL_malloc(NW_VERIFY_CHUNK);
    bool ok = buf && nw_verify_hash(disc, f, buf, digest, NULL) && nw_verify_digest(e, digest);
    SDL_free(buf);
    return ok;
}

static bool nw_verify_hashed(const NwManifestEntry *e, unsigned flags)
{
    return !(e->flags & NW_MANIFEST_SKIP) || (flags & NW_VERIFY_SKIPPED);
}

bool nw_verify_listing(const NwIso *iso, const NwManifest *m)
{
    for (uint32_t i = 0; i < iso->count; i++) {
        if (!nw_manifest_find(m, iso->files[i].path)) {
            return SDL_SetError("%s is not part of PAL v" NW_DISC_VER, iso->files[i].path);
        }
    }
    for (size_t i = 0; i < m->count; i++) {
        const NwManifestEntry *e = &m->entries[i];
        const NwIsoFile *f = nw_iso_find(iso, e->path);
        if (!f) {
            return SDL_SetError("%s is missing", e->path);
        }
        if (f->size != e->size) {
            return SDL_SetError("%s is %u bytes, expected %llu", e->path, f->size, (unsigned long long)e->size);
        }
    }
    return true;
}

bool nw_verify_files(NwDisc *disc, const NwIso *iso, const NwManifest *m, unsigned flags, NwProgressFn fn,
                     void *user)
{
    NwProgress progress = { fn, user, 0, 0 };
    uint8_t digest[NW_SHA256_SIZE];

    if (!nw_verify_listing(iso, m)) {
        return false;
    }
    for (size_t i = 0; i < m->count; i++) {
        if (nw_verify_hashed(&m->entries[i], flags)) {
            progress.total += m->entries[i].size;
        }
    }

    uint8_t *buf = SDL_malloc(NW_VERIFY_CHUNK);
    if (!buf) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < m->count; i++) {
        const NwManifestEntry *e = &m->entries[i];
        if (nw_verify_hashed(e, flags)) {
            ok = nw_verify_hash(disc, nw_iso_find(iso, e->path), buf, digest, &progress) &&
                 nw_verify_digest(e, digest);
        }
    }
    SDL_free(buf);
    return ok;
}
