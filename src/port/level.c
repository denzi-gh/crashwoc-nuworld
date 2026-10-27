#include "port/level.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "vfs/manifest.h"

#define NW_LEVEL_TITLES 0x23
#define NW_LEVEL_SPACE 0x28

extern struct ldata_s LData[44];
extern char *LevelName[44][6];
extern s32 Level, Hub, GameMode, Demo, editor_active, cutmovie, gamecut_newlevel;
extern s32 new_level, new_mode;
extern s32 fadeval, fade_rate, fadehack;
extern volatile s32 bgload_complete;
s32 HubFromLevel(s32 level);

/* TITLES' own level, which retail never loads: the main loop turns TITLES into the title (the warp room's title cut). */
#define NW_LEVEL_OLD_TITLE ((int)SDL_arraysize(LData))

static struct {
    SDL_AtomicInt asked; /* level + 1, 0 for none */
    int entering;        /* the level last entered, until it runs: a cutscene may come first */
    bool steady;
    signed char present[SDL_arraysize(LData)]; /* 0 not looked up, 1 on the disc, -1 not */
} nw_level = { .entering = -1 };

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

/* Ends the running level, title or cutscene at once: the screen is already faded out. */
static void nw_level_cut(void)
{
    fadeval = 0xFF;
    fade_rate = 0;
    fadehack = 0;
    nw_level.steady = false;
}

/* As retail's level select and demo start enter a level. The title (PlayCutMovie) loads Level once it ends, past the
   main loop's turn of TITLES into the title: there TITLES is the title already, and only there its own level loads. */
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

/* Steady: the load thread is done and the screen faded in, at this swap and the one before, which BGLoad's loading
   screen never is (it fades out right after the load), and nothing else is changing the level. InitGameMode would
   drop a new_level set before the level loop. A cutscene on arrival (a hub's, played in space before its level, or
   the warp room's intro) is skipped as Start skips it. */
void nw_level_poll(void)
{
    /* The title keeps the new_level that led to it until its level starts. */
    bool steady = bgload_complete && fadeval == 0 && (new_level == -1 || cutmovie == 0) && new_mode == -1 &&
                  cutmovie <= 0 && !Demo && !editor_active;
    bool ready = steady && nw_level.steady;
    nw_level.steady = steady;
    if (!ready) {
        return;
    }
    int level = nw_level_pending();
    if (level == NW_LEVEL_OLD_TITLE && cutmovie != 0) {
        /* The title first; the request stays for it. */
        nw_level_enter(NW_LEVEL_TITLES);
    } else if (level >= 0 && SDL_CompareAndSwapAtomicInt(&nw_level.asked, level + 1, 0)) {
        nw_level.entering = level;
        nw_level_enter(level);
    } else if (nw_level.entering >= 0) {
        if (GameMode == 1 && Level == NW_LEVEL_SPACE && gamecut_newlevel == nw_level.entering) {
            nw_level_enter(nw_level.entering);
        } else if (GameMode == 1 && Level == nw_level.entering) {
            new_mode = 0;
            nw_level_cut();
        }
        nw_level.entering = -1;
    }
}
