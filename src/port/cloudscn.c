#include "port/cloudscn.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamelib/gcutscn.h"
#include "gfx/packet.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"

extern int cutworldix;
extern struct instNUGCUTSCENE_s *CutInst[];

#define NW_CLOUDSCN_WORLDS 32 /* CutInst's */

/* The cards, their names as the name table holds them (lowered as it loads), and each one's place in the noise. */
static const struct {
    const char *name;
    float seed;
} nw_cloudscn_cards[] = {
    { "cloud1", 0.0f },
    { "scloud", 0.37f },
};

/* The sky domes and their weather: sunny under surf.cut and sunny.cut, stormy under storm.cut and washedup.cut. */
static const struct {
    const char *name;
    float weather;
} nw_cloudscn_skies[] = {
    { "sky_sunny", 0.0f },
    { "sky_stormy", 1.0f },
};

/* The storm's cutscenes as they load, the definitions their instances run. */
static const char *const nw_cloudscn_cuts[2] = { "levels\\b\\intro2\\sunny.cut", "levels\\b\\intro2\\storm.cut" };
static const struct NUGCUTSCENE_s *nw_cloudscn_defs[2];

static const struct nuspecial_s *nw_cloudscn_special(const struct nugscn_s *scene, const char *name)
{
    for (int i = 0; i < scene->numspecial; i++) {
        const struct nuspecial_s *s = &scene->specials[i];
        if (s->name && s->instance && strcmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

/* The special's object, NULL when it has none. */
static const struct nugobj_s *nw_cloudscn_gobj(const struct nugscn_s *scene, const struct nuspecial_s *s)
{
    int objid = s ? s->instance->objid : -1;
    return objid >= 0 && objid < scene->numgobj ? scene->gobjs[objid] : NULL;
}

static void nw_cloudscn_set(const struct nugobj_s *gobj, const NwGpuCloud *cloud)
{
    for (struct nugeom_s *geom = gobj->geom; geom; geom = geom->next) {
        NwGpuMesh *mesh = geom->dmastream;
        if (mesh && !geom->tristream) {
            mesh->cloud = *cloud;
        }
    }
}

void nw_cloudscn_mark(struct nugscn_s *scene, const char *path)
{
    if (!scene || !path || SDL_strcasecmp(path, "levels\\b\\intro\\volcano.gsc") != 0) {
        return;
    }
    NwGpuCloud base = { 0 };
    const struct nuspecial_s *sun = nw_cloudscn_special(scene, "sun");
    if (sun) {
        base.sun[0] = sun->instance->mtx._30;
        base.sun[1] = sun->instance->mtx._31;
        base.sun[2] = sun->instance->mtx._32;
        base.sun_ok = true;
    }
    for (size_t i = 0; i < SDL_arraysize(nw_cloudscn_cards); i++) {
        const struct nuspecial_s *s = nw_cloudscn_special(scene, nw_cloudscn_cards[i].name);
        const struct nugobj_s *gobj = nw_cloudscn_gobj(scene, s);
        if (!gobj) {
            continue;
        }
        const struct numtx_s *m = &s->instance->mtx;
        /* The card's own scale makes the cloud's space: there the noise is round in the world as the scene places
           it, and the cutscene's growth stretches it from there. */
        float shape[3] = { sqrtf(m->_00 * m->_00 + m->_01 * m->_01 + m->_02 * m->_02),
                           sqrtf(m->_10 * m->_10 + m->_11 * m->_11 + m->_12 * m->_12),
                           sqrtf(m->_20 * m->_20 + m->_21 * m->_21 + m->_22 * m->_22) };
        if (!(shape[0] > 0.0f && shape[1] > 0.0f && shape[2] > 0.0f)) {
            continue;
        }
        /* A card is flat in z: the volume is as deep, in the cloud's space, as 0.9 of its height. */
        float depth = 0.9f * (gobj->max.y - gobj->min.y) * 0.5f * shape[1] / shape[2];
        NwGpuCloud cloud = base;
        cloud.kind = NW_CLOUD_PUFF;
        cloud.min[0] = gobj->min.x, cloud.min[1] = gobj->min.y, cloud.min[2] = -depth;
        cloud.max[0] = gobj->max.x, cloud.max[1] = gobj->max.y, cloud.max[2] = depth;
        memcpy(cloud.shape, shape, sizeof(shape));
        cloud.seed = nw_cloudscn_cards[i].seed;
        nw_cloudscn_set(gobj, &cloud);
    }
    /* The storm gathers first where Cloud1 grows. */
    const struct nuspecial_s *front = nw_cloudscn_special(scene, "cloud1");
    for (size_t i = 0; i < SDL_arraysize(nw_cloudscn_skies) && front; i++) {
        const struct nugobj_s *gobj = nw_cloudscn_gobj(scene, nw_cloudscn_special(scene, nw_cloudscn_skies[i].name));
        if (gobj) {
            NwGpuCloud sky = base;
            sky.kind = NW_CLOUD_SKY;
            sky.front[0] = front->instance->mtx._30;
            sky.front[1] = front->instance->mtx._31;
            sky.front[2] = front->instance->mtx._32;
            sky.weather = nw_cloudscn_skies[i].weather;
            nw_cloudscn_set(gobj, &sky);
        }
    }
    const struct nugobj_s *card = nw_cloudscn_gobj(scene, sun);
    if (card && front) {
        NwGpuCloud c = base;
        c.kind = NW_CLOUD_SUN;
        c.min[0] = card->min.x, c.min[1] = card->min.y, c.min[2] = card->min.z;
        c.max[0] = card->max.x, c.max[1] = card->max.y, c.max[2] = card->max.z;
        nw_cloudscn_set(card, &c);
    }
}

void nw_cloudscn_cut(const char *path, const struct NUGCUTSCENE_s *def)
{
    for (int i = 0; i < 2; i++) {
        if (path && SDL_strcasecmp(path, nw_cloudscn_cuts[i]) == 0) {
            nw_cloudscn_defs[i] = def;
        } else if (nw_cloudscn_defs[i] == def) {
            nw_cloudscn_defs[i] = NULL; /* another cutscene where it was */
        }
    }
}

/* From v0 at t0 to v1 at t1, eased in and out. */
static float nw_cloudscn_ramp(float t, float t0, float t1, float v0, float v1)
{
    float x = t <= t0 ? 0.0f : t >= t1 ? 1.0f : (t - t0) / (t1 - t0);
    return v0 + (v1 - v0) * x * x * (3.0f - 2.0f * x);
}

/* sunny.cut: clear while Aku Aku talks; from Cloud1's shot (212-448) the storm gathers in it as it grows, slowly at
   first, and the sky starts to fill around it; over the sun's shot (449-501), short, the front comes across, faster
   and faster, and swallows the sun by its end. storm.cut closes the last gap. */
bool nw_cloudscn_storm(float *storm)
{
    const struct instNUGCUTSCENE_s *cut =
        cutworldix >= 0 && cutworldix < NW_CLOUDSCN_WORLDS ? CutInst[cutworldix] : NULL;
    if (!cut || !cut->def) {
        return false;
    }
    float t = cut->time;
    if (cut->def == nw_cloudscn_defs[0]) {
        *storm = t < 448.0f ? 0.5f * powf(SDL_clamp((t - 212.0f) / 236.0f, 0.0f, 1.0f), 1.6f)
                            : 0.5f + 0.42f * powf(SDL_clamp((t - 448.0f) / 53.0f, 0.0f, 1.0f), 2.0f);
        return true;
    }
    if (cut->def == nw_cloudscn_defs[1]) {
        *storm = nw_cloudscn_ramp(t, 0.0f, 40.0f, 0.92f, 1.0f);
        return true;
    }
    return false;
}
