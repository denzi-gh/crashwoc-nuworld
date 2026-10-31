#include "gameplay/bosshealth.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "gfx/rndr.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"

/* Declared by no header. */
extern struct objtab_s ObjTab[201];
extern struct game_s Game;
extern s32 Level;
extern f32 PANEL3DMULX, PANEL3DMULY;

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
void AddSpacesIntoText(char *text, u32 flags);

#define NW_BOSS_HEALTH_FILL 0xBB    /* BossBar's fill; 0xB9 is its frame */
#define NW_BOSS_HEALTH_PHASES 8     /* more is a bar without phases */
#define NW_BOSS_HEALTH_WIDTH 0.015f /* a line's width, of the bar's */
#define NW_BOSS_HEALTH_RGBA 0x10, 0x10, 0x10, 0x80
#define NW_BOSS_HEALTH_TEXT_Y 0.13f /* the percent over the bar's right end */
#define NW_BOSS_HEALTH_TEXT_SCALE 0.6f

static struct numtl_s *nw_boss_health_mtl;

int nw_boss_health_lines(int maxhealth)
{
    return maxhealth >= 2 && maxhealth <= NW_BOSS_HEALTH_PHASES ? maxhealth - 1 : 0;
}

/* DrawPanel's glider health: three digits, the leading zeros blanked. */
void nw_boss_health_percent(int percent, char text[8])
{
    snprintf(text, 8, "%.3i%%", percent < 0 ? 0 : percent > 100 ? 100 : percent);
    if (text[0] == '0') {
        text[0] = ' ';
        if (text[1] == '0') {
            text[1] = ' ';
        }
    }
}

static void nw_boss_health_text(float x, float y, float z, int health, int maxhealth)
{
    char text[16];
    nw_boss_health_percent(maxhealth > 0 ? health * 100 / maxhealth : 0, text);
    if (Game.language == 0x63) {
        AddSpacesIntoText(text, 1);
    }
    Text3D(text, x, y + NW_BOSS_HEALTH_TEXT_Y, z, NW_BOSS_HEALTH_TEXT_SCALE, NW_BOSS_HEALTH_TEXT_SCALE,
           NW_BOSS_HEALTH_TEXT_SCALE, 8, 0);
}

static struct numtl_s *nw_boss_health_material(void)
{
    static uint8_t abgr[2 * 2 * 4];
    memset(abgr, 0xFF, sizeof(abgr));
    struct nutex_s tex = { 3, 2, 2, 1, abgr, NULL };
    struct numtl_s *mtl = NuMtlCreate(1);
    mtl->diffuse.r = 1.0f;
    mtl->diffuse.g = 1.0f;
    mtl->diffuse.b = 1.0f;
    mtl->alpha = 0.999f;
    mtl->attrib.cull = 2;
    mtl->attrib.zmode = 3;
    mtl->attrib.alpha = 1;
    mtl->attrib.lighting = 2;
    mtl->attrib.colour = 1;
    mtl->tid = NuTexCreate(&tex);
    NuMtlUpdate(mtl);
    return mtl;
}

void nw_boss_health_bar(float x, float y, float z, float xs, float ys, int health, int maxhealth)
{
    if (!nw_profile.boss_health) {
        return;
    }
    NW_DEVIATION(boss_health);
    if (Level == 0x18) {
        nw_boss_health_text(x, y, z, health, maxhealth);
    }
    int lines = nw_boss_health_lines(maxhealth);
    struct objtab_s *fill = &ObjTab[NW_BOSS_HEALTH_FILL];
    if (!lines || !fill->obj.scene || !fill->obj.special) {
        return;
    }
    struct nugobj_s *gobj = fill->obj.scene->gobjs[fill->obj.special->instance->objid];
    /* BossBar scales the fill towards its origin: a phase ends where the fill's far end is then. */
    float end = fabsf(gobj->max.x) >= fabsf(gobj->min.x) ? gobj->max.x : gobj->min.x;
    float half = fabsf(gobj->max.x - gobj->min.x) * NW_BOSS_HEALTH_WIDTH * 0.5f;
    if (!nw_boss_health_mtl) {
        nw_boss_health_mtl = nw_boss_health_material();
    }
    /* DrawPanel3DObject's matrix for the fill at full health. */
    struct nuvec_s s = { xs, ys, 0.1f };
    struct numtx_s m;
    NuMtxSetScale(&m, &s);
    m._30 = x * PANEL3DMULX;
    m._31 = y * PANEL3DMULY;
    m._32 = z;
    struct rndrstream_s *rs = &nw_boss_health_mtl->rs;
    NuRndrStreamAddMtl(rs, nw_boss_health_mtl);
    for (int k = 1; k <= lines; k++) {
        float at = end * (float)k / (float)maxhealth;
        struct nuvtx_tc1_s v[4];
        memset(v, 0, sizeof(v));
        for (int c = 0; c < 4; c++) {
            v[c].pnt.x = at + (c & 1 ? half : -half);
            v[c].pnt.y = c & 2 ? gobj->min.y : gobj->max.y;
            v[c].pnt.z = gobj->min.z;
            v[c].rgba = (struct nucolour_s){ NW_BOSS_HEALTH_RGBA };
            v[c].tc[0] = v[c].tc[1] = 0.5f;
        }
        nw_rndr_strip(v, 4, &m, rs);
    }
}

void nw_boss_health_release(void)
{
    nw_boss_health_mtl = NULL;
}
