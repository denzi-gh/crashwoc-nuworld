#include "platform/update.h"

#include "platform/http.h"
#include "platform/log.h"

#define NW_UPDATE_TIMEOUT_MS 10000
#define NW_UPDATE_OPEN 1
#define NW_UPDATE_INSTALL 2

enum { NW_UPDATE_IDLE, NW_UPDATE_CHECKING, NW_UPDATE_NONE, NW_UPDATE_NEWER };

static SDL_AtomicInt nw_update_state, nw_update_on, nw_update_wanted, nw_update_updater;
static int nw_update_argc;
static char **nw_update_argv;
static char nw_update_build[32], nw_update_found[32]; /* found is written before the state turns NEWER */

static bool nw_update_numbers(const char *s, int v[3])
{
    s += *s == 'v';
    v[0] = v[1] = v[2] = 0;
    for (int i = 0; i < 3; i++) {
        if (!SDL_isdigit((unsigned char)*s)) {
            return false;
        }
        while (SDL_isdigit((unsigned char)*s)) {
            if (v[i] > 99999) {
                return false;
            }
            v[i] = v[i] * 10 + (*s++ - '0');
        }
        if (*s == '\0') {
            return true;
        }
        if (*s++ != '.') {
            return false;
        }
    }
    return false;
}

bool nw_update_compare(const char *a, const char *b, int *order)
{
    int va[3], vb[3];
    if (!nw_update_numbers(a, va) || !nw_update_numbers(b, vb)) {
        return false;
    }
    *order = 0;
    for (int i = 0; i < 3 && !*order; i++) {
        *order = (va[i] > vb[i]) - (va[i] < vb[i]);
    }
    return true;
}

