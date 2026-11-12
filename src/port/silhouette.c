/* game/cut's AppCutSceneCharacterRender (port/units.toml wrap): control.cut's heroes in its first shot. */
#include "port/silhouette.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamelib/gcutscn.h"
#include "gfx/packet.h"
#include "nu3d/nuanim.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuhgobj.h"
#include "nu_types.h"
#include "numath/numtx.h"

#include "game/cut.retail.h"

/* nu3d/nuhgobj's and the archives', as game/cut and game/creature declare them */
extern s32 NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *m, s32 nlayers, short *layer,
                             struct numtx_s *tmtx, float **dwa);
extern void NuHGobjEvalAnim2(struct NUHGOBJ_s *obj, void *joint_anim, f32 frame, s32 njoints, void *joints,
                             struct numtx_s *tmtx);
extern void NuHGobjEvalAnim(struct NUHGOBJ_s *hobj, struct nuanimdata_s *data, float time, s32 nJ,
                            struct NUJOINTANIM_s *pJ, struct numtx_s *tmtx);
extern void NuHGobjEval(struct NUHGOBJ_s *obj, s32 njoints, void *joints, struct numtx_s *tmtx);
extern float **NuHGobjEvalDwa2(s32 nlayers, short *layers, void *face_anim, f32 frame);
extern void *NuDatOpen(char *name, s32 mode, s32 arg);
extern void NuDatSet(void *dat);
extern void NuDatClose(void *dat);

/* The door's shot: until the frame the heroes come into sight (their visible curves step to 1 at 212), where they
   then walk in from. */
#define NW_SILHOUETTE_UNTIL 212.0f

bool nw_silhouette_drawing;

static const struct NUGCUTSCENE_s *nw_silhouette_def; /* control.cut, as it loaded */

/* The ones the picture shows, and how each idles. While out of sight the cut moves their joints any way it likes
   (Crash's tumble about), so they idle instead, each at its own phase. Crash and Coco have their idles from the game
   (CHARS.DAT, as game/creature's CrashAnim and CocoAnim list them), on the same skeletons as their cutscene models.
   Aku Aku's game model is another rig (its joints' axes and feathers' places differ from his cutscene model's), so he
   idles on the cut's own frames where he hovers in the room, there and back. Pura, crouched by them in the picture, is
   only in the room in the cut (from its frame 502, crouched by Crash): she stands in the door by Crash, turned to him
   as she is there, and idles on those frames. */
static struct {
    const char *name;
    const char *idle; /* the game's idle; NULL: the cut's frames from to to, there and back */
    float phase;      /* frames into it at the cut's start */
    float from, to;
    float place;      /* the frame the cut has it in sight, placed as it holds it then; 0: the door's shot's end */
    float beside;     /* not in the door: set by Crash's root there this far along its x, units; 0: in the door */
    struct nuanimdata_s *anim;
} nw_silhouette_heroes[] = {
    { "CSCrash_group", "chars\\a\\crash\\b\\idle.ani", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, NULL },
    { "CSCoco_group", "chars\\a\\coco\\a\\idle.ani", 11.0f, 0.0f, 0.0f, 0.0f, 0.0f, NULL },
    { "CSAku_group", NULL, 0.0f, 1002.0f, 1191.0f, 0.0f, 0.0f, NULL },
    { "BigPurra_group", NULL, 23.0f, 502.0f, 581.0f, 502.0f, 0.7f, NULL },
};

/* Crash's root this frame, which Pura is set by: in the door, and in the room at the frame she comes into sight. */
static struct {
    float frame; /* the cut's frame it is for; -1 none */
    struct numtx_s door, room;
} nw_silhouette_crash = { .frame = -1.0f };

void nw_silhouette_mark(struct nugscn_s *scene, const char *path)
{
    if (!scene || !path || SDL_strcasecmp(path, "levels\\b\\intro\\control.gsc") != 0) {
        return;
    }
    for (int i = 0; i < scene->numspecial; i++) {
        const struct nuspecial_s *s = &scene->specials[i];
        int objid = s->name && s->instance && SDL_strcasecmp(s->name, "silhouette") == 0 ? s->instance->objid : -1;
        const struct nugobj_s *gobj = objid >= 0 && objid < scene->numgobj ? scene->gobjs[objid] : NULL;
        for (struct nugeom_s *geom = gobj ? gobj->geom : NULL; geom; geom = geom->next) {
            NwGpuMesh *mesh = geom->dmastream;
            if (mesh && !geom->tristream) {
                mesh->heroes_picture = true;
            }
        }
    }
}

/* control.cut's idles go on the heap after it, so they last as long as it does. */
void nw_silhouette_cut(const char *path, const struct NUGCUTSCENE_s *def, union variptr_u *heap,
                       union variptr_u *heapend)
{
    if (!path || SDL_strcasecmp(path, "levels\\b\\intro2\\control.cut") != 0) {
        if (nw_silhouette_def == def) {
            nw_silhouette_def = NULL; /* another cutscene where it was */
        }
        return;
    }
    nw_silhouette_def = def;
    void *dat = NuDatOpen("chars.dat", 0, 0);
    NuDatSet(dat);
    for (size_t i = 0; i < SDL_arraysize(nw_silhouette_heroes); i++) {
        const char *idle = nw_silhouette_heroes[i].idle;
        nw_silhouette_heroes[i].anim = idle && heap && heapend ? NuAnimDataLoadBuff((char *)idle, heap, heapend) : NULL;
    }
    NuDatSet(0);
    if (dat) {
        NuDatClose(dat);
    }
}

