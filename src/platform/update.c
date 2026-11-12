#include "platform/update.h"

#include "platform/http.h"

#define NW_UPDATE_REPO "denzi-gh/crashwoc-nuworld"
#define NW_UPDATE_TIMEOUT_MS 10000

enum { NW_UPDATE_IDLE, NW_UPDATE_CHECKING, NW_UPDATE_NONE, NW_UPDATE_NEWER };

static SDL_AtomicInt nw_update_state, nw_update_on, nw_update_wanted;
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
        SDL_Log("update: %s", SDL_GetError());
    } else if (status != 200) {
        SDL_Log("update: GitHub answered %d", status);
    } else if (!nw_update_tag(body, nw_update_found, sizeof(nw_update_found))) {
        SDL_Log("update: no version tag in GitHub's answer");
    } else if (!nw_update_compare(nw_update_found, nw_update_build, &order)) {
        SDL_Log("update: build version %s is not x.y.z", nw_update_build);
    } else if (order > 0) {
        SDL_Log("update: %s is out", nw_update_found);
        state = NW_UPDATE_NEWER;
    }
    SDL_free(body);
    SDL_SetAtomicInt(&nw_update_state, state);
    return 0;
}

void nw_update_enable(bool on, const char *version)
{
    SDL_SetAtomicInt(&nw_update_on, on);
    if (!on || !SDL_CompareAndSwapAtomicInt(&nw_update_state, NW_UPDATE_IDLE, NW_UPDATE_CHECKING)) {
        return;
    }
    SDL_strlcpy(nw_update_build, version, sizeof(nw_update_build));
    SDL_Thread *thread = SDL_CreateThread(nw_update_run, "update", NULL);
    if (!thread) {
        SDL_Log("update: %s", SDL_GetError());
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
    SDL_SetAtomicInt(&nw_update_wanted, 1);
}

void nw_update_poll(SDL_Window *window)
{
    if (!SDL_SetAtomicInt(&nw_update_wanted, 0) || !nw_update_available()) {
        return;
    }
    char url[160];
    SDL_snprintf(url, sizeof(url), "https://github.com/" NW_UPDATE_REPO "/releases/tag/%s", nw_update_found);
    if (!SDL_OpenURL(url)) {
        SDL_Log("update: cannot open %s: %s", url, SDL_GetError());
        return;
    }
    if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
        SDL_MinimizeWindow(window);
    }
}
