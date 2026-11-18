#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "platform/log.h"

#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "platform/buildid.h"
#include "platform/crash.h"
#include "platform/paths.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef _WIN32
#define NW_LOG_HOME_VAR "USERPROFILE"
#define NW_LOG_HOME "%USERPROFILE%"
#else
#define NW_LOG_HOME_VAR "HOME"
#define NW_LOG_HOME "~"
#endif

#define NW_LOG_RING (256 * 1024)
#define NW_LOG_CATEGORIES (NW_LOG_CATEGORY_END - NW_LOG_APP)

static const char *const nw_log_names[] = { "app",  "game",  "decomp", "gfx", "gl",   "audio",
                                            "load", "save", "input",  "net", "port", "stdio" };
_Static_assert(SDL_arraysize(nw_log_names) == NW_LOG_CATEGORIES, "a name per category");

static const struct {
    const char *name;
    SDL_LogPriority priority;
} nw_log_levels[] = { { "trace", SDL_LOG_PRIORITY_TRACE }, { "debug", SDL_LOG_PRIORITY_DEBUG },
                      { "info", SDL_LOG_PRIORITY_INFO },   { "warn", SDL_LOG_PRIORITY_WARN },
                      { "error", SDL_LOG_PRIORITY_ERROR } };

static struct {
    SDL_SpinLock lock;
    SDL_IOStream *file;
    int argc;
    char **argv;
    SDL_ThreadID main;
    Uint64 start;
    char home[512];
    char spec[64];
    SDL_LogPriority level[NW_LOG_CATEGORIES];
    SDL_LogPriority sdl;
    bool stdio;
#ifdef _WIN32
    SDL_AtomicInt stdio_drained;
#endif
    SDL_SpinLock ring_lock;
    uint64_t ring_head;
    char ring[NW_LOG_RING];
} nw_log;

static _Thread_local char nw_log_thread[24];

static bool nw_log_name_char(char c)
{
    return SDL_isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-';
}

/* Windows paths ignore case and take either separator. */
static bool nw_log_home_at(const char *text, const char *home, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char a = text[i], b = home[i];
#ifdef _WIN32
        a = a == '/' ? '\\' : (char)SDL_tolower((unsigned char)a);
        b = b == '/' ? '\\' : (char)SDL_tolower((unsigned char)b);
#endif
        if (a != b) {
            return false;
        }
    }
    return !nw_log_name_char(text[n]);
}

char *nw_log_redact(const char *text, const char *home)
{
    if (!home) {
        home = nw_log.home;
    }
    size_t n = SDL_strlen(home), len = SDL_strlen(text), r = SDL_strlen(NW_LOG_HOME);
    while (n && (home[n - 1] == '/' || home[n - 1] == '\\')) {
        n--;
    }
    if (n < 2) {
        return SDL_strdup(text);
    }
    char *out = SDL_malloc(len + (len / n + 1) * r + 1), *o = out;
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < len;) {
        if ((i == 0 || !nw_log_name_char(text[i - 1])) && nw_log_home_at(text + i, home, n)) {
            SDL_memcpy(o, NW_LOG_HOME, r);
            o += r;
            i += n;
        } else {
            *o++ = text[i++];
        }
    }
    *o = '\0';
    return out;
}

const char *nw_log_thread_name(void)
{
    if (nw_log_thread[0]) {
        return nw_log_thread;
    }
    char name[64] = "";
    if (SDL_GetCurrentThreadID() == nw_log.main) {
        SDL_strlcpy(name, "main", sizeof(name));
    } else {
#ifdef _WIN32
        typedef HRESULT(WINAPI * NwGetDescription)(HANDLE, PWSTR *);
        static NwGetDescription get;
        if (!get) {
            get = (NwGetDescription)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
        }
        PWSTR wide = NULL;
        if (get && SUCCEEDED(get(GetCurrentThread(), &wide)) && wide) {
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), NULL, NULL);
            LocalFree(wide);
        }
#else
        pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
    }
    const char *shown = name[0] ? name : "thread";
    SDL_strlcpy(nw_log_thread, SDL_strncmp(shown, "nw_", 3) == 0 ? shown + 3 : shown, sizeof(nw_log_thread));
    return nw_log_thread;
}

