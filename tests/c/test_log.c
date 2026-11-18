#include <stdio.h>

#include <SDL3/SDL.h>

#include "platform/crash.h"
#include "platform/log.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char recent[512 * 1024];

static bool redacts(const char *text, const char *home, const char *want)
{
    char *got = nw_log_redact(text, home);
    bool ok = got && SDL_strcmp(got, want) == 0;
    if (!ok) {
        fprintf(stderr, "  \"%s\" -> \"%s\", want \"%s\"\n", text, got ? got : "(null)", want);
    }
    SDL_free(got);
    return ok;
}

static void check_redact(void)
{
#ifdef _WIN32
    const char *home = "C:\\Users\\Ann";
    CHECK(redacts("data folder: C:\\Users\\Ann\\AppData\\Roaming\\crashwoc-nuworld\\", home,
                  "data folder: %USERPROFILE%\\AppData\\Roaming\\crashwoc-nuworld\\"));
    CHECK(redacts("c:/users/ann/x.iso and C:\\Users\\Ann", "C:\\Users\\Ann\\",
                  "%USERPROFILE%/x.iso and %USERPROFILE%"));
    CHECK(redacts("C:\\Users\\Anna\\x", home, "C:\\Users\\Anna\\x"));
    CHECK(redacts("D:\\C:\\Users\\Ann", home, "D:\\%USERPROFILE%"));
#else
    const char *home = "/home/ann";
    CHECK(redacts("data folder: /home/ann/.local/share/crashwoc-nuworld/", home,
                  "data folder: ~/.local/share/crashwoc-nuworld/"));
    CHECK(redacts("'/home/ann/a b.iso' /home/ann", "/home/ann/", "'~/a b.iso' ~"));
    CHECK(redacts("/home/anna/x /mnt/home/ann/x", home, "/home/anna/x /mnt/home/ann/x"));
    CHECK(redacts("/HOME/ANN/x", home, "/HOME/ANN/x"));
#endif
    CHECK(redacts("no path here", home, "no path here"));
    CHECK(redacts("", home, ""));
    CHECK(redacts("/ and \\ stay", "/", "/ and \\ stay"));
    CHECK(redacts("x", "", "x"));
}

static void check_specs(void)
{
    CHECK(nw_log_configure("info"));
    CHECK(nw_log_configure("info,gfx=debug,decomp=trace"));
    CHECK(nw_log_configure("error,sdl=info,stdio=warn"));
    CHECK(nw_log_configure(""));
    CHECK(!nw_log_configure("loud"));
    CHECK(!nw_log_configure("gfx=loud"));
    CHECK(!nw_log_configure("nope=debug"));
    CHECK(!nw_log_configure("info,"));
    CHECK(!nw_log_configure("info,gfx"));
    CHECK(!nw_log_configure("info,gfx=debug,decomp=debug,load=debug,save=debug,audio=debug,net=debug"));
    CHECK(SDL_strcmp(nw_log_category_name(NW_LOG_DECOMP), "decomp") == 0);
    CHECK(SDL_strcmp(nw_log_category_name(SDL_LOG_CATEGORY_VIDEO), "sdl") == 0);
}

static void check_levels(void)
{
    CHECK(nw_log_configure("warn,gfx=trace"));
    nw_log_info(NW_LOG_APP, "levels: info under warn");
    nw_log_trace(NW_LOG_APP, "levels: app trace");
    nw_log_trace(NW_LOG_GFX, "levels: gfx trace");
    nw_log_warn(NW_LOG_SAVE, "levels: a warning");
    nw_log_recent(recent, sizeof(recent));
    CHECK(SDL_strstr(recent, "levels: info under warn"));
    CHECK(!SDL_strstr(recent, "levels: app trace"));
    CHECK(SDL_strstr(recent, "levels: gfx trace"));
    const char *line = SDL_strstr(recent, "WARNING: levels: a warning");
    CHECK(line);
    while (line && line > recent && line[-1] != '\n') {
        line--;
    }
    CHECK(line && line[0] == '[' && line[3] == ':' && line[6] == '.' && line[10] == ']' &&
          SDL_strncmp(line + 12, "main    save   WARNING: ", 24) == 0);
    CHECK(nw_log_configure("info"));
}

static void check_ring(void)
{
    nw_log_info(NW_LOG_APP, "ring: the first line");
    for (int i = 0; i < 3000; i++) {
        nw_log_debug(NW_LOG_APP, "ring: line %04d ..........................................................", i);
    }
    size_t n = nw_log_recent(recent, sizeof(recent));
    CHECK(n > 200 * 1024 && n < 256 * 1024 && recent[0] == '[' && recent[n - 1] == '\n' && recent[n] == '\0');
    CHECK(!SDL_strstr(recent, "ring: the first line"));
    CHECK(SDL_strstr(recent, "ring: line 2999"));
    char small[300];
    n = nw_log_recent(small, sizeof(small));
    CHECK(n < sizeof(small) && small[0] == '[' && SDL_strstr(small, "ring: line 2999") && small[n - 1] == '\n');
}

static int SDLCALL thread_logs(void *data)
{
    (void)data;
    nw_log_info(NW_LOG_PORT, "threads: from a named thread");
    return 0;
}

static void check_threads(void)
{
    SDL_Thread *t = SDL_CreateThread(thread_logs, "nw_probe", NULL);
    CHECK(t);
    SDL_WaitThread(t, NULL);
    nw_log_recent(recent, sizeof(recent));
    const char *line = SDL_strstr(recent, "threads: from a named thread");
    CHECK(line);
    while (line && line > recent && line[-1] != '\n') {
        line--;
    }
    CHECK(line && SDL_strncmp(line + 12, "probe   port   ", 15) == 0);
    CHECK(SDL_strcmp(nw_log_thread_name(), "main") == 0);
}

static __attribute__((noinline)) int nw_test_probe(int x)
{
    return x * 3 + 1;
}

static void check_symbols(void)
{
    int (*volatile probe)(int) = nw_test_probe;
    uintptr_t off = 99;
    const char *name = nw_crash_symbol((const void *)(uintptr_t)probe, &off);
    CHECK(name && SDL_strcmp(name, "nw_test_probe") == 0 && off == 0);
    name = nw_crash_symbol((const char *)(uintptr_t)probe + 2, &off);
    CHECK(name && SDL_strcmp(name, "nw_test_probe") == 0 && off == 2);
    CHECK(probe(2) == 7);
}

int main(int argc, char **argv)
{
    nw_log_init(argc, argv);
    check_redact();
    check_specs();
    check_levels();
    check_ring();
    check_threads();
    check_symbols();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("log: all checks passed\n");
    return 0;
}
