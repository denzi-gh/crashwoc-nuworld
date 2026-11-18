#include "updater/apply.h"

#include <SDL3/SDL.h>

#include "monocypher-ed25519.h"
#include "platform/update.h"

typedef struct NwSwapList {
    char **names;
    size_t count;
    const char *root;
    const char *prefix;
} NwSwapList;

bool nw_updater_signed(const uint8_t (*keys)[32], size_t count, const void *message, size_t size,
                       const void *signature, size_t signature_size)
{
    if (signature_size != 64) {
        return SDL_SetError("the release's signature has %d bytes, not 64", (int)signature_size);
    }
    for (size_t i = 0; i < count; i++) {
        if (crypto_ed25519_check(signature, keys[i], message, size) == 0) {
            return true;
        }
    }
    return SDL_SetError("the release is not signed with NuWorld's key");
}

bool nw_updater_listed(const char *sums, const char *name, char hex[NW_SHA256_HEX])
{
    size_t want = SDL_strlen(name);
    for (const char *line = sums; line && *line; line = SDL_strchr(line, '\n')) {
        line += *line == '\n';
        const char *end = SDL_strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : SDL_strlen(line);
        len -= len && line[len - 1] == '\r';
        if (len != NW_SHA256_HEX - 1 + 2 + want || line[64] != ' ' || (line[65] != ' ' && line[65] != '*') ||
            SDL_strncmp(line + 66, name, want) != 0) {
            continue;
        }
        for (int i = 0; i < 64; i++) {
            if (!SDL_isxdigit((unsigned char)line[i])) {
                return SDL_SetError("SHA256SUMS has no hash for %s", name);
            }
            hex[i] = (char)SDL_tolower((unsigned char)line[i]);
        }
        hex[64] = '\0';
        return true;
    }
    return SDL_SetError("SHA256SUMS does not list %s", name);
}

bool nw_updater_file_hash(const char *path, char hex[NW_SHA256_HEX])
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    uint8_t *buf = SDL_malloc(1 << 16), digest[NW_SHA256_SIZE];
    bool ok = io && buf;
    NwSha256 sha;
    nw_sha256_init(&sha);
    for (size_t n = 1; ok && n;) {
        n = SDL_ReadIO(io, buf, 1 << 16);
        nw_sha256_update(&sha, buf, n);
        ok = n || SDL_GetIOStatus(io) == SDL_IO_STATUS_EOF;
    }
    nw_sha256_final(&sha, digest);
    nw_sha256_hex(digest, hex);
    SDL_free(buf);
    if (io) {
        SDL_CloseIO(io);
    }
    return ok;
}

static SDL_EnumerationResult SDLCALL nw_swap_collect(void *user, const char *dir, const char *name)
{
    NwSwapList *list = user;
    char *path = NULL, *rel = NULL;
    SDL_PathInfo info;
    SDL_EnumerationResult result = SDL_ENUM_FAILURE;
    if (SDL_asprintf(&path, "%s%s", dir, name) >= 0 && SDL_asprintf(&rel, "%s%s", list->prefix, name) >= 0 &&
        SDL_GetPathInfo(path, &info)) {
        if (info.type == SDL_PATHTYPE_DIRECTORY) {
            char *inner = NULL, *sub = NULL;
            const char *outer = list->prefix;
            if (SDL_asprintf(&inner, "%s/", rel) >= 0 && SDL_asprintf(&sub, "%s/", path) >= 0) {
                list->prefix = inner;
                result = SDL_EnumerateDirectory(sub, nw_swap_collect, list) ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
                list->prefix = outer;
            }
            SDL_free(inner);
            SDL_free(sub);
        } else {
            char **names = SDL_realloc(list->names, (list->count + 1) * sizeof(*names));
            if (names) {
                list->names = names;
                list->names[list->count++] = rel;
                rel = NULL;
                result = SDL_ENUM_CONTINUE;
            }
        }
    }
    SDL_free(path);
    SDL_free(rel);
    return result;
}

static bool nw_swap_parent(char *path, size_t root)
{
    char *slash = SDL_strrchr(path + root, '/');
    if (!slash) {
        return true;
    }
    *slash = '\0';
    bool ok = SDL_CreateDirectory(path);
    *slash = '/';
    return ok;
}

static bool nw_swap_same(const char *a, const char *b)
{
    char one[NW_SHA256_HEX], other[NW_SHA256_HEX];
    return SDL_GetPathInfo(b, NULL) && nw_updater_file_hash(a, one) && nw_updater_file_hash(b, other) &&
           SDL_strcmp(one, other) == 0;
}

static bool nw_swap_paths(const char *from, const char *install, const char *name, char **src, char **dst, char **old)
{
    *src = *dst = *old = NULL;
    return SDL_asprintf(src, "%s%s", from, name) >= 0 && SDL_asprintf(dst, "%s%s", install, name) >= 0 &&
           SDL_asprintf(old, "%s" NW_UPDATE_OLD, *dst) >= 0;
}

bool nw_updater_swap(const char *from, const char *install, char **replaced)
{
    enum { NW_SWAP_SAME, NW_SWAP_ADDED, NW_SWAP_REPLACED };
    NwSwapList list = { .prefix = "" };
    size_t done = 0;
    uint8_t *how = NULL;
    char *note = NULL;
    bool ok = SDL_EnumerateDirectory(from, nw_swap_collect, &list) &&
              (list.count || SDL_SetError("the update is empty"));
    if (ok) {
        how = SDL_calloc(list.count, sizeof(*how));
        ok = how != NULL;
    }
    for (; ok && done < list.count; done++) {
        char *src, *dst, *old, *grown = NULL;
        ok = nw_swap_paths(from, install, list.names[done], &src, &dst, &old);
        if (ok && nw_swap_same(src, dst)) {
            SDL_RemovePath(src);
        } else if (ok) {
            how[done] = NW_SWAP_ADDED;
            ok = nw_swap_parent(dst, SDL_strlen(install));
            if (ok && SDL_GetPathInfo(dst, NULL)) {
                SDL_RemovePath(old);
                ok = SDL_RenamePath(dst, old);
                if (ok) {
                    how[done] = NW_SWAP_REPLACED;
                    if (SDL_asprintf(&grown, "%s%s\n", note ? note : "", list.names[done]) >= 0) {
                        SDL_free(note);
                        note = grown;
                    }
                }
            }
            if (ok && !SDL_RenamePath(src, dst)) {
                ok = false;
                if (how[done] == NW_SWAP_REPLACED) {
                    SDL_RenamePath(old, dst);
                }
            }
        }
        SDL_free(src);
        SDL_free(dst);
        SDL_free(old);
        if (!ok) {
            break;
        }
    }
    if (!ok && how) {
        char *why = SDL_strdup(SDL_GetError());
        while (done--) {
            char *src = NULL, *dst = NULL, *old = NULL;
            if (how[done] != NW_SWAP_SAME && nw_swap_paths(from, install, list.names[done], &src, &dst, &old)) {
                SDL_RenamePath(dst, src);
                if (how[done] == NW_SWAP_REPLACED) {
                    SDL_RenamePath(old, dst);
                }
            }
            SDL_free(src);
            SDL_free(dst);
            SDL_free(old);
        }
        SDL_SetError("%s", why ? why : "the files could not be replaced");
        SDL_free(why);
    }
    for (size_t i = 0; i < list.count; i++) {
        SDL_free(list.names[i]);
    }
    SDL_free(list.names);
    SDL_free(how);
    if (ok && replaced) {
        *replaced = note ? note : SDL_strdup("");
        note = NULL;
    }
    SDL_free(note);
    return ok;
}