const char *nw_log_category_name(int category)
{
    if (category >= NW_LOG_APP && category < NW_LOG_CATEGORY_END) {
        return nw_log_names[category - NW_LOG_APP];
    }
    return category == SDL_LOG_CATEGORY_APPLICATION ? "app" : "sdl";
}

static SDL_LogPriority nw_log_written(int category)
{
    if (category >= NW_LOG_APP && category < NW_LOG_CATEGORY_END) {
        return nw_log.level[category - NW_LOG_APP];
    }
    return category == SDL_LOG_CATEGORY_APPLICATION ? nw_log.level[0] : nw_log.sdl;
}

static bool nw_log_level(const char *text, size_t n, SDL_LogPriority *out)
{
    for (size_t i = 0; i < SDL_arraysize(nw_log_levels); i++) {
        if (SDL_strlen(nw_log_levels[i].name) == n && SDL_strncmp(text, nw_log_levels[i].name, n) == 0) {
            *out = nw_log_levels[i].priority;
            return true;
        }
    }
    return false;
}

bool nw_log_configure(const char *spec)
{
    SDL_LogPriority all = SDL_LOG_PRIORITY_INFO, sdl = SDL_LOG_PRIORITY_WARN, level[NW_LOG_CATEGORIES] = { 0 };
    bool set[NW_LOG_CATEGORIES] = { false };
    if (SDL_strlen(spec) >= sizeof(nw_log.spec)) {
        return false;
    }
    for (const char *p = spec; *spec; p++) {
        const char *end = SDL_strchr(p, ',');
        end = end ? end : p + SDL_strlen(p);
        const char *eq = p;
        while (eq < end && *eq != '=') {
            eq++;
        }
        SDL_LogPriority v;
        if (eq == end) {
            if (!nw_log_level(p, (size_t)(end - p), &all)) {
                return false;
            }
        } else if (!nw_log_level(eq + 1, (size_t)(end - eq - 1), &v)) {
            return false;
        } else if (eq - p == 3 && SDL_strncmp(p, "sdl", 3) == 0) {
            sdl = v;
        } else {
            size_t i = 0;
            while (i < NW_LOG_CATEGORIES &&
                   !(SDL_strlen(nw_log_names[i]) == (size_t)(eq - p) && SDL_strncmp(p, nw_log_names[i], eq - p) == 0)) {
                i++;
            }
            if (i == NW_LOG_CATEGORIES) {
                return false;
            }
            level[i] = v;
            set[i] = true;
        }
        if (!*end) {
            break;
        }
        p = end;
    }
    for (int i = 0; i < NW_LOG_CATEGORIES; i++) {
        nw_log.level[i] = set[i] ? level[i] : all;
    }
    nw_log.sdl = sdl;
    SDL_strlcpy(nw_log.spec, spec, sizeof(nw_log.spec));
    nw_log_restore();
    return true;
}

void nw_log_restore(void)
{
    for (int c = 0; c < SDL_LOG_CATEGORY_CUSTOM; c++) {
        SDL_SetLogPriority(c, SDL_min(c == SDL_LOG_CATEGORY_APPLICATION ? nw_log.level[0] : nw_log.sdl,
                                      SDL_LOG_PRIORITY_DEBUG));
    }
    for (int i = 0; i < NW_LOG_CATEGORIES; i++) {
        SDL_SetLogPriority(NW_LOG_APP + i, SDL_min(nw_log.level[i], SDL_LOG_PRIORITY_DEBUG));
    }
}

uint64_t nw_log_ms(void)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    return freq ? (SDL_GetPerformanceCounter() - nw_log.start) * 1000 / freq : 0;
}

static void nw_log_ring_add(const char *line, size_t n)
{
    SDL_LockSpinlock(&nw_log.ring_lock);
    size_t at = (size_t)(nw_log.ring_head % NW_LOG_RING), first = SDL_min(n, NW_LOG_RING - at);
    SDL_memcpy(nw_log.ring + at, line, first);
    SDL_memcpy(nw_log.ring, line + first, n - first);
    nw_log.ring_head += n;
    SDL_UnlockSpinlock(&nw_log.ring_lock);
}

