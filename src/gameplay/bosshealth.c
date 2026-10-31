#include "gameplay/bosshealth.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "gfx/rndr.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu_port.h"
#include "numath/numtx.h"
#include "numath/nuvec.h"

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
int NuHGobjRndrMtxDwa(struct NUHGOBJ_s *hobj, struct numtx_s *wm, int nlayers, short *layer, struct numtx_s *mtx_array,
                      float **dwa);
void NuHGobjEval(struct NUHGOBJ_s *hobj, int njanims, struct NUJOINTANIM_s *janim, struct numtx_s *mtx_array);

#define NW_BOSS_HEALTH_FRAME 0xB9   /* BossBar's frame and fill */
#define NW_BOSS_HEALTH_FILL 0xBB
#define NW_BOSS_HEALTH_PHASES 8     /* more is a bar without phases */
#define NW_BOSS_HEALTH_WIDTH 0.015f /* a line's width, of the bar's */
#define NW_BOSS_HEALTH_RGBA 0x10, 0x10, 0x10, 0x80
#define NW_BOSS_HEALTH_TEXT_Y 0.13f /* the percent over the bar */
#define NW_BOSS_HEALTH_TEXT_SCALE 0.6f
#define NW_BOSS_HEALTH_CORTEX 2 /* the GameCube's panel character in Crunch Time, in its idle */
#define NW_BOSS_HEALTH_IDLE 0x22
#define NW_BOSS_HEALTH_CRUNCH_HEAD 0xBC /* the panel's Crunch head */
#define NW_BOSS_HEALTH_FIRE_BOSS 0x9C
#define NW_BOSS_HEALTH_WEATHER_BOSS 0x86
#define NW_BOSS_HEALTH_FIRE_PITCH 0x1400 /* the Fire boss's idle looks down */
#define NW_BOSS_HEALTH_BAR_SHIFT -0.12f /* the Fire and Air bars, clear of the bigger boss heads */

static struct numtl_s *nw_boss_health_mtl;
static AnimPacket nw_boss_health_anim; /* Cortex's, then the boss head's */
static bool nw_boss_health_anim_set;
static struct numtx_s nw_boss_health_tmtx[0x100];

float nw_boss_health_bar_x(float x)
{
    if (!nw_profile.boss_health || (Level != 0x16 && Level != 0x18)) {
        return x;
    }
    NW_DEVIATION(boss_health);
    return x + NW_BOSS_HEALTH_BAR_SHIFT;
}

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

/* InitObjectTable's find for an entry whose levbits leave out this level. */
static void nw_boss_health_find(int i)
{
    struct objtab_s *e = &ObjTab[i];
    if (!e->obj.special && *e->scene && NuSpecialFind(*e->scene, &e->obj, e->name)) {
        e->obj.special->instance->flags.visible = e->visible;
    }
}

/* The panel character's idle, stepped as the game thread steps TempAnim. */
static void nw_boss_health_animate(struct CharacterModel *model)
{
    if (!nw_boss_health_anim_set) {
        ResetAnimPacket(&nw_boss_health_anim, NW_BOSS_HEALTH_IDLE);
        nw_boss_health_anim_set = true;
    }
    nw_boss_health_anim.oldaction = nw_boss_health_anim.action;
    if ((u16)nw_boss_health_anim.action < 0x76 && (u16)nw_boss_health_anim.newaction < 0x76) {
        UpdateAnimPacket(model, &nw_boss_health_anim, NW_HZ(0.59999996f, 0.5f), 0.0f);
    }
}

/* DrawPanel3DTempCharacter's matrix (rot 0) without its translation. */
static void nw_boss_health_turn(struct numtx_s *m, float scale, u16 xrot, u16 yrot)
{
    struct nuvec_s s = { scale, scale, scale };
    NuMtxSetScale(m, &s);
    if (xrot != 0) {
        NuMtxRotateX(m, xrot);
    }
    if (yrot != 0) {
        NuMtxRotateY(m, yrot);
    }
}

/* DrawPanel3DTempCharacter for the GameCube's Crunch Time character: PS2's temp_character stays unset there. */
static void nw_boss_health_cortex(u16 xrot, u16 yrot, float x, float y, float z, float scale)
{
    s32 i = CRemap[NW_BOSS_HEALTH_CORTEX];
    if (i == -1) {
        return;
    }
    struct CharacterModel *model = &CModel[i];
    nw_boss_health_animate(model);
    struct numtx_s m;
    float **dwa;
    nw_boss_health_turn(&m, scale, xrot, yrot);
    m._32 = z;
    m._30 = x * PANEL3DMULX;
    m._31 = y * PANEL3DMULY;
    EvalModelAnim(model, &nw_boss_health_anim, &m, nw_boss_health_tmtx, &dwa, NULL);
    NuHGobjRndrMtxDwa(model->obj, &m, 1, NULL, nw_boss_health_tmtx, dwa);
}

/* A joint by the last part of its name ("head|rightEar" is "rightEar"), -1 for none. */
static int nw_boss_health_joint(const struct NUHGOBJ_s *o, const char *name)
{
    for (int j = 0; j < o->num_joints; j++) {
        const char *n = o->joints[j].name;
        const char *bar = n ? strrchr(n, '|') : NULL;
        if (n && strcmp(bar ? bar + 1 : n, name) == 0) {
            return j;
        }
    }
    return -1;
}

static struct nuvec_s nw_boss_health_at(const struct numtx_s *m)
{
    return (struct nuvec_s){ m->_30, m->_31, m->_32 };
}

