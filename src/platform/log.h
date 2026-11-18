#ifndef NUWORLD_PLATFORM_LOG_H
#define NUWORLD_PLATFORM_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL_log.h>

typedef enum NwLogCategory {
    NW_LOG_APP = SDL_LOG_CATEGORY_CUSTOM,
    NW_LOG_GAME,
    NW_LOG_DECOMP,
    NW_LOG_GFX,
    NW_LOG_GL,
    NW_LOG_AUDIO,
    NW_LOG_LOAD,
    NW_LOG_SAVE,
    NW_LOG_INPUT,
    NW_LOG_NET,
    NW_LOG_PORT,
    NW_LOG_STDIO,
    NW_LOG_CATEGORY_END
} NwLogCategory;

#define nw_log_error(cat, ...) SDL_LogError((cat), __VA_ARGS__)
#define nw_log_warn(cat, ...) SDL_LogWarn((cat), __VA_ARGS__)
#define nw_log_info(cat, ...) SDL_LogInfo((cat), __VA_ARGS__)
#define nw_log_debug(cat, ...) SDL_LogDebug((cat), __VA_ARGS__)
#define nw_log_trace(cat, ...) SDL_LogTrace((cat), __VA_ARGS__)

void nw_log_init(int argc, char **argv);
bool nw_log_configure(const char *spec);
void nw_log_restore(void);
uint64_t nw_log_ms(void);
bool nw_log_open(const char *root);
bool nw_log_open_file(const char *path);
void nw_log_close(int code);

const char *nw_log_thread_name(void);
const char *nw_log_category_name(int category);

size_t nw_log_recent(char *out, size_t size);

/* text with the home folder (home NULL: USERPROFILE or HOME at nw_log_init) as %USERPROFILE% or ~, as in every log
   line: logs get posted in bug reports. SDL_free it; NULL when out of memory. */
char *nw_log_redact(const char *text, const char *home);

#endif
