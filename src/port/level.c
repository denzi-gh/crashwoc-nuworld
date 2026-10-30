#include "port/level.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "port/heap.h"
#include "vfs/manifest.h"

#define NW_LEVEL_TITLES 0x23
#define NW_LEVEL_HUB 0x25
#define NW_LEVEL_SPACE 0x28
#define NW_LEVEL_ROUTE 8

extern struct ldata_s LData[44];
extern char *LevelName[44][6];
extern s32 Level, Hub, GameMode, Demo, editor_active, cutmovie, gamecut_newlevel;
extern s32 new_level, new_mode, next_cut_movie;
extern s32 fadeval, fade_rate, fadehack;
extern volatile s32 bgload_complete;
s32 HubFromLevel(s32 level);

/* TITLES' own level, which retail never loads: the main loop turns TITLES into the title (the warp room's title cut). */
#define NW_LEVEL_OLD_TITLE ((int)SDL_arraysize(LData))
/* A route's NEW GAME. */
#define NW_LEVEL_INTRO (-2)

static struct {
    SDL_AtomicInt asked; /* level + 1, 0 for none */
    int entering;        /* the level last entered, until it runs: a cutscene may come first */
    bool steady;
    signed char present[SDL_arraysize(LData)]; /* 0 not looked up, 1 on the disc, -1 not */
    int arrival;           /* --level's leg on the way, -1 for none */
    bool on_way;           /* entered, not yet running */
    bool intro;            /* NEW GAME not yet chosen */
    int route[NW_LEVEL_ROUTE], nroute, leg;
    NwLevelArrival arrivals[NW_LEVEL_ROUTE];
    SDL_AtomicInt arrived; /* 1 once the last runs */
    int logged;            /* the level the log named last */
} nw_level = { .entering = -1, .arrival = -1, .logged = -1 };

int nw_level_count(void)
{
    return NW_LEVEL_OLD_TITLE + 1;
}

const char *nw_level_name(int level)
{
    if (level < 0 || level >= nw_level_count()) {
        return NULL;
    }
    if (level == NW_LEVEL_OLD_TITLE) {
        return nw_level_name(NW_LEVEL_TITLES) ? "TITLES (UNUSED LEVEL)" : NULL;
    }
    if (!nw_level.present[level]) {
        char path[128];
        SDL_snprintf(path, sizeof(path), "LEVELS/%s.GSC", LData[level].path);
        for (char *c = path; *c; c++) {
            if (*c == '\\') {
                *c = '/';
            }
        }
        nw_level.present[level] = nw_manifest_find(&nw_disc_manifest, path) ? 1 : -1;
    }
    return nw_level.present[level] > 0 ? LevelName[level][0] : NULL;
}

int nw_level_current(void)
{
    return cutmovie == 0 ? NW_LEVEL_TITLES : Level == NW_LEVEL_TITLES ? NW_LEVEL_OLD_TITLE : Level;
}

int nw_level_pending(void)
{
    return SDL_GetAtomicInt(&nw_level.asked) - 1;
}

void nw_level_load(int level)
{
    if (nw_level_name(level)) {
        SDL_SetAtomicInt(&nw_level.asked, level + 1);
    }
}

static int nw_level_find(const char *name)
{
    char *end;
    long level = SDL_strtol(name, &end, 10);
    if (*end) {
        level = -1;
        for (int i = 0; i < (int)SDL_arraysize(LData) && level < 0; i++) {
            const char *file = LData[i].path ? SDL_strrchr(LData[i].path, '\\') : NULL;
            if (file && SDL_strcasecmp(file + 1, name) == 0) {
                level = i;
            }
        }
    }
    return level >= 0 && level < (long)SDL_arraysize(LData) && nw_level_name((int)level) ? (int)level : -1;
}

/* Asks for the route's next leg. */
static void nw_level_next(void)
{
    int level = nw_level.route[nw_level.leg];
    nw_level.intro = level == NW_LEVEL_INTRO;
    nw_level.arrival = nw_level.intro ? NW_LEVEL_HUB : level;
    if (!nw_level.intro) {
        nw_level_load(level);
    }
}

