/* nucore/nuerror's fatal path (port/units.toml): prints as retail does, then stops the game thread (nw_game_fatal)
   instead of spinning; NuErrorProlog comes along because the file and line are nuerror.c statics. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nucore/nuerror.h"
#include "platform/log.h"
#include "port/game.h"

static const char *nw_errfile;
static int nw_errline;

void NuErrorFunction(const char *fmt, ...)
{
    char buf[0x1000];
    char msg[0x1100];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    snprintf(msg, sizeof(msg), "NuError - %s(%d) : %s", nw_errfile, nw_errline, buf);
    nw_log_error(NW_LOG_GAME, "%s", msg);
    nw_game_fatal(msg);
}

NuErrorFunc NuErrorProlog(const char *file, int line)
{
    const char *base = strrchr(file, '\\');
    nw_errline = line;
    nw_errfile = base ? base + 1 : file;
    return NuErrorFunction;
}

int nw_decomp_printf(const char *fmt, ...)
{
    static _Thread_local char line[1024];
    static _Thread_local size_t len;
    char text[2048];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    for (const char *c = text; *c; c++) {
        if (*c == '\n' || len == sizeof(line) - 1) {
            line[len] = '\0';
            if (len) {
                nw_log_debug(NW_LOG_DECOMP, "%s", line);
            }
            len = 0;
            if (*c == '\n') {
                continue;
            }
        }
        if (*c != '\r') {
            line[len++] = *c;
        }
    }
    return n;
}
