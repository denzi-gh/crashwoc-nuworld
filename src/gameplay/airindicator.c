#include "gameplay/airindicator.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "gfx/interp.h"
#include "gfx/rndr.h"
#include "nu3d/nugobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"

/* Declared by no header. */
extern s32 DRAWCREATURESHADOWS;
extern s32 Level;
extern s32 level_part_2;
extern s32 VEHICLECONTROL;
extern s32 plr_terrain_ok;
extern struct ldata_s *LDATA;
extern struct tersurface_s TerSurface[];

#define NW_NO_FLOOR 2000000.0f
#define NW_AIR_INDICATOR_LOW 0.1f  /* feet this high over the floor or lower: no ring */
#define NW_AIR_INDICATOR_HIGH 0.5f /* from here up the ring is at full alpha */
#define NW_AIR_INDICATOR_LIFT 0.035f /* over the floor; retail's shadow lies at 0.025 */
#define NW_AIR_INDICATOR_RADIUS 0.4f
#define NW_AIR_INDICATOR_TEX 64


/* Color and Alpha of the ring */
#define NW_AIR_INDICATOR_RGB 0x6C, 0x68, 0x60
#define NW_AIR_INDICATOR_ALPHA 0x46

static struct numtl_s *nw_air_indicator_mtl;
static struct numtx_s nw_air_indicator_mtx; 

/* Where retail's DrawCreatures draws Crash's shadow at all, but for its camera distance and never on a vehicle. */
static bool nw_air_indicator_shown(const struct creature_s *c)
{
    if (!DRAWCREATURESHADOWS || Level == 0x1D || Level == 0x24 || Level == 0x1A || (Level == 0x1E && level_part_2) ||
        (LDATA->flags & 0x1000) || VEHICLECONTROL == 2 || (VEHICLECONTROL == 1 && c->obj.vehicle != -1)) {
        return false;
    }
    return !c->freeze && !(c->obj.flags & 0x4000) && !(TerSurface[c->obj.surface_type].flags & 1);
}

bool nw_air_indicator_at(const struct creature_s *c, float *y, float *alpha)
{
    if (c->obj.ground || c->obj.dangle || c->obj.dead || !nw_air_indicator_shown(c)) {
        return false;
    }
    /* Over no floor GetSurfaceInfo leaves SAFEY in shadow, which can also be a real floor's height. */
    float floor = c->obj.shadow;
    if (floor == NW_NO_FLOOR || (!plr_terrain_ok && !c->obj.got_shadow)) {
        return false;
    }
    if (c->obj.layer_type != -1 && c->obj.layer_shadow != NW_NO_FLOOR && c->obj.layer_shadow > floor) {
        floor = c->obj.layer_shadow; /* water or lava over the floor */
    }
    float height = c->obj.pos.y + c->obj.bot * c->obj.SCALE - floor;
    if (height <= NW_AIR_INDICATOR_LOW) {
        return false;
    }
    *y = floor;
    *alpha = height >= NW_AIR_INDICATOR_HIGH
                 ? 1.0f
                 : (height - NW_AIR_INDICATOR_LOW) / (NW_AIR_INDICATOR_HIGH - NW_AIR_INDICATOR_LOW);
    return true;
}

static float nw_air_indicator_step(float e0, float e1, float x)
{
    float t = x <= e0 ? 0.0f : x >= e1 ? 1.0f : (x - e0) / (e1 - e0);
    return t * t * (3.0f - 2.0f * t);
}

/* Air indicator Ring material */
static struct numtl_s *nw_air_indicator_material(void)
{
    static uint8_t abgr[NW_AIR_INDICATOR_TEX * NW_AIR_INDICATOR_TEX * 4];
    for (int y = 0; y < NW_AIR_INDICATOR_TEX; y++) {
        for (int x = 0; x < NW_AIR_INDICATOR_TEX; x++) {
            float u = (x + 0.5f) / NW_AIR_INDICATOR_TEX * 2.0f - 1.0f;
            float v = (y + 0.5f) / NW_AIR_INDICATOR_TEX * 2.0f - 1.0f;
            float r = sqrtf(u * u + v * v);
            float a = nw_air_indicator_step(0.64f, 0.76f, r) * (1.0f - nw_air_indicator_step(0.86f, 0.98f, r));
            uint8_t *p = &abgr[(y * NW_AIR_INDICATOR_TEX + x) * 4]; /* type 3: a, b, g, r */
            p[0] = (uint8_t)(a * 255.0f + 0.5f);
            p[1] = p[2] = p[3] = 0xFF;
        }
    }
    struct nutex_s tex = { 3, NW_AIR_INDICATOR_TEX, NW_AIR_INDICATOR_TEX, 1, abgr, NULL };
    struct numtl_s *mtl = NuMtlCreate(1);
    mtl->diffuse.r = 1.0f;
    mtl->diffuse.g = 1.0f;
    mtl->diffuse.b = 1.0f;
    mtl->alpha = 0.999f;
    mtl->attrib.cull = 2;
    mtl->attrib.zmode = 1; /* tested, not written */
    mtl->attrib.alpha = 1;
    mtl->attrib.filter = 1;
    mtl->sort_pri = 101; /* over the shadows (100) */
    mtl->tid = NuTexCreate(&tex);
    NuMtlUpdate(mtl);
    return mtl;
}

void nw_air_indicator_draw(struct creature_s *c)
{
    static const float corner[4][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
    float y, alpha;
    if (!nw_profile.air_indicator) {
        return;
    }
    NW_DEVIATION(air_indicator);
    if (!nw_air_indicator_at(c, &y, &alpha)) {
        return;
    }
    if (!nw_air_indicator_mtl) {
        nw_air_indicator_mtl = nw_air_indicator_material();
    }
    struct nuvtx_tc1_s vtx[4];
    memset(vtx, 0, sizeof(vtx));
    for (int k = 0; k < 4; k++) {
        vtx[k].pnt.x = corner[k][0];
        vtx[k].pnt.z = corner[k][1];
        vtx[k].rgba = (struct nucolour_s){ NW_AIR_INDICATOR_RGB, (unsigned)(NW_AIR_INDICATOR_ALPHA * alpha + 0.5f) };
        vtx[k].tc[0] = (float)(k & 1);
        vtx[k].tc[1] = (float)(k >> 1);
    }
    /* Retail's shadow matrix (DrawCreatures' mS) at a fixed size. */
    struct numtx_s *m = &nw_air_indicator_mtx;
    struct nuvec_s s = { NW_AIR_INDICATOR_RADIUS, 1.0f, NW_AIR_INDICATOR_RADIUS };
    NuMtxSetScale(m, &s);
    NuMtxRotateZ(m, c->obj.surface_zrot);
    NuMtxRotateX(m, c->obj.surface_xrot);
    m->_30 = c->obj.pos.x;
    m->_31 = y + NW_AIR_INDICATOR_LIFT;
    m->_32 = c->obj.pos.z;
    struct rndrstream_s *rs = &nw_air_indicator_mtl->rs;
    NuRndrStreamAddMtl(rs, nw_air_indicator_mtl);
    nw_interp_key(m);
    nw_rndr_strip(vtx, 4, m, rs);
    nw_interp_key(NULL);
}

void nw_air_indicator_release(void)
{
    nw_air_indicator_mtl = NULL;
}