bool nw_level_start(const char *names)
{
    char name[64];
    nw_level.nroute = 0;
    for (const char *p = names; *p;) {
        const char *comma = SDL_strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : SDL_strlen(p);
        if (nw_level.nroute == NW_LEVEL_ROUTE || len >= sizeof(name)) {
            return false;
        }
        SDL_strlcpy(name, p, len + 1);
        int level = nw_level.nroute == 0 && SDL_strcasecmp(name, "intro") == 0 ? NW_LEVEL_INTRO : nw_level_find(name);
        if (level == -1) {
            return false;
        }
        nw_level.route[nw_level.nroute++] = level;
        p += len + (p[len] == ',');
    }
    if (nw_level.nroute == 0 || nw_level.route[nw_level.nroute - 1] == NW_LEVEL_INTRO) {
        return false;
    }
    nw_level_next();
    return true;
}

int nw_level_final(void)
{
    return nw_level.nroute ? nw_level.route[nw_level.nroute - 1] : -1;
}

int nw_level_arrival(void)
{
    return nw_level.arrival;
}

bool nw_level_arrived(void)
{
    return SDL_GetAtomicInt(&nw_level.arrived) != 0;
}

int nw_level_arrivals(const NwLevelArrival **out)
{
    *out = nw_level.arrivals;
    return nw_level.leg;
}

/* Ends the running level, title or cutscene at once: the screen is already faded out. */
static void nw_level_cut(void)
{
    fadeval = 0xFF;
    fade_rate = 0;
    fadehack = 0;
    nw_level.steady = false;
}

/* As retail's level select and demo start; the title loads Level when it ends, past the main loop's turn of TITLES
   into the title, so only there TITLES' own level can load. */
static void nw_level_enter(int level)
{
    if (cutmovie == 0) {
        if (level == NW_LEVEL_TITLES) {
            return;
        }
        if (level == NW_LEVEL_OLD_TITLE) {
            level = NW_LEVEL_TITLES;
        }
        Level = level;
    }
    new_level = level;
    Hub = HubFromLevel(level);
    nw_level_cut();
}

bool nw_level_routing(void)
{
    return nw_level.intro || nw_level_pending() >= 0;
}

void nw_level_titles(void)
{
    Level = NW_LEVEL_TITLES;
    new_level = NW_LEVEL_TITLES;
    Hub = -1;
    nw_level_cut();
}

/* Acts after two steady swaps (loads done, faded in, which BGLoad's loading screen never is, no other level change),
   since InitGameMode drops a new_level set before the level loop; an arrival cutscene is skipped as Start skips it. */
void nw_level_poll(void)
{
    /* The title keeps the new_level that led to it until its level starts. */
    bool steady = bgload_complete && fadeval == 0 && (new_level == -1 || cutmovie == 0) && new_mode == -1 &&
                  cutmovie <= 0 && !Demo && !editor_active;
    bool ready = steady && nw_level.steady;
    nw_level.steady = steady;
    if (steady && Level != nw_level.logged) {
        const char *name = nw_level_name(Level);
        nw_level.logged = Level;
        SDL_Log("level %d: %s", Level, name ? name : "?");
    }
    if (!ready) {
        return;
    }
    int level = nw_level_pending();
    if (nw_level.intro && cutmovie == 0) {
        /* As the name entry's last item (game.c); the hub's arrival cut is skipped as for an entered level. */
        nw_level.intro = false;
        nw_level.on_way = true;
        nw_level.entering = NW_LEVEL_HUB;
        new_level = NW_LEVEL_HUB;
        next_cut_movie = 1;
        fade_rate = 0xA;
    } else if (level == NW_LEVEL_OLD_TITLE && cutmovie != 0) {
        /* The title first; the request stays for it. */
        nw_level_enter(NW_LEVEL_TITLES);
    } else if (level >= 0 && SDL_CompareAndSwapAtomicInt(&nw_level.asked, level + 1, 0)) {
        nw_level.entering = level;
        nw_level.on_way = level == nw_level.arrival;
        nw_level_enter(level);
    } else if (nw_level.entering >= 0) {
        if (GameMode == 1 && Level == NW_LEVEL_SPACE && gamecut_newlevel == nw_level.entering) {
            nw_level_enter(nw_level.entering);
        } else if (GameMode == 1 && Level == nw_level.entering) {
            new_mode = 0;
            nw_level_cut();
        }
        nw_level.entering = -1;
    } else if (nw_level.on_way && Level == nw_level.arrival && GameMode == 0 && cutmovie < 0) {
        nw_level.on_way = false;
        nw_level.arrivals[nw_level.leg] = (NwLevelArrival){ Level, nw_heap_gpu() };
        if (++nw_level.leg < nw_level.nroute) {
            nw_level_next();
        } else {
            SDL_SetAtomicInt(&nw_level.arrived, 1);
        }
    }
}
