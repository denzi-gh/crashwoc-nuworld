/* SDL_Log to stderr and, once opened, to <root>NW_PATHS_LOG; a crash writes a last line. */
#ifndef NUWORLD_PLATFORM_LOG_H
#define NUWORLD_PLATFORM_LOG_H

#include <stdbool.h>

void nw_log_init(int argc, char **argv);
/* Keeps the last run as NW_PATHS_LOG_PREVIOUS; false with SDL_GetError(). */
bool nw_log_open(const char *root);
void nw_log_close(int code);

/* text with the home folder (home NULL: USERPROFILE or HOME at nw_log_init) as %USERPROFILE% or ~, as in every log
   line: logs get posted in bug reports. SDL_free it; NULL when out of memory. */
char *nw_log_redact(const char *text, const char *home);

#endif
