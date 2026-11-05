#include "gameplay/hubcrunch.h"

#include <math.h>
#include <stdbool.h>

#include <SDL3/SDL.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "gameplay/minigame.h"
#include "nu3d/nuanim.h"
#include "nu3d/nuhgobj.h"
#include "nu_port.h"
#include "nucore/nucoretypes.h"
#include "numath/numtx.h"
#include "nups2/ps2ctl.h"
#include "port/level.h"

/* Declared by no header. */
extern s32 Level, cutmovie, Demo, Paused, new_level, new_mode;
void *NuDatOpen(char *name, char **mem, s32 *used);
void NuDatSet(void *dat);
void NuDatClose(void *dat);
void NuHGobjEvalAnim(struct NUHGOBJ_s *hobj, struct nuanimdata_s *animdata, float time, int njanims,
                     struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);
void NuHGobjEval(struct NUHGOBJ_s *hobj, int njanims, struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa);
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);

#define NW_CRUNCH_HUB 0x25
#define NW_CRUNCH_MODEL "chars\\d\\crunch\\crunch.ghg"
#define NW_CRUNCH_IDLE "chars\\d\\crunch\\a\\idle.ani"
#define NW_CRUNCH_BYTES 0x200000
#define NW_CRUNCH_ANIM_STEP 0.5f /* idle frames per 50 Hz frame */
#define NW_CRUNCH_RANGE 2.0f     /* the prompt's */
#define NW_CRUNCH_RADIUS 0.9f    /* Crash keeps this far */
#define NW_CRUNCH_CROSS 0x40

/* The top floor, beside its middle (game.c's hubmidpos[1]: 0, 37.25, 10). */
static const struct nuvec_s nw_crunch_pos = { 2.5f, 37.15f, 11.5f };

static struct {
    char *buf;
    struct NUHGOBJ_s *obj;
    struct nuanimdata_s *idle;
    bool failed;
    float time;
    u16 yaw;
    bool near;
} nw_crunch;

static bool nw_crunch_here(void)
{
    return nw_profile.hub_minigame && Level == NW_CRUNCH_HUB && cutmovie == -1 && !Demo;
}

static void nw_crunch_load(void)
{
    if (!nw_crunch.buf && !(nw_crunch.buf = SDL_malloc(NW_CRUNCH_BYTES))) {
        nw_crunch.failed = true;
        return;
    }
    union variptr_u buf = { .charptr = nw_crunch.buf }, end = { .charptr = nw_crunch.buf + NW_CRUNCH_BYTES };
    void *dat = NuDatOpen("chars.dat", NULL, NULL);
    NuDatSet(dat);
    nw_crunch.obj = NuGHGRead(&buf, NW_CRUNCH_MODEL);
    nw_crunch.idle = nw_crunch.obj ? NuAnimDataLoadBuff(NW_CRUNCH_IDLE, &buf, &end) : NULL;
    NuDatSet(NULL);
    if (dat) {
        NuDatClose(dat);
    }
    nw_crunch.failed = !nw_crunch.obj;
    nw_crunch.time = 1.0f;
}

struct nupad_s *nw_crunch_pad(struct creature_s *c, struct nupad_s *pad)
{
    static struct nupad_s held;
    nw_crunch.near = false;
    if (!nw_crunch_here() || c != &Character[0]) {
        return pad;
    }
    NW_DEVIATION(hub_minigame);
    if (!nw_crunch.obj && !nw_crunch.failed) {
        nw_crunch_load();
    }
    if (!nw_crunch.obj) {
        return pad;
    }
    if (nw_crunch.idle) {
        nw_crunch.time += NW_CRUNCH_ANIM_STEP * NW_HZ(1.0f, 50.0f / 60.0f);
        if (nw_crunch.time > nw_crunch.idle->time) {
            nw_crunch.time -= nw_crunch.idle->time - 1.0f;
        }
    }
    float dx = c->obj.pos.x - nw_crunch_pos.x, dz = c->obj.pos.z - nw_crunch_pos.z;
    /* His model faces -z. */
    nw_crunch.yaw = (u16)((int)(atan2f(dx, dz) * (32768.0f / SDL_PI_F)) + 0x8000);
    nw_crunch.near = dx * dx + dz * dz < NW_CRUNCH_RANGE * NW_CRUNCH_RANGE &&
                     SDL_fabsf(c->obj.pos.y - nw_crunch_pos.y) < NW_CRUNCH_RANGE;
    if (!nw_crunch.near || !pad || Paused || new_level != -1 || new_mode != -1 || nw_level_pending() >= 0 ||
        !(pad->paddata_db & NW_CRUNCH_CROSS)) {
        return pad;
    }
    nw_minigame_start();
    held = *pad;
    held.paddata &= ~(u32)NW_CRUNCH_CROSS;
    held.paddata_db &= ~(u32)NW_CRUNCH_CROSS;
    return &held;
}

void nw_crunch_push(struct creature_s *c)
{
    if (!nw_crunch.obj || !nw_crunch_here() || c != &Character[0]) {
        return;
    }
    float dx = c->obj.pos.x - nw_crunch_pos.x, dz = c->obj.pos.z - nw_crunch_pos.z;
    float d = sqrtf(dx * dx + dz * dz);
    if (d >= NW_CRUNCH_RADIUS || SDL_fabsf(c->obj.pos.y - nw_crunch_pos.y) > 2.0f) {
        return;
    }
    NW_DEVIATION(hub_minigame);
    if (d < 0.001f) {
        dx = 0.0f;
        dz = d = 1.0f;
    }
    c->obj.pos.x = nw_crunch_pos.x + dx / d * NW_CRUNCH_RADIUS;
    c->obj.pos.z = nw_crunch_pos.z + dz / d * NW_CRUNCH_RADIUS;
}

void nw_crunch_draw(void)
{
    if (!nw_crunch.obj || !nw_crunch_here()) {
        return;
    }
    NW_DEVIATION(hub_minigame);
    static struct numtx_s tmtx[256];
    short layers[5] = { 0, 1, 2, 3, 4 };
    if (nw_crunch.idle) {
        NuHGobjEvalAnim(nw_crunch.obj, nw_crunch.idle, nw_crunch.time, 0, NULL, tmtx);
    } else {
        NuHGobjEval(nw_crunch.obj, 0, NULL, tmtx);
    }
    struct numtx_s m;
    NuMtxSetRotationY(&m, nw_crunch.yaw);
    m._30 = nw_crunch_pos.x;
    m._31 = nw_crunch_pos.y;
    m._32 = nw_crunch_pos.z;
    NuHGobjRndrMtxDwa(nw_crunch.obj, &m, 2, layers, tmtx, NULL);
}

void nw_crunch_prompt(void)
{
    if (!nw_crunch.near || !nw_crunch_here() || Paused || nw_level_pending() >= 0) {
        return;
    }
    NW_DEVIATION(hub_minigame);
    /* x is the cross button's glyph. */
    Text3D("PRESS x TO START", 0.0f, -0.6f, 1.0f, 0.5f, 0.5f, 0.5f, 0, 0);
    Text3D("LOADING MINIGAME", 0.0f, -0.75f, 1.0f, 0.5f, 0.5f, 0.5f, 0, 0);
}

void nw_crunch_close(void)
{
    if (nw_crunch.obj) {
        NuHGobjDestroy(nw_crunch.obj);
        nw_crunch.obj = NULL;
        nw_crunch.idle = NULL;
    }
    nw_crunch.failed = false;
    nw_crunch.near = false;
}
