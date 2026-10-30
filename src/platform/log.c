#ifndef _WIN32
#define _GNU_SOURCE /* REG_RIP, dladdr */
#endif
#include "platform/log.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "platform/buildid.h"
#include "platform/paths.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <ucontext.h>
#include <unistd.h>
#endif

static struct {
    SDL_SpinLock lock;
    SDL_IOStream *file;
    int argc;
    char **argv;
    SDL_ThreadID main;
#ifdef _WIN32
    HANDLE raw; /* the file for the crash handler */
#else
    int raw;
#endif
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

/* From a crash handler */
static void nw_log_crash(const char *what, void *at, const char *module, uintptr_t base)
{
    char thread[32] = "the main thread", body[512], line[544];
    SDL_ThreadID id = SDL_GetCurrentThreadID();
    if (id != nw_log.main) {
        SDL_snprintf(thread, sizeof(thread), "thread %llu", (unsigned long long)id);
    }
    const char *name = module;
    for (const char *c = module; *c; c++) {
        if (*c == '/' || *c == '\\') {
            name = c + 1;
        }
    }
    if (at) {
        SDL_snprintf(body, sizeof(body), "CRASH: %s at %s+0x%llx (%p) on %s\n", what, name,
                     (unsigned long long)((uintptr_t)at - base), at, thread);
    } else {
        SDL_snprintf(body, sizeof(body), "CRASH: %s on %s\n", what, thread);
    }
    Uint64 ms = SDL_GetTicks();
    int n = SDL_snprintf(line, sizeof(line), "[%02llu:%02llu.%03llu] %s", (unsigned long long)(ms / 60000),
                         (unsigned long long)(ms / 1000 % 60), (unsigned long long)(ms % 1000), body);
#ifdef _WIN32
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), body, (DWORD)SDL_strlen(body), &written, NULL);
    if (nw_log.raw) {
        WriteFile(nw_log.raw, line, (DWORD)n, &written, NULL);
    }
#else
    ssize_t r = write(STDERR_FILENO, body, SDL_strlen(body));
    if (nw_log.raw >= 0) {
        r = write(nw_log.raw, line, (size_t)n);
    }
    (void)r;
#endif
}

#ifdef _WIN32
static LPTOP_LEVEL_EXCEPTION_FILTER nw_log_next;

static LONG WINAPI nw_log_unhandled(EXCEPTION_POINTERS *e)
{
    EXCEPTION_RECORD *r = e->ExceptionRecord;
    char what[96];
    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        SDL_snprintf(what, sizeof(what), "access violation %s 0x%llx",
                     r->ExceptionInformation[0] == 1 ? "writing" : r->ExceptionInformation[0] == 8 ? "executing"
                                                                                                  : "reading",
                     (unsigned long long)r->ExceptionInformation[1]);
        break;
    case EXCEPTION_STACK_OVERFLOW:
        SDL_strlcpy(what, "stack overflow", sizeof(what));
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        SDL_strlcpy(what, "illegal instruction", sizeof(what));
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        SDL_strlcpy(what, "integer divide by zero", sizeof(what));
        break;
    default:
        SDL_snprintf(what, sizeof(what), "exception 0x%08lx", (unsigned long)r->ExceptionCode);
    }
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       r->ExceptionAddress, &module);
    GetModuleFileNameA(module, name, sizeof(name));
    nw_log_crash(what, r->ExceptionAddress, name, (uintptr_t)module);
    return nw_log_next ? nw_log_next(e) : EXCEPTION_CONTINUE_SEARCH;
}

static void (*nw_log_next_abort)(int);

static void nw_log_abort(int sig)
{
    nw_log_crash("abort", NULL, "", 0);
    signal(sig, nw_log_next_abort);
}

static void nw_log_crash_install(void)
{
    nw_log_next = SetUnhandledExceptionFilter(nw_log_unhandled);
    nw_log_next_abort = signal(SIGABRT, nw_log_abort);
}
#else
static const int nw_log_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
static struct sigaction nw_log_next[SDL_arraysize(nw_log_signals)];

static void nw_log_signal(int sig, siginfo_t *info, void *ctx)
{
    void *at = NULL;
#if defined(__linux__) && defined(__x86_64__)
    at = (void *)((ucontext_t *)ctx)->uc_mcontext.gregs[REG_RIP];
#else
    (void)ctx;
#endif
    char what[96];
    switch (sig) {
    case SIGSEGV:
        SDL_snprintf(what, sizeof(what), "segmentation fault accessing %p", info->si_addr);
        break;
    case SIGBUS:
        SDL_snprintf(what, sizeof(what), "bus error accessing %p", info->si_addr);
        break;
    case SIGILL:
        SDL_strlcpy(what, "illegal instruction", sizeof(what));
        break;
    case SIGFPE:
        SDL_strlcpy(what, "arithmetic exception", sizeof(what));
        break;
    default:
        SDL_strlcpy(what, "abort", sizeof(what));
        at = NULL;
    }
    Dl_info dl = { 0 };
    if (at) {
        dladdr(at, &dl);
    }
    nw_log_crash(what, at, dl.dli_fname ? dl.dli_fname : "?", (uintptr_t)dl.dli_fbase);
    /* hand over to the previous handler */
    for (size_t i = 0; i < SDL_arraysize(nw_log_signals); i++) {
        if (nw_log_signals[i] == sig) {
            sigaction(sig, &nw_log_next[i], NULL);
        }
    }
    if (info->si_code <= 0) {
        raise(sig);
    }
}

static void nw_log_crash_install(void)
{
    struct sigaction sa = { 0 };
    sa.sa_sigaction = nw_log_signal;
    sa.sa_flags = SA_SIGINFO;
    for (size_t i = 0; i < SDL_arraysize(nw_log_signals); i++) {
        sigaction(nw_log_signals[i], &sa, &nw_log_next[i]);
    }
}
#endif

void nw_log_init(int argc, char **argv)
{
    nw_log.argc = argc;
    nw_log.argv = argv;
    nw_log.main = SDL_GetCurrentThreadID();
#ifndef _WIN32
    nw_log.raw = -1;
#endif
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
    SDL_PropertiesID props = SDL_GetIOProperties(file);
    SDL_LockSpinlock(&nw_log.lock);
    nw_log.file = file;
#ifdef _WIN32
    nw_log.raw = SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER, NULL);
#else
    nw_log.raw = (int)SDL_GetNumberProperty(props, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1);
#endif
    SDL_UnlockSpinlock(&nw_log.lock);
    nw_log_crash_install();

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
#ifdef _WIN32
    nw_log.raw = NULL;
#else
    nw_log.raw = -1;
#endif
    SDL_UnlockSpinlock(&nw_log.lock);
    SDL_CloseIO(file);
}
