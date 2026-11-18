/* One HTTPS GET on the calling thread: WinHTTP on Windows, elsewhere the system's libcurl, loaded at run time so the
   game starts without it. */
#ifndef NUWORLD_PLATFORM_HTTP_H
#define NUWORLD_PLATFORM_HTTP_H

#include <stdbool.h>

/* body is NUL-terminated, SDL_free it. False with SDL_GetError() when no answer came or it is over 1 MiB; any
   status is an answer. */
bool nw_http_get(const char *url, const char *agent, int timeout_ms, char **body, int *status);

#endif
