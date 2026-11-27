#include "port/level.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "platform/log.h"
#include "port/heap.h"
#include "port/nameentry.h"
#include "shim/shim.h"
#include "vfs/vfs.h"

#define NW_LEVEL_TITLES 0x23
#define NW_LEVEL_ROUTE 8

/* game.c's warp rooms: each room's levels in the game's order, the boss last. */
struct hdata_s {
    s8 level[6];
    s8 i_spl[2];
    u8 barrier;
    u8 i_gdeb;
    short sfx;
};

extern struct ldata_s LData[44];
extern struct hdata_s HData[6];
extern char *LevelName[44][6];
extern s32 Level, Hub, GameMode, Demo, editor_active, cutmovie, gamecut_newlevel;
extern s32 new_level, new_mode, next_cut_movie, gamecut, gamecut_hack, logos_played;
extern void *SpaceGameCutTab[26][2];
extern struct game_s Game;
extern struct cursor_s Cursor;
extern s32 fadeval, fade_rate, fadehack;
extern int nw_hz;
extern volatile s32 bgload_complete;
s32 HubFromLevel(s32 level);
void NewMenu(struct cursor_s *cursor, s32 menu, s32 y, s32 level);

/* TITLES' own level, which retail never loads: the main loop turns TITLES into the title (the warp room's cut). */
#define NW_LEVEL_OLD_TITLE ((int)SDL_arraysize(LData))
/* A route's NEW GAME. */
#define NW_LEVEL_INTRO (-2)

static struct {
    SDL_AtomicInt asked; /* level + 1, 0 for none */
    int entering;        /* the level last entered, until it runs: a cutscene may come first */
    bool steady;
    signed char present[SDL_arraysize(LData)]; /* 0 not looked up, 1 on the disc, -1 not */
    int arrival;           /* --level's leg on the way, -1 for none */
    bool on_way;
    bool intro;            /* NEW GAME not yet chosen */
    int route[NW_LEVEL_ROUTE], nroute, leg;
    NwLevelArrival arrivals[NW_LEVEL_ROUTE];
    SDL_AtomicInt arrived; /* 1 once the last runs */
    int logged;
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
        nw_level.present[level] = nw_manifest_find(nw_shim_file_vfs()->manifest, path) ? 1 : -1;
    }
    return nw_level.present[level] > 0 ? LevelName[level][0] : NULL;
}

