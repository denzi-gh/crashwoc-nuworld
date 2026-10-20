#include "vfs/extract.h"

#include <SDL3/SDL.h>

#include "platform/paths.h"

#define NW_EXTRACT_CHUNK (1u << 20)

typedef struct NwCopy {
    NwProgressFn fn;
    void *user;
    uint64_t done;
    uint64_t total;
    uint8_t *buf;
} NwCopy;

/* Prefixes the current error with what failed. */
static bool nw_extract_fail(const char *what, const char *path)
{
    char why[512];
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    return SDL_SetError("%s %s: %s", what, path, why);
}

/* <root>name, with a separator between. */
static char *nw_extract_path(const char *root, const char *name)
{
    char *path = NULL;
    size_t n = SDL_strlen(root);
    bool sep = n > 0 && (root[n - 1] == '/' || root[n - 1] == '\\');
    return SDL_asprintf(&path, "%s%s%s", root, sep ? "" : "/", name) < 0 ? NULL : path;
}

static void nw_extract_progress(NwCopy *c, size_t n)
{
    c->done += n;
    if (c->fn) {
        c->fn(c->user, c->done, c->total);
    }
}

static bool nw_extract_file(NwDisc *disc, const NwIsoFile *f, const NwManifestEntry *e, const char *dir, NwCopy *c)
{
    uint8_t digest[NW_SHA256_SIZE];
    NwSha256 sha;
    char *dest = nw_extract_path(dir, e->path);
    if (!dest) {
        return false;
    }

    char *slash = SDL_strrchr(dest, '/');
    *slash = '\0';
    bool ok = SDL_CreateDirectory(dest) || nw_extract_fail("cannot create", dest);
    *slash = '/';
    SDL_IOStream *out = ok ? SDL_IOFromFile(dest, "wb") : NULL;
    if (ok && !out) {
        ok = nw_extract_fail("cannot create", dest);
    }

    nw_sha256_init(&sha);
    for (uint64_t offset = 0; ok && offset < f->size;) {
        size_t n = f->size - offset < NW_EXTRACT_CHUNK ? (size_t)(f->size - offset) : NW_EXTRACT_CHUNK;
        ok = nw_iso_read(disc, f, offset, c->buf, n);
        if (ok && SDL_WriteIO(out, c->buf, n) != n) {
            ok = nw_extract_fail("cannot write", dest);
        }
        if (ok) {
            nw_sha256_update(&sha, c->buf, n);
            offset += n;
            nw_extract_progress(c, n);
        }
    }
    if (out && !SDL_CloseIO(out) && ok) {
        ok = nw_extract_fail("cannot write", dest);
    }
    if (ok) {
        nw_sha256_final(&sha, digest);
        ok = nw_verify_digest(e, digest);
    }
    SDL_free(dest);
    return ok;
}

static bool nw_extract_stamp(const char *dir, const NwManifest *m, size_t files)
{
    const NwManifestEntry *elf = nw_manifest_find(m, NW_DISC_BOOT);
    char *path = nw_extract_path(dir, NW_DISC_STAMP), *text = NULL;
    bool ok = path && SDL_asprintf(&text, "nuworld-disc 1\nboot %s\nver %s\nelf-sha256 %s\nfiles %zu\n", NW_DISC_BOOT,
                                   NW_DISC_VER, elf ? elf->sha256 : "-", files) >= 0;
    ok = ok && (SDL_SaveFile(path, text, SDL_strlen(text)) || nw_extract_fail("cannot write", path));
    SDL_free(text);
    SDL_free(path);
    return ok;
}

/* partial -> final; an existing final is moved to old first and restored if the rename fails. */
static bool nw_extract_swap(const char *partial, const char *final, const char *old)
{
    bool had = SDL_GetPathInfo(final, NULL);
    if (had && (!nw_paths_remove_tree(old) || !SDL_RenamePath(final, old))) {
        return nw_extract_fail("cannot move aside", final);
    }
    if (!SDL_RenamePath(partial, final)) {
        nw_extract_fail("cannot rename", partial);
        if (had) {
            char why[512];
            SDL_strlcpy(why, SDL_GetError(), sizeof(why));
            SDL_RenamePath(old, final);
            SDL_SetError("%s", why);
        }
        return false;
    }
    if (had) {
        nw_paths_remove_tree(old);
    }
    return true;
}

