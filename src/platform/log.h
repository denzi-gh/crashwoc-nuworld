/* The log: every SDL_Log message goes to stderr as printed and, while the log file is open, to <data root>
   NW_PATHS_LOG as well, with the time since the start and flushed per line, so a crash keeps its last lines. The file
   starts with the build ID; the run before is kept as NW_PATHS_LOG_PREVIOUS. */
#ifndef NUWORLD_PLATFORM_LOG_H
#define NUWORLD_PLATFORM_LOG_H

#include <stdbool.h>

/* First thing in main: takes over SDL's log output and keeps the command line for the header. */
void nw_log_init(int argc, char **argv);
/* Opens root's log file and writes the header; false (SDL_GetError) leaves the log on stderr only. */
bool nw_log_open(const char *root);
/* Logs the exit code and closes the file; nothing without an open file. */
void nw_log_close(int code);

#endif