int nw_level_at(int i, int *room, int *slot)
{
    static struct {
        signed char level, room, slot;
    } order[NW_LEVEL_OLD_TITLE + 1];
    static bool built;
    if (!built) {
        bool listed[NW_LEVEL_OLD_TITLE + 1] = { false };
        int n = 0;
        for (int r = 0; r < (int)SDL_arraysize(HData); r++) {
            for (int s = 0; s < (int)SDL_arraysize(HData[r].level); s++) {
                int level = HData[r].level[s];
                /* The sixth room's last slot is the game over chamber, not a boss. */
                if (level >= 0 && level < nw_level_count() && level != NW_LEVEL_GAMEOVER && !listed[level]) {
                    order[n].level = (signed char)level;
                    order[n].room = (signed char)(r + 1);
                    order[n++].slot = (signed char)s;
                    listed[level] = true;
                }
            }
        }
        for (int level = 0; level < nw_level_count(); level++) {
            if (!listed[level]) {
                order[n].level = (signed char)level;
                order[n].room = 0;
                order[n++].slot = 0;
            }
        }
        built = true;
    }
    if (i < 0 || i >= nw_level_count()) {
        return -1;
    }
    *room = order[i].room;
    *slot = order[i].slot;
    return order[i].level;
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

/* At once: the screen is already faded out. */
static void nw_level_cut(void)
{
    fadeval = 0xFF;
    fade_rate = 0;
    fadehack = 0;
    nw_level.steady = false;
}

/* As retail's level select and demo start; only the title's end can load TITLES' own level. */
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

typedef struct NwCutscene {
    const char *group, *name;
    signed char movie, gamecut;
} NwCutscene;

static const NwCutscene nw_cutscenes[] = {
    { "Movies", "Logos and title", 0, -1 },
    { "Movies", "Intro", 1, -1 },
    { "Movies", "Ending", 3, -1 },
    { "Movies", "Ending (all gems)", 4, -1 },
    { "Warp Room 1", "Arrival", -1, 0 },
    { "Warp Room 1", "First level", -1, 1 },
    { "Warp Room 1", "2 crystals", -1, 2 },
    { "Warp Room 1", "4 crystals", -1, 3 },
    { "Warp Room 1", "Before the boss", -1, 4 },
    { "Warp Room 1", "Back from the boss", -1, 5 },
    { "Warp Room 1", "Leaving after the boss", -1, 6 },
    { "Warp Room 2", "First level", -1, 7 },
    { "Warp Room 2", "2 crystals", -1, 8 },
    { "Warp Room 2", "4 crystals", -1, 9 },
    { "Warp Room 2", "Before the boss", -1, 10 },
    { "Warp Room 2", "Leaving after the boss", -1, 11 },
    { "Warp Room 3", "First level", -1, 12 },
    { "Warp Room 3", "2 crystals", -1, 13 },
    { "Warp Room 3", "4 crystals", -1, 14 },
    { "Warp Room 3", "Before the boss", -1, 15 },
    { "Warp Room 3", "Leaving after the boss", -1, 16 },
    { "Warp Room 4", "First level", -1, 17 },
    { "Warp Room 4", "2 crystals", -1, 18 },
    { "Warp Room 4", "4 crystals", -1, 19 },
    { "Warp Room 4", "Before the boss", -1, 20 },
    { "Warp Room 5", "First level", -1, 22 },
    { "Warp Room 5", "2 crystals", -1, 23 },
    { "Warp Room 5", "4 crystals", -1, 24 },
    { "Warp Room 5", "Before the boss", -1, 25 },
};

static struct {
    SDL_AtomicInt asked;
    int next, space, title_movie, hub, ret;
    bool started;
} nw_cut = { .next = -1, .space = -1, .title_movie = -1, .hub = -1 };

static bool nw_cutscene_hub(const NwCutscene *c)
{
    return c->movie < 0 && !SpaceGameCutTab[c->gamecut][0];
}

int nw_cutscene_count(void)
{
    return (int)SDL_arraysize(nw_cutscenes);
}

const char *nw_cutscene_name(int i, const char **group)
{
    if (i < 0 || i >= nw_cutscene_count()) {
        return NULL;
    }
    *group = nw_cutscenes[i].group;
    return nw_cutscenes[i].name;
}

int nw_cutscene_current(void)
{
    for (int i = 0; i < nw_cutscene_count(); i++) {
        const NwCutscene *c = &nw_cutscenes[i];
        if (c->movie >= 0 ? cutmovie == c->movie
                          : cutmovie < 0 && GameMode == 1 && gamecut == c->gamecut &&
                                Level == (nw_cutscene_hub(c) ? NW_LEVEL_HUB : NW_LEVEL_SPACE)) {
            return i;
        }
    }
    return -1;
}

int nw_cutscene_pending(void)
{
    return SDL_GetAtomicInt(&nw_cut.asked) - 1;
}

void nw_cutscene_play(int i)
{
    const char *group;
    if (nw_cutscene_name(i, &group)) {
        SDL_SetAtomicInt(&nw_cut.asked, i + 1);
    }
}

bool nw_cutscene_route(int next)
{
    int expect = nw_cut.next;
    nw_cut.next = -1;
    if (expect != next) {
        nw_cut.space = -1;
        return false;
    }
    gamecut = nw_cut.space;
    if (nw_cut.space >= 0) {
        gamecut_newlevel = next;
        new_level = NW_LEVEL_SPACE;
        nw_cut.space = -1;
        nw_cut.next = next;
    } else if (next != NW_LEVEL_HUB) {
        gamecut_hack = 0;
    }
    return true;
}

static void nw_cutscene_title_next(int movie)
{
    nw_nameentry_finish();
    NewMenu(&Cursor, -1, -1, -1);
    next_cut_movie = movie;
    nw_level_cut();
}

static void nw_cutscene_start(const NwCutscene *c)
{
    bool title = cutmovie == 0;
    int ret = title                    ? NW_LEVEL_TITLES
              : Level != NW_LEVEL_SPACE ? Level
              : gamecut_newlevel >= 0   ? gamecut_newlevel
                                        : NW_LEVEL_HUB;
    nw_cut.next = nw_cut.space = nw_cut.title_movie = nw_cut.hub = -1;
    nw_cut.started = false;
    if (c->movie == 0) {
        logos_played = 0;
        if (title) {
            nw_cutscene_title_next(0);
        } else {
            nw_cut.next = NW_LEVEL_TITLES;
            nw_level_enter(NW_LEVEL_TITLES);
        }
    } else if (c->movie > 0) {
        if (title) {
            logos_played = 1;
            nw_cut.title_movie = c->movie;
            nw_cutscene_title_next(c->movie);
        } else {
            cutmovie = c->movie;
            nw_cut.next = ret;
            nw_level_enter(ret);
        }
    } else if (nw_cutscene_hub(c)) {
        nw_cut.hub = c->gamecut;
        nw_cut.ret = ret;
        if (title) {
            Game.cutbits |= c->gamecut != 0;
            nw_level_enter(NW_LEVEL_HUB);
        } else if (Level != NW_LEVEL_HUB) {
            nw_cut.next = NW_LEVEL_HUB;
            nw_level_enter(NW_LEVEL_HUB);
        }
    } else if (title) {
        gamecut = c->gamecut;
        gamecut_newlevel = NW_LEVEL_TITLES;
        nw_cut.next = NW_LEVEL_TITLES;
        nw_level_enter(NW_LEVEL_SPACE);
    } else {
        nw_cut.space = c->gamecut;
        nw_cut.next = ret;
        nw_level_enter(ret);
    }
}

static bool nw_cutscene_poll(bool ready)
{
    if (nw_cut.title_movie > 0) {
        if (cutmovie == nw_cut.title_movie) {
            nw_cut.started = true;
            next_cut_movie = 0;
        } else if (nw_cut.started) {
            nw_cut.title_movie = -1;
        }
    }
    if (nw_cut.hub < 0) {
        return false;
    }
    if (Level == NW_LEVEL_HUB && cutmovie < 0 && GameMode == 1 && gamecut == nw_cut.hub) {
        nw_cut.started = true;
    }
    if (!ready || cutmovie >= 0 || GameMode != 0) {
        return false;
    }
    if (Level != NW_LEVEL_HUB) {
        nw_cut.hub = -1;
        return false;
    }
    if (!nw_cut.started) {
        gamecut = nw_cut.hub;
        Hub = -1;
        new_mode = 1;
        nw_level_cut();
        return true;
    }
    nw_cut.hub = -1;
    if (nw_cut.ret == NW_LEVEL_HUB) {
        return false;
    }
    nw_cut.next = nw_cut.ret;
    nw_level_enter(nw_cut.ret);
    return true;
}

/* Acts after two steady swaps, since InitGameMode drops a new_level set before the level loop. */
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
        nw_log_info(NW_LOG_GAME, "level %d: %s", Level, name ? name : "?");
    }
    if (nw_cutscene_poll(ready) || !ready) {
        return;
    }
    int level = nw_level_pending();
    int cut = nw_cutscene_pending();
    if (nw_level.intro && cutmovie == 0) {
        /* As the name entry's last item (game.c); the hub's arrival cut is skipped as for an entered level. */
        nw_level.intro = false;
        nw_level.on_way = true;
        nw_level.entering = NW_LEVEL_HUB;
        new_level = NW_LEVEL_HUB;
        next_cut_movie = 1;
        fade_rate = nw_hz == 60 ? 8 : 0xA;
    } else if (level == NW_LEVEL_OLD_TITLE && cutmovie != 0) {
        /* The title first; the request stays for it. */
        nw_level_enter(NW_LEVEL_TITLES);
    } else if (level >= 0 && SDL_CompareAndSwapAtomicInt(&nw_level.asked, level + 1, 0)) {
        nw_level.entering = level;
        nw_level.on_way = level == nw_level.arrival;
        nw_level_enter(level);
    } else if (cut >= 0 && SDL_CompareAndSwapAtomicInt(&nw_cut.asked, cut + 1, 0)) {
        nw_cutscene_start(&nw_cutscenes[cut]);
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
