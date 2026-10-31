#include "gameplay/bosshealth.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/creature.h"
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
extern s32 PLAYERCOUNT;
extern struct gtimer_s GlobalTimer;
extern f32 NuTrigTable[];
extern u32 crunchtime_attack_phase;
extern f32 PANEL3DMULX, PANEL3DMULY;
extern f32 FIREBOSSBARX, FIREBOSSBARY, FIREBOSSBARZ, FIREBOSSBARSCALEX, FIREBOSSBARSCALEY;
extern f32 PANELCRUNCHX, PANELCRUNCHY, PANELCRUNCHZ, PANELCRUNCHSCALE;
extern u16 PANELCRUNCHXROT;

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
void AddSpacesIntoText(char *text, u32 flags);
void BossBar(f32 x, f32 y, f32 z, f32 xs, f32 ys, s32 health, s32 maxhealth);
s32 DrawPanel3DTempCharacter(u16 xrot, u16 yrot, u16 zrot, s32 rot, f32 x, f32 y, f32 z, f32 scale);

#define NW_BOSS_HEALTH_FRAME 0xB9   /* BossBar's frame and fill */
#define NW_BOSS_HEALTH_FILL 0xBB
#define NW_BOSS_HEALTH_PHASES 8     /* more is a bar without phases */
#define NW_BOSS_HEALTH_WIDTH 0.015f /* a line's width, of the bar's */
#define NW_BOSS_HEALTH_RGBA 0x10, 0x10, 0x10, 0x80
#define NW_BOSS_HEALTH_TEXT_Y 0.13f /* the percent over the bar */
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

static struct nugobj_s *nw_boss_health_gobj(int i)
{
    struct objtab_s *e = &ObjTab[i];
    return e->obj.scene && e->obj.special ? e->obj.scene->gobjs[e->obj.special->instance->objid] : NULL;
}

static void nw_boss_health_text(float x, float y, float z, float xs, int health, int maxhealth)
{
    struct nugobj_s *frame = nw_boss_health_gobj(NW_BOSS_HEALTH_FRAME);
    if (!frame) {
        return;
    }
    char text[16];
    nw_boss_health_percent(maxhealth > 0 ? health * 100 / maxhealth : 0, text);
    if (Game.language == 0x63) {
        AddSpacesIntoText(text, 1);
    }
    float mid = x + (frame->min.x + frame->max.x) * 0.5f * xs / PANEL3DMULX;
    Text3D(text, mid, y + NW_BOSS_HEALTH_TEXT_Y, z, NW_BOSS_HEALTH_TEXT_SCALE, NW_BOSS_HEALTH_TEXT_SCALE,
           NW_BOSS_HEALTH_TEXT_SCALE, 0, 0);
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
        nw_boss_health_text(x, y, z, xs, health, maxhealth);
    }
    int lines = nw_boss_health_lines(maxhealth);
    struct nugobj_s *gobj = nw_boss_health_gobj(NW_BOSS_HEALTH_FILL);
    if (!lines || !gobj) {
        return;
    }
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

int nw_boss_health_crunch_time_hp(unsigned phase)
{
    int hp = 4 - (int)phase;
    return hp < 0 ? 0 : hp > 4 ? 4 : hp;
}

/* The GameCube's DrawPanel branch for Level 0x19. */
void nw_boss_health_crunch_time(void)
{
    if (!nw_profile.boss_health || Level != 0x19 || !PLAYERCOUNT || !player || player->obj.finished) {
        return;
    }
    NW_DEVIATION(boss_health);
    BossBar(FIREBOSSBARX, FIREBOSSBARY, FIREBOSSBARZ, FIREBOSSBARSCALEX, FIREBOSSBARSCALEY,
            nw_boss_health_crunch_time_hp(crunchtime_attack_phase), 4);
    /* DrawPanel's wobble */
    short wobble_x = (short)(NuTrigTable[(((GlobalTimer.frame % 250) << 16) / 250) & 0xFFFF] * 2731.0f + 1820.0f);
    short wobble_y = (short)(NuTrigTable[((((GlobalTimer.frame % 200) << 16) / 200) + 0x4000) & 0xFFFF] * 2731.0f);
    DrawPanel3DTempCharacter((u16)(wobble_x + (s16)PANELCRUNCHXROT), (u16)(-wobble_y + 0x2000), 0, 0, PANELCRUNCHX,
                             PANELCRUNCHY - 0.1f, PANELCRUNCHZ, PANELCRUNCHSCALE);
}

void nw_boss_health_release(void)
{
    nw_boss_health_mtl = NULL;
}
