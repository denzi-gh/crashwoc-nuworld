/* nucore/nuerror's fatal path (port/units.toml): retail prints the message and spins forever; the port prints it
   the same way and stops the game thread for good (nw_game_fatal). The file and line are nuerror.c statics, so
   NuErrorProlog comes along; the warning and debug prologs keep retail's. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "nucore/nuerror.h"
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
    printf("%s\n", msg);
    fflush(stdout);
    nw_game_fatal(msg);
}

NuErrorFunc NuErrorProlog(const char *file, int line)
{
    const char *base = strrchr(file, '\\');
    nw_errline = line;
    nw_errfile = base ? base + 1 : file;
    return NuErrorFunction;
}
