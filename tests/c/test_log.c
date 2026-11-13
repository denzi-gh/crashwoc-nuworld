/* platform/log.c: the home folder in log lines. */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "platform/log.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

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

int main(void)
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

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("log: all checks passed\n");
    return 0;
}