bool nw_update_tag(const char *json, char *out, size_t cap)
{
    const char *p = SDL_strstr(json, "\"tag_name\"");
    if (!p || !cap) {
        return false;
    }
    p += SDL_strlen("\"tag_name\"");
    while (*p == ' ') {
        p++;
    }
    if (*p++ != ':') {
        return false;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p++ != '"') {
        return false;
    }
    size_t n = 0;
    while (p[n] && p[n] != '"' && n + 1 < cap) {
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
    int v[3];
    return p[n] == '"' && nw_update_numbers(out, v);
}

static int SDLCALL nw_update_run(void *unused)
{
    (void)unused;
    char agent[64];
    SDL_snprintf(agent, sizeof(agent), "NuWorld/%s", nw_update_build);
    char *body = NULL;
    int status = 0, order = 0, state = NW_UPDATE_NONE;
    if (!nw_http_get("https://api.github.com/repos/" NW_UPDATE_REPO "/releases/latest", agent, NW_UPDATE_TIMEOUT_MS,
                     &body, &status)) {
        nw_log_info(NW_LOG_NET, "update: %s", SDL_GetError());
    } else if (status != 200) {
        nw_log_info(NW_LOG_NET, "update: GitHub answered %d", status);
    } else if (!nw_update_tag(body, nw_update_found, sizeof(nw_update_found))) {
        nw_log_info(NW_LOG_NET, "update: no version tag in GitHub's answer");
    } else if (!nw_update_compare(nw_update_found, nw_update_build, &order)) {
        nw_log_info(NW_LOG_NET, "update: build version %s is not x.y.z", nw_update_build);
    } else if (order > 0) {
        nw_log_info(NW_LOG_NET, "update: %s is out", nw_update_found);
        state = NW_UPDATE_NEWER;
    }
    SDL_free(body);
    SDL_SetAtomicInt(&nw_update_state, state);
    return 0;
}

static bool nw_update_writable(void)
{
    const char *base = SDL_GetBasePath();
    char *updater = NULL, *probe = NULL;
    bool ok = base && SDL_asprintf(&updater, "%s" NW_UPDATE_UPDATER, base) >= 0 &&
              SDL_asprintf(&probe, "%s" NW_UPDATE_STAGE "-probe", base) >= 0 && SDL_GetPathInfo(updater, NULL) &&
              SDL_CreateDirectory(probe) && SDL_RemovePath(probe);
    SDL_free(updater);
    SDL_free(probe);
    return ok;
}

void nw_update_enable(bool on, const char *version)
{
    SDL_SetAtomicInt(&nw_update_on, on);
    if (!on || !SDL_CompareAndSwapAtomicInt(&nw_update_state, NW_UPDATE_IDLE, NW_UPDATE_CHECKING)) {
        return;
    }
    SDL_SetAtomicInt(&nw_update_updater, nw_update_writable());
    SDL_strlcpy(nw_update_build, version, sizeof(nw_update_build));
    SDL_Thread *thread = SDL_CreateThread(nw_update_run, "update", NULL);
    if (!thread) {
        nw_log_info(NW_LOG_NET, "update: %s", SDL_GetError());
        SDL_SetAtomicInt(&nw_update_state, NW_UPDATE_NONE);
        return;
    }
    SDL_DetachThread(thread); /* an exit during the check does not wait for its timeout */
}

bool nw_update_available(void)
{
    return SDL_GetAtomicInt(&nw_update_on) && SDL_GetAtomicInt(&nw_update_state) == NW_UPDATE_NEWER;
}

void nw_update_open(void)
{
    SDL_SetAtomicInt(&nw_update_wanted, NW_UPDATE_OPEN);
}

void nw_update_args(int argc, char **argv)
{
    nw_update_argc = argc;
    nw_update_argv = argv;
}

bool nw_update_installs(void)
{
    return SDL_GetAtomicInt(&nw_update_updater) != 0;
}

void nw_update_install(void)
{
    SDL_SetAtomicInt(&nw_update_wanted, NW_UPDATE_INSTALL);
}

static bool nw_update_launch(void)
{
    static SDL_Process *process;
    const char *base = SDL_GetBasePath();
    char *updater = NULL;
    int passed = SDL_max(nw_update_argc - 1, 0);
    const char **args = SDL_calloc((size_t)passed + 10, sizeof(*args));
    bool ok = base && args && SDL_asprintf(&updater, "%s" NW_UPDATE_UPDATER, base) >= 0;
    if (ok) {
        const char *fixed[] = { updater,         "--install-dir", base,     "--from",   nw_update_build,
                                "--to",          nw_update_found, "--wait", "--restart" };
        SDL_memcpy(args, fixed, sizeof(fixed));
        for (int i = 0; i < passed; i++) {
            args[SDL_arraysize(fixed) + (size_t)i] = nw_update_argv[i + 1];
        }
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        ok = process != NULL;
    }
    if (ok) {
        nw_log_info(NW_LOG_NET, "update: the updater installs %s", nw_update_found);
    } else {
        nw_log_warn(NW_LOG_NET, "update: the updater did not start: %s", SDL_GetError());
    }
    SDL_free(updater);
    SDL_free(args);
    return ok;
}

void nw_update_cleanup(const char *base)
{
    char *stage = NULL, *journal = NULL, *text = NULL, *left = NULL;
    if (base && SDL_asprintf(&stage, "%s" NW_UPDATE_STAGE, base) >= 0 &&
        SDL_asprintf(&journal, "%s/" NW_UPDATE_JOURNAL, stage) >= 0) {
        text = SDL_LoadFile(journal, NULL);
    }
    for (char *line = text, *next = NULL; line && *line; line = next) {
        next = SDL_strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        char *old = NULL, *grown = NULL;
        if (SDL_strncmp(line, "from ", 5) == 0) {
            nw_log_info(NW_LOG_APP, "updated from %s", line + 5);
        } else if (*line && *line != '/' && !SDL_strstr(line, "..") && !SDL_strchr(line, ':') &&
                   !SDL_strchr(line, '\\') && SDL_asprintf(&old, "%s%s" NW_UPDATE_OLD, base, line) >= 0 &&
                   SDL_GetPathInfo(old, NULL) && !SDL_RemovePath(old) &&
                   SDL_asprintf(&grown, "%s%s\n", left ? left : "", line) >= 0) {
            SDL_free(left);
            left = grown;
        }
        SDL_free(old);
    }
    if (text && left) {
        SDL_SaveFile(journal, left, SDL_strlen(left));
    } else if (text) {
        SDL_RemovePath(journal);
        SDL_RemovePath(stage);
    }
    SDL_free(stage);
    SDL_free(journal);
    SDL_free(text);
    SDL_free(left);
}

bool nw_update_poll(SDL_Window *window)
{
    int wanted = SDL_SetAtomicInt(&nw_update_wanted, 0);
    if (!wanted || !nw_update_available()) {
        return false;
    }
    if (wanted == NW_UPDATE_INSTALL && nw_update_launch()) {
        return true;
    }
    char url[160];
    SDL_snprintf(url, sizeof(url), "https://github.com/" NW_UPDATE_REPO "/releases/tag/%s", nw_update_found);
    if (!SDL_OpenURL(url)) {
        nw_log_warn(NW_LOG_NET, "update: cannot open %s: %s", url, SDL_GetError());
        return false;
    }
    if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
        SDL_MinimizeWindow(window);
    }
    return false;
}
