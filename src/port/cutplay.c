/* Native CUT/ANI-E sampling services: a CUT camera or skeletal clip replayed on the host's clock. */
#include "cutplay.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "nu3d/nuanim.h"
#include "numath/numtx.h"

void NuHGobjEvalAnim2__retail(struct NUHGOBJ_s *, struct nuanimdata2_s *, float, int, struct NUJOINTANIM_s *, struct numtx_s *);
static NwCutPlayback *installed;

static bool fail(NwCutPlayback *p, const char *message)
{
    snprintf(p->error,sizeof(p->error),"%s",message); return SDL_SetError("cut playback: %s",message);
}

/* A borrowed input, or its default when the motion names none. */
static bool input(NwCutPlayback *p, const float *source, float fallback, float *value)
{
    *value = source ? *source : fallback;
    return isfinite(*value) || fail(p,"nonfinite motion input");
}

static bool joint_selected(const NwMotion *m, const char *name)
{
    for (size_t i = 0; name && i < m->joint_count; i++) {
        const char *pattern = m->joints[i]; size_t n = strlen(pattern);
        if (pattern[n-1] == '*' ? !strncmp(name,pattern,n-1) : !strcmp(name,pattern)) return true;
    }
    return false;
}

/* The CUT frame a run shows now: first, then rate frames a second from origin + delay. */
static bool frame_at(NwCutPlayback *p, const NwCutPlaybackRun *run, float *frame)
{
    const NwMotion *m = run->motion; float origin, delay;
    if (!input(p,m->origin,run->started,&origin) || !input(p,m->delay,0,&delay)) return false;
    float since = (run->seconds-origin)-delay;
    *frame = m->first + (since > 0 ? since*m->rate : 0.0f);
    return isfinite(*frame) || fail(p,"invalid motion time");
}

void nw_cutplay_init(NwCutPlayback *p, NwCutPlaybackHost host)
{
    nw_cutplay_destroy(p); p->host = host;
}
void nw_cutplay_destroy(NwCutPlayback *p)
{
    if (installed == p) installed = NULL;
    free(p->clips); free(p->pose); memset(p,0,sizeof(*p));
}
void nw_cutplay_install(NwCutPlayback *p) { installed = p; }

bool nw_cutplay_play(NwCutPlayback *p, const NwMotion *m, float started)
{
    float unused;
    if (!m || !input(p,m->origin,0,&unused) || !input(p,m->delay,0,&unused) || !input(p,m->enabled,1,&unused)) return false;
    NwCutPlaybackRun run = {.motion=m,.started=started,.seconds=started};
    if (!m->skeletal) { p->camera = run; p->camera_valid = false; return true; }
    size_t i = 0;
    while (i < p->clip_count && p->clips[i].motion != m) i++;
    if (i == p->clip_count) {
        if (i == p->clip_capacity) {
            size_t cap = p->clip_capacity ? p->clip_capacity*2 : 4;
            NwCutPlaybackRun *more = cap <= SIZE_MAX/sizeof(*more) ? realloc(p->clips,cap*sizeof(*more)) : NULL;
            if (!more) return fail(p,"cannot allocate clip runs");
            p->clips = more; p->clip_capacity = cap;
        }
        p->clip_count++;
    }
    p->clips[i] = run; return true;
}

void nw_cutplay_clear(NwCutPlayback *p)
{
    memset(&p->camera,0,sizeof(p->camera)); p->camera_valid = false; p->clip_count = 0;
}

bool nw_cutplay_camera_sample(struct instNUGCUTSCENE_s *scene, int index, float frame, struct numtx_s *out)
{
    if (!scene || !scene->def || !out || !isfinite(frame) || frame < 1 || frame > scene->def->duration)
        return SDL_SetError("invalid CUT camera sample");
    struct NUGCUTCAMSYS_s *def = scene->def->camdef;
    if (index == -1 && scene->camsys) index = scene->camsys->current;
    if (!def || !def->cutcams || index < 0 || (unsigned)index >= def->ncutcams) return SDL_SetError("missing CUT camera");
    /* Target aiming is stateful in the legacy player; never silently discard it. */
    if (scene->camsys && scene->camsys->count) return SDL_SetError("targeted CUT cameras require the legacy camera service");
    struct NUGCUTCAM_s *camera = &def->cutcams[index];
    if (camera->flags & 1u) { /* NUGCUTCAM_ANIMATED: private enum in the legacy implementation. */
        struct nuanimdata2_s *anim = def->camanim; struct nuanimtime_s at;
        if (!anim || camera->anim_ix >= anim->nnodes || frame > anim->time) return SDL_SetError("invalid CUT camera animation/sample");
        NuAnimData2CalcTime(anim,frame,&at);
        NuAnimCurve2SetApplyToMatrix(&anim->curves[anim->ncurves*camera->anim_ix],
            &anim->curveflags[anim->ncurves*camera->anim_ix],anim->curvesetflags[camera->anim_ix],&at,out);
    } else *out = camera->mtx;
    if (scene->flags & NUGCUTSCENE_MATRIXSET) NuMtxMul(out,out,&scene->mtx);
    return true;
}

