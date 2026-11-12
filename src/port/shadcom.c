/* Native shadows. SHD entry points are compatibility handles only: no file, atlas or CLUT. */
#include <math.h>
#include <string.h>

#include "game/shadcom.h"
#include "gfx/rndr.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "numath/numtx.h"
#include "port/shadows.h"

extern u32 shadcol;

typedef struct NwShadowDraw {
    const NwRecord *record;
    struct NwShadowDraw *next;
} NwShadowDraw;

static struct {
    NwShadowDraw *first, *last;
    float origin[3], radius;
    bool active, character, locked, used;
} nw_caster;

static struct numtl_s *nw_shadow_mtl;
static struct shaddata_s nw_shadow_handle = { .external = 1 };
static struct shadentry_s nw_shadow_entry;

void nw_shadow_reset(void)
{
    memset(&nw_caster, 0, sizeof(nw_caster));
}

void nw_shadow_release(void)
{
    nw_shadow_mtl = NULL; /* NuMtlRelease freed it. */
    nw_shadow_reset();
}

void nw_shadow_character_begin(void)
{
    nw_shadow_reset();
    nw_caster.character = true;
}

void nw_shadow_character_end(void)
{
    nw_caster.character = nw_caster.locked = false;
}

bool nw_shadow_model_begin(const struct NUHGOBJ_s *obj, const struct numtx_s *world)
{
    if (nw_caster.locked) {
        return false; /* DrawCharacterModel's reflected copy. */
    }
    const float *m = &world->_00;
    float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (det < 0.0f && nw_caster.first && fabsf(world->_30 - nw_caster.origin[0]) < 1e-4f &&
        fabsf(world->_32 - nw_caster.origin[2]) < 1e-4f) {
        return false; /* The unwrapped vehicle paths also draw a reflected copy before ShadRndr. */
    }
    nw_caster.first = nw_caster.last = NULL;
    nw_caster.active = true;
    nw_caster.used = false;
    memcpy(nw_caster.origin, &world->_30, sizeof(nw_caster.origin));
    float extent = 0.0f, scale = 0.0f;
    for (int k = 0; k < 3; k++) {
        float e = (&obj->bbox_max.x)[k] - (&obj->bbox_min.x)[k];
        extent += e * e;
        const float *row = (&world->_00) + 4 * k;
        float s = row[0] * row[0] + row[1] * row[1] + row[2] * row[2];
        scale = fmaxf(scale, s);
    }
    nw_caster.radius = fmaxf(1.0f, sqrtf(extent * scale));
    return true;
}

void nw_shadow_model_end(bool captured)
{
    if (captured) {
        nw_caster.active = false;
        nw_caster.locked = nw_caster.character;
    }
}

void nw_shadow_record(NwCmd *cmd)
{
    if (!nw_caster.active) {
        return;
    }
    NwShadowDraw *d = nw_rndr_alloc(sizeof(*d));
    d->record = (const NwRecord *)cmd; /* cmd is NwRecord's first member. */
    d->next = NULL;
    if (nw_caster.last) {
        nw_caster.last->next = d;
    } else {
        nw_caster.first = d;
    }
    nw_caster.last = d;
}

struct shaddata_s *ShadDataLoad(char *file, union variptr_u *buffer)
{
    (void)file;
    (void)buffer;
    return &nw_shadow_handle;
}

/* Bypass InstShadDataLoad's file-existence check and instance pool as well. */
struct shaddata_s *InstShadDataLoad(char *file)
{
    return ShadDataLoad(file, NULL);
}

void ShadDataDestroy(struct shaddata_s *data) { (void)data; }
void InstShadDataDestroy(struct shaddata_s *data) { (void)data; }

struct shadentry_s *ShadFindData(struct shaddata_s *data, char *name)
{
    (void)name;
    return data ? &nw_shadow_entry : NULL;
}

void ShadComInit(void)
{
    nw_shadow_reset();
}

void ShadRndr(struct numtx_s *plane, struct shadentry_s *shad, f32 frame, f32 scale)
{
    (void)frame;
    if (!plane || !shad || !(scale > 0.0f) || !nw_caster.first || nw_caster.used) {
        return;
    }
    /* Never project a stale model under an unrelated special effect. */
    float dx = plane->_30 - nw_caster.origin[0], dz = plane->_32 - nw_caster.origin[2];
    float radius = nw_caster.radius + 1.0f;
    if (!isfinite(dx) || !isfinite(dz) || dx * dx + dz * dz > radius * radius) {
        return;
    }
    unsigned opacity = (shadcol >> 24) & 0xFF;
    if (!opacity) {
        return;
    }
    if (!nw_shadow_mtl) {
        nw_shadow_mtl = NuMtlCreate(1);
        nw_shadow_mtl->sort_pri = 100; /* after opaque geometry, before the landing indicator */
        nw_shadow_mtl->attrib.alpha = 1;
        nw_shadow_mtl->attrib.zmode = 1;
        nw_shadow_mtl->alpha = 0.25f;
        NuMtlUpdate(nw_shadow_mtl);
    }
    struct rndrstream_s *stream = &nw_shadow_mtl->rs;
    const NwRecord *source = nw_caster.first->record;
    NwRecord *begin = nw_rndr_alloc(sizeof(*begin));
    *begin = *source;
    begin->cmd.op = NW_CMD_SHADOW_BEGIN;
    begin->cmd.key = source->cmd.key ? source->cmd.key ^ UINT64_C(0x5348414442454749) : 0;
    begin->cmd.material = nw_shadow_mtl;
    begin->cmd.alpha = (uint16_t)(opacity > 127 ? 127 : opacity);
    memcpy(begin->cmd.local, plane, sizeof(begin->cmd.local));
    begin->joints = NULL;
    begin->blend = NULL;
    nw_rndr_push(stream, begin);
    for (NwShadowDraw *d = nw_caster.first; d; d = d->next) {
        NwRecord *copy = nw_rndr_alloc(sizeof(*copy));
        *copy = *d->record;
        copy->cmd.shadow = true;
        copy->cmd.key = copy->cmd.key ? copy->cmd.key ^ UINT64_C(0x5348414443415354) : 0;
        nw_rndr_push(stream, copy);
    }
    NwRecord *end = nw_rndr_alloc(sizeof(*end));
    *end = *begin;
    end->cmd.op = NW_CMD_SHADOW_END;
    end->cmd.key = 0;
    nw_rndr_push(stream, end);
    nw_caster.used = true;
}
