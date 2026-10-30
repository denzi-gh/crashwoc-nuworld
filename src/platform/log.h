/* SDL_Log to stderr and, once opened, to <root>NW_PATHS_LOG; a crash writes a last line. */
#ifndef NUWORLD_PLATFORM_LOG_H
#define NUWORLD_PLATFORM_LOG_H

#include <stdbool.h>

void nw_log_init(int argc, char **argv);
/* Keeps the last run as NW_PATHS_LOG_PREVIOUS; false with SDL_GetError(). */
bool nw_log_open(const char *root);
void nw_log_close(int code);

#endif