static int nw_silhouette_hero(const struct NUGCUTCHAR_s *cutchar)
{
    for (size_t i = 0; cutchar->name && i < SDL_arraysize(nw_silhouette_heroes); i++) {
        if (SDL_strcasecmp(cutchar->name, nw_silhouette_heroes[i].name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* In the door's shot a hero draws where it stands until it comes into sight: placed as at the first frame it is,
   its root as the cut holds it there, idling on that root; its draws are
   marked for the renderer: black against the light with the silhouettes option, left out as retail without it. These
   heroes carry no locators. */
void AppCutSceneCharacterRender(struct instNUGCUTSCENE_s *icutscene, struct NUGCUTSCENE_s *cutscene,
                                struct instNUGCUTCHAR_s *icutchar, struct NUGCUTCHAR_s *cutchar, f32 current_frame)
{
    struct NUHGOBJ_s *obj = icutchar->data;
    int hero = nw_silhouette_def && cutscene == nw_silhouette_def && current_frame < NW_SILHOUETTE_UNTIL && obj
                   ? nw_silhouette_hero(cutchar)
                   : -1;
    if (hero < 0) {
        AppCutSceneCharacterRender__retail(icutscene, cutscene, icutchar, cutchar, current_frame);
        return;
    }
    static struct numtx_s tmtx[256];
    static struct numtx_s pose[256];
    short layertab[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    struct numtx_s mtx;
    int visible;
    float place = nw_silhouette_heroes[hero].place, beside = nw_silhouette_heroes[hero].beside;
    NuGCutCharAnimProcess(cutchar, place > 0.0f ? place : NW_SILHOUETTE_UNTIL, &mtx, &visible, 0, 0, 0);
    if (!visible || (beside != 0.0f && nw_silhouette_crash.frame != current_frame)) {
        return;
    }
    if (icutscene->flags & NUGCUTSCENE_MATRIXSET) {
        NuMtxMul(&mtx, &mtx, &icutscene->mtx);
    }
    if (cutchar->joint_anim) {
        NuHGobjEvalAnim2(obj, cutchar->joint_anim, current_frame, 0, 0, tmtx);
    } else {
        NuHGobjEval(obj, 0, 0, tmtx);
    }
    if (hero == 0 && cutchar->joint_anim) {
        /* Crash's roots for Pura, before his idle: drawn before her, as the cut lists him first. */
        nw_silhouette_crash.door = tmtx[0];
        NuHGobjEvalAnim2(obj, cutchar->joint_anim, nw_silhouette_heroes[3].place, 0, 0, pose);
        nw_silhouette_crash.room = pose[0];
        nw_silhouette_crash.frame = current_frame;
    }
    struct nuanimdata_s *idle = nw_silhouette_heroes[hero].anim;
    float from = nw_silhouette_heroes[hero].from, to = nw_silhouette_heroes[hero].to;
    float face = current_frame;
    if (idle && idle->time > 2.0f) {
        /* In the model's own space, set on the root the cut stands it on: both roots carry the hips' height, so the
           idle's own place at its first frame comes off first, its bobbing and lean left in. */
        NuHGobjEvalAnim(obj, idle, 1.0f, 0, NULL, pose);
        struct numtx_s base = tmtx[0];
        base._30 -= pose[0]._30 * base._00 + pose[0]._31 * base._10 + pose[0]._32 * base._20;
        base._31 -= pose[0]._30 * base._01 + pose[0]._31 * base._11 + pose[0]._32 * base._21;
        base._32 -= pose[0]._30 * base._02 + pose[0]._31 * base._12 + pose[0]._32 * base._22;
        float t = 1.0f + fmodf(current_frame - 1.0f + nw_silhouette_heroes[hero].phase, idle->time - 1.0f);
        NuHGobjEvalAnim(obj, idle, t, 0, NULL, pose);
        for (int j = 0; j < obj->num_joints; j++) {
            NuMtxMul(&tmtx[j], &pose[j], &base);
        }
    } else if (to > from && cutchar->joint_anim) {
        /* The cut's own frames, there and back, moved from where it has the root at the first of them onto the
           root it holds in the door (the hovering and the feathers kept, turned to face the way it faces there), or,
           for one only in the room, onto Crash's root in the door as it stands to him there, beside him. */
        struct numtx_s inv, base, at = tmtx[0];
        NuHGobjEvalAnim2(obj, cutchar->joint_anim, from, 0, 0, pose);
        NuMtxInv(&inv, &pose[0]);
        if (beside != 0.0f) {
            struct numtx_s crash, rel;
            NuMtxInv(&crash, &nw_silhouette_crash.room);
            NuMtxMul(&rel, &pose[0], &crash);
            rel._30 = beside;
            rel._32 = 0.0f;
            NuMtxMul(&at, &rel, &nw_silhouette_crash.door);
        }
        NuMtxMul(&base, &inv, &at);
        float span = to - from, u = fmodf(current_frame - 1.0f + nw_silhouette_heroes[hero].phase, 2.0f * span);
        face = from + (u < span ? u : 2.0f * span - u);
        NuHGobjEvalAnim2(obj, cutchar->joint_anim, face, 0, 0, pose);
        for (int j = 0; j < obj->num_joints; j++) {
            NuMtxMul(&tmtx[j], &pose[j], &base);
        }
    }
    float **dwa = cutchar->face_anim ? NuHGobjEvalDwa2(obj->num_layers, layertab, cutchar->face_anim, face) : NULL;
    nw_silhouette_drawing = true;
    NuHGobjRndrMtxDwa(obj, &mtx, obj->num_layers, layertab, tmtx, dwa);
    nw_silhouette_drawing = false;
}