bool nw_cutplay_update(NwCutPlayback *p, float seconds)
{
    for (size_t i = 0; i < p->clip_count; i++) p->clips[i].seconds = seconds;
    if (!p->camera.motion) return true;
    p->camera.seconds = seconds; float frame, enabled;
    if (!frame_at(p,&p->camera,&frame) || !input(p,p->camera.motion->enabled,1,&enabled)) return false;
    p->camera_valid = false;
    struct instNUGCUTSCENE_s *source = p->host.scene ? p->host.scene(p->host.context,p->camera.motion->source) : NULL;
    if (source && enabled != 0) {
        const NwMotion *m = p->camera.motion;
        if (frame > m->last) frame = m->last;
        if (!nw_cutplay_camera_sample(source,m->camera,frame,&p->camera_pose)) {
            char message[256]; SDL_strlcpy(message,SDL_GetError(),sizeof(message)); return fail(p,message);
        }
        p->camera_valid = true;
    }
    return true;
}

bool nw_cutplay_camera(const NwCutPlayback *p, struct numtx_s *out)
{
    if (!p || !p->camera_valid) return false;
    *out = p->camera_pose; return true;
}

bool nw_cutplay_apply(NwCutPlayback *p, struct NUHGOBJ_s *model, float source_frame, struct numtx_s *matrices)
{
    if (!model || !model->joints || model->num_joints <= 0 || !matrices) return true;
    for (size_t i = 0; i < p->clip_count; i++) {
        NwCutPlaybackRun *run = &p->clips[i]; const NwMotion *m = run->motion;
        struct instNUGCUTSCENE_s *scene = p->host.scene ? p->host.scene(p->host.context,m->source) : NULL;
        if (!scene || !scene->def || !scene->def->chardef) continue; /* Assets can be loaded after the plan. */
        struct NUGCUTCHARSYS_s *chars = scene->def->chardef; struct NUGCUTCHAR_s *character = NULL;
        for (unsigned j = 0; j < chars->count; j++) if (chars->chars[j].name && !strcmp(chars->chars[j].name,m->character)) { character = &chars->chars[j]; break; }
        if (!character) return fail(p,"missing CUT character for clip");
        if (character->character != model) continue;
        if (!character->joint_anim) return fail(p,"missing skeletal clip animation");
        if (character->joint_anim->nnodes < model->num_joints || m->last > character->joint_anim->time)
            return fail(p,"clip range/skeleton exceeds the source animation");
        float enabled, frame;
        if (!input(p,m->enabled,1,&enabled) || !frame_at(p,run,&frame)) return false;
        if (enabled == 0 || frame >= m->last) continue; /* Release to the underlying animation at the end. */
        if (m->before_source && p->host.current && p->host.scene &&
            p->host.current(p->host.context) == p->host.scene(p->host.context,m->before_source) && source_frame <= m->before) continue;
        size_t count = (size_t)model->num_joints;
        if (count > p->pose_capacity) {
            if (count > SIZE_MAX/sizeof(*p->pose)) return fail(p,"pose too large");
            struct numtx_s *more = realloc(p->pose,count*sizeof(*more));
            if (!more) return fail(p,"cannot allocate clip pose");
            p->pose = more; p->pose_capacity = count;
        }
        NuHGobjEvalAnim2__retail(model,character->joint_anim,frame,0,NULL,p->pose);
        struct numtx_s carry, inv;
        if (m->preserve_root) {
            if (!p->host.multiply || !p->host.inverse) return fail(p,"preserve_root requires host matrix operations");
            p->host.inverse(&inv,&p->pose[0]); p->host.multiply(&carry,&inv,&matrices[0]);
        }
        for (size_t j = 0; j < count; j++) if (joint_selected(m,model->joints[j].name)) {
            if (m->preserve_root) p->host.multiply(&matrices[j],&p->pose[j],&carry); else matrices[j] = p->pose[j];
        }
    }
    return true;
}

void NuHGobjEvalAnim2(struct NUHGOBJ_s *model, struct nuanimdata2_s *animation, float frame, int count,
                    struct NUJOINTANIM_s *joints, struct numtx_s *pose)
{
    NuHGobjEvalAnim2__retail(model,animation,frame,count,joints,pose);
    NwCutPlayback *p = installed;
    if (!p || (p->host.active && !p->host.active(p->host.context))) return;
    if (p->host.prepare) p->host.prepare(p->host.context);
    if (!nw_cutplay_apply(p,model,frame,pose)) SDL_Log("cut playback: %s",p->error);
}