static float nw_boss_health_span(const struct numtx_s *a, const struct numtx_s *b)
{
    float dx = a->_30 - b->_30, dy = a->_31 - b->_31, dz = a->_32 - b->_32;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* The head joint, its ear span and their positions in tmtx; false without them. */
static bool nw_boss_health_ears(const struct NUHGOBJ_s *o, const struct numtx_s *tmtx, int *head, struct nuvec_s *at,
                                float *span)
{
    int h = nw_boss_health_joint(o, "head"), l = nw_boss_health_joint(o, "leftEar");
    int r = nw_boss_health_joint(o, "rightEar");
    if (h < 0 || l < 0 || r < 0) {
        return false;
    }
    *head = h;
    *at = nw_boss_health_at(&tmtx[h]);
    *span = nw_boss_health_span(&tmtx[l], &tmtx[r]);
    return *span > 0.0f;
}

static bool nw_boss_health_under(const struct NUHGOBJ_s *o, int j, int head)
{
    for (; j >= 0; j = o->joints[j].parent_ix) {
        if (j == head) {
            return true;
        }
    }
    return false;
}

bool nw_boss_health_head(u16 xrot, u16 yrot, int rot, float x, float y, float z, float scale)
{
    if (!nw_profile.boss_health || rot != 0 || (Level != 0x16 && Level != 0x18)) {
        return false;
    }
    NW_DEVIATION(boss_health);
    if (Level == 0x16) {
        xrot = (u16)(xrot + NW_BOSS_HEALTH_FIRE_PITCH);
    }
    s32 b = CRemap[Level == 0x16 ? NW_BOSS_HEALTH_FIRE_BOSS : NW_BOSS_HEALTH_WEATHER_BOSS];
    s32 c = CRemap[NW_BOSS_HEALTH_CRUNCH_HEAD];
    if (b == -1 || c == -1) {
        return false;
    }
    struct NUHGOBJ_s *boss = CModel[b].obj, *crunch = CModel[c].obj;
    struct numtx_s *tmtx = nw_boss_health_tmtx;
    struct nuvec_s crunch_at, boss_at;
    float crunch_span, boss_span;
    int crunch_head, boss_head;
    NuHGobjEval(crunch, 0, NULL, tmtx);
    if (!nw_boss_health_ears(crunch, tmtx, &crunch_head, &crunch_at, &crunch_span)) {
        return false;
    }
    nw_boss_health_animate(&CModel[b]);
    struct numtx_s m;
    float **dwa;
    NuMtxSetIdentity(&m);
    EvalModelAnim(&CModel[b], &nw_boss_health_anim, &m, tmtx, &dwa, NULL);
    if (!nw_boss_health_ears(boss, tmtx, &boss_head, &boss_at, &boss_span)) {
        return false;
    }
    /* Everything but the head folds into it: the body has no size left to draw. */
    for (int j = 0; j < boss->num_joints; j++) {
        if (!nw_boss_health_under(boss, j, boss_head)) {
            memset(&tmtx[j], 0, sizeof(tmtx[j]));
            tmtx[j]._30 = boss_at.x;
            tmtx[j]._31 = boss_at.y;
            tmtx[j]._32 = boss_at.z;
            tmtx[j]._33 = 1.0f;
        }
    }
    /* The boss's head as big as the Crunch head and where its head joint would be. */
    float boss_scale = scale * crunch_span / boss_span;
    struct numtx_s turn;
    nw_boss_health_turn(&turn, 1.0f, xrot, yrot);
    struct nuvec_s d = { crunch_at.x * scale - boss_at.x * boss_scale, crunch_at.y * scale - boss_at.y * boss_scale,
                         crunch_at.z * scale - boss_at.z * boss_scale };
    NuVecMtxTransform(&d, &d, &turn);
    nw_boss_health_turn(&m, boss_scale, xrot, yrot);
    m._30 = x * PANEL3DMULX + d.x;
    m._31 = y * PANEL3DMULY + d.y;
    m._32 = z + d.z;
    NuHGobjRndrMtxDwa(boss, &m, 1, NULL, tmtx, dwa);
    return true;
}

/* The GameCube's DrawPanel branch for Level 0x19. */
void nw_boss_health_crunch_time(void)
{
    if (!nw_profile.boss_health || Level != 0x19 || !PLAYERCOUNT || !player || player->obj.finished) {
        return;
    }
    NW_DEVIATION(boss_health);
    nw_boss_health_find(NW_BOSS_HEALTH_FRAME);
    nw_boss_health_find(NW_BOSS_HEALTH_FILL);
    BossBar(FIREBOSSBARX, FIREBOSSBARY, FIREBOSSBARZ, FIREBOSSBARSCALEX, FIREBOSSBARSCALEY,
            nw_boss_health_crunch_time_hp(crunchtime_attack_phase), 4);
    /* DrawPanel's wobble */
    short wobble_x = (short)(NuTrigTable[(((GlobalTimer.frame % 250) << 16) / 250) & 0xFFFF] * 2731.0f + 1820.0f);
    short wobble_y = (short)(NuTrigTable[((((GlobalTimer.frame % 200) << 16) / 200) + 0x4000) & 0xFFFF] * 2731.0f);
    nw_boss_health_cortex((u16)(wobble_x + (s16)PANELCRUNCHXROT), (u16)(-wobble_y + 0x2000), PANELCRUNCHX,
                          PANELCRUNCHY - 0.1f, PANELCRUNCHZ, PANELCRUNCHSCALE);
}

void nw_boss_health_release(void)
{
    nw_boss_health_mtl = NULL;
    nw_boss_health_anim_set = false;
}
