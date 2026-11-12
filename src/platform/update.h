/* The newest NuWorld release: a thread asks GitHub once, the title menu offers it when it is newer than the build. */
#ifndef NUWORLD_PLATFORM_UPDATE_H
#define NUWORLD_PLATFORM_UPDATE_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

/* Main thread. The first on starts the check against version, the build's x.y.z; off hides what it found. */
void nw_update_enable(bool on, const char *version);
/* Any thread: a newer release is out and the check is on. */
bool nw_update_available(void);
/* Any thread: the release's page opens in the browser on the next nw_update_poll. */
void nw_update_open(void);
/* Main thread: opens a requested page and minimizes a fullscreen window for it. */
void nw_update_poll(SDL_Window *window);

/* The release's tag in GitHub's JSON, false unless it is a version as nw_update_compare reads it. */
bool nw_update_tag(const char *json, char *out, size_t cap);
/* *order <0, 0 or >0 as a is older than, the same as or newer than b: each 1 to 3 dotted numbers after an optional
   v. */
bool nw_update_compare(const char *a, const char *b, int *order);

#endif