bool nw_extract_disc(NwDisc *disc, const NwIso *iso, const NwManifest *m, const char *root, NwProgressFn fn,
                     void *user)
{
    NwCopy c = { fn, user, 0, 0, NULL };
    size_t files = 0;

    if (!nw_verify_identity(disc, iso, m) || !nw_verify_listing(iso, m)) {
        return false;
    }
    for (size_t i = 0; i < m->count; i++) {
        if (!(m->entries[i].flags & NW_MANIFEST_SKIP)) {
            c.total += m->entries[i].size;
            files++;
        }
    }

    char *partial = nw_extract_path(root, NW_DISC_DIR ".partial");
    char *final = nw_extract_path(root, NW_DISC_DIR);
    char *old = nw_extract_path(root, NW_DISC_DIR ".old");
    char *dir = partial ? nw_paths_dir(partial, NULL, false) : NULL;
    c.buf = SDL_malloc(NW_EXTRACT_CHUNK);
    bool ok = partial && final && old && dir && c.buf && nw_paths_remove_tree(partial);
    ok = ok && (SDL_CreateDirectory(partial) || nw_extract_fail("cannot create", partial));
    for (size_t i = 0; ok && i < m->count; i++) {
        const NwManifestEntry *e = &m->entries[i];
        if (!(e->flags & NW_MANIFEST_SKIP)) {
            ok = nw_extract_file(disc, nw_iso_find(iso, e->path), e, dir, &c);
        }
    }
    ok = ok && nw_extract_stamp(dir, m, files);
    if (!ok && partial) {
        char why[512];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        nw_paths_remove_tree(partial);
        SDL_SetError("%s", why);
    }
    ok = ok && nw_extract_swap(partial, final, old);

    SDL_free(c.buf);
    SDL_free(dir);
    SDL_free(old);
    SDL_free(final);
    SDL_free(partial);
    return ok;
}

bool nw_extract_verify(const char *root, const NwManifest *m, NwProgressFn fn, void *user)
{
    NwCopy c = { fn, user, 0, 0, NULL };
    char *dir = nw_extract_path(root, NW_DISC_DIR "/");
    char *stamp = dir ? nw_extract_path(dir, NW_DISC_STAMP) : NULL;
    bool ok = stamp != NULL;

    if (ok && !SDL_GetPathInfo(stamp, NULL)) {
        ok = SDL_SetError("no imported disc in %s", dir);
    }
    for (size_t i = 0; i < m->count; i++) {
        if (!(m->entries[i].flags & NW_MANIFEST_SKIP)) {
            c.total += m->entries[i].size;
        }
    }
    c.buf = ok ? SDL_malloc(NW_EXTRACT_CHUNK) : NULL;
    ok = ok && c.buf;

    for (size_t i = 0; ok && i < m->count; i++) {
        const NwManifestEntry *e = &m->entries[i];
        uint8_t digest[NW_SHA256_SIZE];
        NwSha256 sha;
        if (e->flags & NW_MANIFEST_SKIP) {
            continue;
        }
        char *path = nw_extract_path(dir, e->path);
        SDL_IOStream *in = path ? SDL_IOFromFile(path, "rb") : NULL;
        SDL_free(path);
        if (!in) {
            ok = SDL_SetError("%s is missing", e->path);
            break;
        }
        Sint64 size = SDL_GetIOSize(in);
        if (size < 0 || (uint64_t)size != e->size) {
            ok = SDL_SetError("%s is %lld bytes, expected %llu", e->path, (long long)size, (unsigned long long)e->size);
        }
        nw_sha256_init(&sha);
        for (uint64_t offset = 0; ok && offset < e->size;) {
            size_t n = e->size - offset < NW_EXTRACT_CHUNK ? (size_t)(e->size - offset) : NW_EXTRACT_CHUNK;
            if (SDL_ReadIO(in, c.buf, n) != n) {
                ok = SDL_SetError("read error in %s", e->path);
                break;
            }
            nw_sha256_update(&sha, c.buf, n);
            offset += n;
            nw_extract_progress(&c, n);
        }
        SDL_CloseIO(in);
        if (ok) {
            nw_sha256_final(&sha, digest);
            ok = nw_verify_digest(e, digest);
        }
    }
    SDL_free(c.buf);
    SDL_free(stamp);
    SDL_free(dir);
    return ok;
}