size_t nw_log_recent(char *out, size_t size)
{
    if (!size) {
        return 0;
    }
    bool locked = false;
    for (int i = 0; i < 100000 && !locked; i++) {
        locked = SDL_TryLockSpinlock(&nw_log.ring_lock);
    }
    uint64_t head = nw_log.ring_head;
    size_t have = head < NW_LOG_RING ? (size_t)head : NW_LOG_RING, take = SDL_min(have, size - 1);
    uint64_t from = head - take;
    for (size_t i = 0; i < take; i++) {
        out[i] = nw_log.ring[(from + i) % NW_LOG_RING];
    }
    bool whole = from == 0 || (take < have && nw_log.ring[(from - 1) % NW_LOG_RING] == '\n');
    if (locked) {
        SDL_UnlockSpinlock(&nw_log.ring_lock);
    }
    size_t skip = 0;
    if (!whole) {
        while (skip < take && out[skip] != '\n') {
            skip++;
        }
        skip += skip < take;
    }
    SDL_memmove(out, out + skip, take - skip);
    out[take - skip] = '\0';
    return take - skip;
}

static void SDLCALL nw_log_output(void *user, int category, SDL_LogPriority priority, const char *message)
{
    const char *prefix = priority >= SDL_LOG_PRIORITY_CRITICAL ? "CRITICAL: "
                         : priority >= SDL_LOG_PRIORITY_ERROR  ? "ERROR: "
                         : priority >= SDL_LOG_PRIORITY_WARN   ? "WARNING: "
                                                               : "";
    (void)user;
    char *redacted = nw_log_redact(message, NULL);
    if (redacted) {
        message = redacted;
    }
    char line[4096];
    uint64_t ms = nw_log_ms();
    int n = SDL_snprintf(line, sizeof(line), "[%02llu:%02llu.%03llu] %-7s %-6s %s%s\n",
                         (unsigned long long)(ms / 60000), (unsigned long long)(ms / 1000 % 60),
                         (unsigned long long)(ms % 1000), nw_log_thread_name(), nw_log_category_name(category), prefix,
                         message);
    if (n < 0) {
        n = 0;
    } else if ((size_t)n >= sizeof(line)) {
        n = (int)sizeof(line) - 1;
        line[n - 1] = '\n';
    }
    nw_log_ring_add(line, (size_t)n);
    if (priority >= nw_log_written(category)) {
        if (!nw_log.stdio) {
            fputs(line, stderr);
            fflush(stderr);
        }
        SDL_LockSpinlock(&nw_log.lock);
        if (nw_log.file) {
            SDL_WriteIO(nw_log.file, line, (size_t)n);
            SDL_FlushIO(nw_log.file);
        }
        SDL_UnlockSpinlock(&nw_log.lock);
    }
    SDL_free(redacted);
}

#ifdef _WIN32
/* A GUI exe has no console: redirected output stays as it is, otherwise it goes to the console that started
   the exe or, with --console, to a new one. */
static void nw_log_console(bool open)
{
    bool out = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN;
    bool err = GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_UNKNOWN;
    if (!(out || err) || !(AttachConsole(ATTACH_PARENT_PROCESS) || (open && AllocConsole()))) {
        return;
    }
    if (out && freopen("CONOUT$", "w", stdout)) {
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(stdout)));
    }
    if (err && freopen("CONOUT$", "w", stderr)) {
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));
    }
}

#define NW_LOG_DRAIN '\x01'

static int SDLCALL nw_log_stdio_run(void *data)
{
    HANDLE pipe = data;
    char buf[4096], line[1024];
    size_t len = 0;
    DWORD got;
    while (ReadFile(pipe, buf, sizeof(buf), &got, NULL) && got) {
        for (DWORD i = 0; i < got; i++) {
            if (buf[i] == '\n' || buf[i] == NW_LOG_DRAIN || len == sizeof(line) - 1) {
                line[len] = '\0';
                if (len) {
                    nw_log_output(NULL, NW_LOG_STDIO, SDL_LOG_PRIORITY_INFO, line);
                }
                len = 0;
                if (buf[i] == NW_LOG_DRAIN) {
                    SDL_SetAtomicInt(&nw_log.stdio_drained, 1);
                }
                if (buf[i] == '\n' || buf[i] == NW_LOG_DRAIN) {
                    continue;
                }
            }
            if (buf[i] != '\r') {
                line[len++] = buf[i];
            }
        }
    }
    return 0;
}

