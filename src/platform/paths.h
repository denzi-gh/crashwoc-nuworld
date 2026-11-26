/* Returned paths end with a separator and are freed with SDL_free. */
#ifndef NUWORLD_PLATFORM_PATHS_H
#define NUWORLD_PLATFORM_PATHS_H

#include <stdbool.h>

#define NW_PATHS_APP "crashwoc-nuworld"
#define NW_PATHS_PORTABLE "portable.txt"
#define NW_PATHS_IMPORT "import"
#define NW_PATHS_SAVES "saves"
#define NW_PATHS_SETTINGS "settings.toml"
#define NW_PATHS_PROFILES "profiles"
#define NW_PATHS_LOG "nuworld.log"
#define NW_PATHS_LOG_PREVIOUS "nuworld.previous.log"
#define NW_PATHS_CRASHES "crashes"
#define NW_PATHS_SCREENSHOTS "screenshots"
#define NW_PATHS_CAPTURES "captures/port" /* --capture's frames; tools/pcsx2_capture.py writes captures/pcsx2 */
#define NW_PATHS_DUMPS "dumps/port"       /* --dump's states; tools/pcsx2_dump.py writes dumps/pcsx2 */

/* <base_dir>user/ when <base_dir>portable.txt exists, else pref_dir (NULL: SDL_GetPrefPath(NULL, NW_PATHS_APP)). */
char *nw_paths_data_root(const char *base_dir, const char *pref_dir);

char *nw_paths_dir(const char *dir, const char *name, bool create);

bool nw_paths_create_layout(const char *root);

/* A missing path is not an error. */
bool nw_paths_remove_tree(const char *path);

/* Puts from in path's place (a rename over it). Windows refuses while another program has path open, as a virus
   scanner or the search indexer may for a moment after a write: tried again for up to half a second. */
bool nw_paths_replace(const char *from, const char *path);

#endif
