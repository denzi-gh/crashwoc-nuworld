/* game/cut's StartCutMovie (port/units.toml wrap): --cut's shot started in place of the movie's first. */
#include "port/cutstart.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "gamelib/gcutscn.h"
#include "nu_types.h"

#include "game/cut.retail.h"

#define NW_CUTSTART_SHOTS 32 /* CutInst's */

extern s32 cutworldix;
extern struct instNUGCUTSCENE_s *CutInst[];

static char nw_cutstart_name[32]; /* lowered, without ".cut"; empty once used */

void nw_cut_start(const char *name)
{
    size_t n = SDL_strlcpy(nw_cutstart_name, name, sizeof(nw_cutstart_name));
    if (n >= 4 && SDL_strcasecmp(nw_cutstart_name + n - 4, ".cut") == 0) {
        nw_cutstart_name[n - 4] = '\0';
    }
    for (char *c = nw_cutstart_name; *c; c++) {
        *c = (char)SDL_tolower((unsigned char)*c);
    }
}

/* The shot an instance plays: its name (the .cut's path's last 16 characters, not always ended) after its last
   backslash, without ".cut", lowered. */
static void nw_cutstart_shot(const struct instNUGCUTSCENE_s *inst, char *out, size_t size)
{
    char name[sizeof(inst->name) + 1];
    memcpy(name, inst->name, sizeof(inst->name));
    name[sizeof(inst->name)] = '\0';
    const char *base = SDL_strrchr(name, '\\');
    SDL_strlcpy(out, base ? base + 1 : name, size);
    size_t n = SDL_strlen(out);
    if (n >= 4 && SDL_strcasecmp(out + n - 4, ".cut") == 0) {
        out[n - 4] = '\0';
    }
    for (char *c = out; *c; c++) {
        *c = (char)SDL_tolower((unsigned char)*c);
    }
}

/* The movie's shots are chained, each starting the next as it ends (LoadCutComponents), and StartCutMovie starts the
   first, with the audio stream of the shot cutworldix names: the shot asked for stands in for the first while it
   starts, so it runs with its own stream (a shot that has none, as sunny and storm, runs silent) and its chain. */
void StartCutMovie(void)
{
    int shot = -1;
    for (int i = 1; nw_cutstart_name[0] && i < NW_CUTSTART_SHOTS && CutInst[i]; i++) {
        char name[32];
        nw_cutstart_shot(CutInst[i], name, sizeof(name));
        if (SDL_strcmp(name, nw_cutstart_name) == 0) {
            shot = i;
            break;
        }
    }
    if (shot < 0) {
        StartCutMovie__retail();
        return;
    }
    SDL_Log("--cut: the movie starts at shot %d, %s", shot, nw_cutstart_name);
    nw_cutstart_name[0] = '\0';
    struct instNUGCUTSCENE_s *first = CutInst[0];
    CutInst[0] = CutInst[shot];
    cutworldix = shot;
    StartCutMovie__retail();
    CutInst[0] = first;
}