static void nw_log_capture_stdio(void)
{
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN) {
        return;
    }
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, NULL, 1 << 16)) {
        return;
    }
    int fd = _open_osfhandle((intptr_t)wr, _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(rd);
        CloseHandle(wr);
        return;
    }
    bool ok = freopen("NUL", "w", stdout) && freopen("NUL", "w", stderr) && _dup2(fd, _fileno(stdout)) == 0 &&
              _dup2(fd, _fileno(stderr)) == 0;
    _close(fd);
    SDL_Thread *reader = ok ? SDL_CreateThread(nw_log_stdio_run, "nw_stdio", rd) : NULL;
    if (!reader) {
        CloseHandle(rd);
        return;
    }
    SDL_DetachThread(reader);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(stdout)));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));
    nw_log.stdio = true;
}

static void nw_log_drain_stdio(void)
{
    if (!nw_log.stdio) {
        return;
    }
    fflush(stdout);
    fflush(stderr);
    SDL_SetAtomicInt(&nw_log.stdio_drained, 0);
    DWORD written;
    char mark = NW_LOG_DRAIN;
    if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), &mark, 1, &written, NULL)) {
        return;
    }
    for (int i = 0; i < 100 && !SDL_GetAtomicInt(&nw_log.stdio_drained); i++) {
        SDL_Delay(5);
    }
}
#endif

void nw_log_init(int argc, char **argv)
{
    nw_log.argc = argc;
    nw_log.argv = argv;
    nw_log.main = SDL_GetCurrentThreadID();
    nw_log.start = SDL_GetPerformanceCounter();
    const char *home = SDL_getenv(NW_LOG_HOME_VAR);
    SDL_strlcpy(nw_log.home, home ? home : "", sizeof(nw_log.home));
#ifdef _WIN32
    bool console = false;
    for (int i = 1; i < argc; i++) {
        console |= SDL_strcmp(argv[i], "--console") == 0;
    }
    nw_log_console(console);
#endif
    nw_log_configure("info");
    SDL_SetLogOutputFunction(nw_log_output, NULL);
    nw_crash_install();
}

static void nw_log_header(const char *folder)
{
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
    nw_log_info(NW_LOG_APP, NW_APP_NAME " %s (decomp %s)", nw_build_id, nw_build_decomp);
    nw_log_info(NW_LOG_APP, "started %s on %s, %d CPU threads, %d MB RAM, SDL %d.%d.%d", started, SDL_GetPlatform(),
                SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM(), SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl),
                SDL_VERSIONNUM_MICRO(sdl));
    nw_log_info(NW_LOG_APP, "command line: %s", args);
    if (folder) {
        nw_log_info(NW_LOG_APP, "data folder: %s", folder);
    }
    nw_log_info(NW_LOG_APP, "log: %s", nw_log.spec);
}

static bool nw_log_start(SDL_IOStream *file)
{
    if (!file) {
        return false;
    }
    SDL_PropertiesID props = SDL_GetIOProperties(file);
#ifdef _WIN32
    intptr_t raw = (intptr_t)SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER,
                                                    (void *)NW_CRASH_NO_FILE);
#else
    intptr_t raw = (intptr_t)SDL_GetNumberProperty(props, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1);
#endif
    SDL_LockSpinlock(&nw_log.lock);
    nw_log.file = file;
    SDL_UnlockSpinlock(&nw_log.lock);
    nw_crash_sink(raw, !nw_log.stdio);
    return true;
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
#ifdef _WIN32
    nw_log_capture_stdio();
#endif
    nw_log_start(file);
    nw_log_header(root);
    char *crashes = NULL;
    if (SDL_asprintf(&crashes, "%s" NW_PATHS_CRASHES, root) >= 0) {
        nw_crash_dumps(crashes);
    }
    SDL_free(crashes);
    return true;
}

bool nw_log_open_file(const char *path)
{
    if (!nw_log_start(SDL_IOFromFile(path, "w"))) {
        return false;
    }
    nw_log_header(NULL);
    return true;
}

void nw_log_close(int code)
{
    if (!nw_log.file) {
        return;
    }
#ifdef _WIN32
    nw_log_drain_stdio();
#endif
    nw_log_info(NW_LOG_APP, "exit code %d", code);
    SDL_LockSpinlock(&nw_log.lock);
    SDL_IOStream *file = nw_log.file;
    nw_log.file = NULL;
    SDL_UnlockSpinlock(&nw_log.lock);
    nw_crash_sink(NW_CRASH_NO_FILE, !nw_log.stdio);
    nw_crash_dumps(NULL);
    SDL_CloseIO(file);
}
