#include "platform/paths.h"

#include <SDL3/SDL.h>

typedef struct NwNames {
    char **names;
    size_t count;
    size_t cap;
} NwNames;

static bool nw_paths_sep(char c)
{
    return c == '/' || c == '\\';
}

char *nw_paths_data_root(const char *base_dir, const char *pref_dir)
{
    char *portable = NULL;
    if (base_dir && SDL_asprintf(&portable, "%s" NW_PATHS_PORTABLE, base_dir) >= 0) {
        bool found = SDL_GetPathInfo(portable, NULL);
        SDL_free(portable);
        if (found) {
            return nw_paths_dir(base_dir, "user", false);
        }
    }
    return pref_dir ? SDL_strdup(pref_dir) : SDL_GetPrefPath(NULL, NW_PATHS_APP);
}

char *nw_paths_dir(const char *dir, const char *name, bool create)
{
    char *path = NULL;
    size_t n = SDL_strlen(dir);
    const char *sep = n == 0 || nw_paths_sep(dir[n - 1]) ? "" : "/";

    if (SDL_asprintf(&path, "%s%s%s%s", dir, sep, name ? name : "", name ? "/" : "") < 0) {
        return NULL;
    }
    if (create && !SDL_CreateDirectory(path)) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        SDL_SetError("cannot create %s: %s", path, why);
        SDL_free(path);
        return NULL;
    }
    return path;
}

static SDL_EnumerationResult SDLCALL nw_paths_collect(void *user, const char *dir, const char *name)
{
    NwNames *list = user;
    (void)dir;
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        char **names = SDL_realloc(list->names, cap * sizeof(*names));
        if (!names) {
            return SDL_ENUM_FAILURE;
        }
        list->names = names;
        list->cap = cap;
    }
    list->names[list->count] = SDL_strdup(name);
    return list->names[list->count++] ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

bool nw_paths_remove_tree(const char *path)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        return true;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        NwNames list = { NULL, 0, 0 };
        bool ok = SDL_EnumerateDirectory(path, nw_paths_collect, &list);
        for (size_t i = 0; i < list.count; i++) {
            char *child = NULL;
            if (ok && list.names[i] && SDL_asprintf(&child, "%s/%s", path, list.names[i]) >= 0) {
                ok = nw_paths_remove_tree(child);
            }
            SDL_free(child);
            SDL_free(list.names[i]);
        }
        SDL_free(list.names);
        if (!ok) {
            return false;
        }
    }
    return SDL_RemovePath(path);
}
