#include "platform/log.h"

#include <stdio.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "platform/buildid.h"
#include "platform/paths.h"

static struct {
    SDL_SpinLock lock; /* the file against nw_log_close; SDL already serializes the messages */
    SDL_IOStream *file;
    int argc;
    char **argv;
} nw_log;

static void SDLCALL nw_log_output(void *user, int category, SDL_LogPriority priority, const char *message)
{
    const char *prefix = priority >= SDL_LOG_PRIORITY_CRITICAL ? "CRITICAL: "
                         : priority >= SDL_LOG_PRIORITY_ERROR  ? "ERROR: "
                         : priority >= SDL_LOG_PRIORITY_WARN   ? "WARNING: "
                                                               : "";
    (void)user;
    (void)category;
    fprintf(stderr, "%s%s\n", prefix, message);
    fflush(stderr);
    SDL_LockSpinlock(&nw_log.lock);
    if (nw_log.file) {
        Uint64 ms = SDL_GetTicks();
        SDL_IOprintf(nw_log.file, "[%02llu:%02llu.%03llu] %s%s\n", (unsigned long long)(ms / 60000),
                     (unsigned long long)(ms / 1000 % 60), (unsigned long long)(ms % 1000), prefix, message);
        SDL_FlushIO(nw_log.file);
    }
    SDL_UnlockSpinlock(&nw_log.lock);
}

void nw_log_init(int argc, char **argv)
{
    nw_log.argc = argc;
    nw_log.argv = argv;
    SDL_SetLogOutputFunction(nw_log_output, NULL);
}

bool nw_log_open(const char *root)
{
    char *path = NULL, *previous = NULL;
    bool ok = SDL_asprintf(&path, "%s" NW_PATHS_LOG, root) >= 0 &&
              SDL_asprintf(&previous, "%s" NW_PATHS_LOG_PREVIOUS, root) >= 0;
    if (ok && SDL_GetPathInfo(path, NULL)) {
        SDL_RenamePath(path, previous);
    }
    SDL_IOStream *file = ok ? SDL_IOFromFile(path, "w") : NULL;
    SDL_free(previous);
    SDL_free(path);
    if (!file) {
        return false;
    }
    SDL_LockSpinlock(&nw_log.lock);
    nw_log.file = file;
    SDL_UnlockSpinlock(&nw_log.lock);

    char started[32] = "?";
    SDL_Time now;
    SDL_DateTime dt;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true)) {
        SDL_snprintf(started, sizeof(started), "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour,
                     dt.minute, dt.second);
    }
    char args[512] = "";
    for (int i = 0; i < nw_log.argc; i++) {
        SDL_strlcat(args, i ? " " : "", sizeof(args));
        SDL_strlcat(args, nw_log.argv[i], sizeof(args));
    }
    int sdl = SDL_GetVersion();
    SDL_Log(NW_APP_NAME " %s (decomp %s)", nw_build_id, nw_build_decomp);
    SDL_Log("started %s on %s, SDL %d.%d.%d", started, SDL_GetPlatform(), SDL_VERSIONNUM_MAJOR(sdl),
            SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl));
    SDL_Log("command line: %s", args);
    SDL_Log("data folder: %s", root);
    return true;
}

void nw_log_close(int code)
{
    if (!nw_log.file) {
        return;
    }
    SDL_Log("exit code %d", code);
    SDL_LockSpinlock(&nw_log.lock);
    SDL_IOStream *file = nw_log.file;
    nw_log.file = NULL;
    SDL_UnlockSpinlock(&nw_log.lock);
    SDL_CloseIO(file);
}
