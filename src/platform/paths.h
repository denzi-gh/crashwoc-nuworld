/* Data folder layout: <root>disc/, <root>import/, <root>saves/. Returned paths end with a separator and are
   freed with SDL_free. Errors via SDL_GetError(). */
#ifndef NUWORLD_PLATFORM_PATHS_H
#define NUWORLD_PLATFORM_PATHS_H

#include <stdbool.h>

#define NW_PATHS_APP "NuWorld"
#define NW_PATHS_PORTABLE "portable.txt"

/* <base_dir>user/ when <base_dir>portable.txt exists (portable mode), else pref_dir
   (NULL: SDL_GetPrefPath(NULL, "NuWorld")). */
char *nw_paths_data_root(const char *base_dir, const char *pref_dir);

/* <dir><name>/ (name may be NULL), created with its parents when create is set. */
char *nw_paths_dir(const char *dir, const char *name, bool create);

/* Removes a file or a whole directory tree; a missing path is not an error. */
bool nw_paths_remove_tree(const char *path);

#endif
