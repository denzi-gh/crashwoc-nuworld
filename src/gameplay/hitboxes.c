#include "gameplay/hitboxes.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "gameplay/hazards.h"
#include "gfx/interp.h"
#include "gfx/rndr.h"
#include "nu3d/nugobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"
#include "numath/nuvec.h"

/* game.c's own struct kaboom_s. */
typedef struct NwKaboom {
    u16 type;
    short crate;
    struct nuvec_s pos;
    float t0, t1, time, duration;
    short group;
    char pad[2];
} NwKaboom;
_Static_assert(sizeof(NwKaboom) == 0x24, "game.c's struct kaboom_s");

/* Declared by no header. */
extern struct obj_s *pObj[64];
extern s32 GAMEOBJECTCOUNT;
extern struct chase_s Chase[3];
extern struct crategroup_s CrateGroup[];
extern struct cratecube_s Crate[];
extern s32 CRATEGROUPCOUNT;
extern struct wumpa_s Wumpa[320];
extern NwKaboom Kaboom[72];
extern struct gtimer_s GameTimer;

#define NW_HITBOXES_PLAYER 0x40, 0xE0, 0x40
#define NW_HITBOXES_HARM 0xF0, 0x30, 0x30  /* can hurt the player */
#define NW_HITBOXES_OTHER 0xF0, 0xD0, 0x30
#define NW_HITBOXES_CHASER 0xF0, 0x80, 0x20
#define NW_HITBOXES_CRATE 0x40, 0x90, 0xF0
#define NW_HITBOXES_SHOT 0xF0, 0xF0, 0xF0 /* shots, explosions, particles */
#define NW_HITBOXES_SIDE 0x28
#define NW_HITBOXES_RIM 0x70
#define NW_HITBOXES_SEGMENTS 16
#define NW_HITBOXES_CRATES 24.0f /* crates this near the player */
#define NW_HITBOXES_PARTICLES 256

static struct numtl_s *nw_hitboxes_mtl;
static struct numtx_s nw_hitboxes_mtx;
static struct {
    struct nuvec_s pos;
    float radius;
} nw_hitboxes_particles[NW_HITBOXES_PARTICLES];
static int nw_hitboxes_nparticles;
static u32 nw_hitboxes_chased[3]; /* GameTimer's frame + 1 at its last UpdateChase */

void nw_hitboxes_chase(const struct chase_s *chase)
{
    if (nw_profile.hitboxes && chase >= Chase && chase < Chase + 3) {
        nw_hitboxes_chased[chase - Chase] = GameTimer.frame + 1;
    }
}

void nw_hitboxes_particle(const struct nuvec_s *pos, float radius)
{
    if (nw_profile.hitboxes && nw_hitboxes_nparticles < NW_HITBOXES_PARTICLES) {
        nw_hitboxes_particles[nw_hitboxes_nparticles].pos = *pos;
        nw_hitboxes_particles[nw_hitboxes_nparticles].radius = radius;
        nw_hitboxes_nparticles++;
    }
}

static struct numtl_s *nw_hitboxes_material(void)
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
    mtl->attrib.zmode = 1; /* tested, not written */
    mtl->attrib.alpha = 1;
    mtl->sort_pri = 102;
    mtl->tid = NuTexCreate(&tex);
    NuMtlUpdate(mtl);
    return mtl;
}

static void nw_hitboxes_vertex(struct nuvtx_tc1_s *v, float x, float y, float z, struct nucolour_s c)
{
    memset(v, 0, sizeof(*v));
    v->pnt = (struct nuvec_s){ x, y, z };
    v->rgba = c;
    v->tc[0] = v->tc[1] = 0.5f;
}

static void nw_hitboxes_strip(struct nuvtx_tc1_s *v, int n, const void *key)
{
    nw_interp_key(key);
    nw_rndr_strip(v, n, &nw_hitboxes_mtx, &nw_hitboxes_mtl->rs);
}

/* Upright, from bot to top (world y). */
static void nw_hitboxes_cylinder(const void *key, const struct nuvec_s *at, float radius, float bot, float top,
                                 struct nucolour_s c)
{
    struct nuvtx_tc1_s side[2 * (NW_HITBOXES_SEGMENTS + 1)], rim[2 * (NW_HITBOXES_SEGMENTS + 1)];
    if (!(radius > 0.0f) || !(top > bot)) {
        return;
    }
    struct nucolour_s faint = c, strong = c;
    faint.a = NW_HITBOXES_SIDE;
    strong.a = NW_HITBOXES_RIM;
    for (int i = 0; i <= NW_HITBOXES_SEGMENTS; i++) {
        float a = i * 6.2831853f / NW_HITBOXES_SEGMENTS, x = cosf(a), z = sinf(a);
        nw_hitboxes_vertex(&side[2 * i], x, 0.0f, z, faint);
        nw_hitboxes_vertex(&side[2 * i + 1], x, 1.0f, z, faint);
        nw_hitboxes_vertex(&rim[2 * i], x, 1.0f, z, strong);
        nw_hitboxes_vertex(&rim[2 * i + 1], x * 0.9f, 1.0f, z * 0.9f, strong);
    }
    struct nuvec_s s = { radius, top - bot, radius };
    NuMtxSetScale(&nw_hitboxes_mtx, &s);
    nw_hitboxes_mtx._30 = at->x;
    nw_hitboxes_mtx._31 = bot;
    nw_hitboxes_mtx._32 = at->z;
    nw_hitboxes_strip(side, 2 * (NW_HITBOXES_SEGMENTS + 1), key);
    nw_hitboxes_strip(rim, 2 * (NW_HITBOXES_SEGMENTS + 1), key);
}

/* lo to hi turned by hdg round at, as CylinderCuboidOverlapXZ tests it. */
static void nw_hitboxes_box(const void *key, const struct nuvec_s *at, u16 hdg, const struct nuvec_s *lo,
                            const struct nuvec_s *hi, struct nucolour_s c)
{
    static const int round[5][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } };
    static const int lid[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    struct nuvtx_tc1_s side[10], top[4];
    struct nucolour_s faint = c;
    faint.a = NW_HITBOXES_SIDE;
    c.a = NW_HITBOXES_RIM / 2;
    for (int i = 0; i < 5; i++) {
        struct nuvec_s p = { round[i][0] ? hi->x : lo->x, 0.0f, round[i][1] ? hi->z : lo->z };
        NuVecRotateY(&p, &p, hdg);
        nw_hitboxes_vertex(&side[2 * i], p.x, lo->y, p.z, faint);
        nw_hitboxes_vertex(&side[2 * i + 1], p.x, hi->y, p.z, faint);
    }
    for (int i = 0; i < 4; i++) {
        struct nuvec_s p = { lid[i][0] ? hi->x : lo->x, 0.0f, lid[i][1] ? hi->z : lo->z };
        NuVecRotateY(&p, &p, hdg);
        nw_hitboxes_vertex(&top[i], p.x, hi->y, p.z, c);
    }
    NuMtxSetIdentity(&nw_hitboxes_mtx);
    nw_hitboxes_mtx._30 = at->x;
    nw_hitboxes_mtx._31 = at->y;
    nw_hitboxes_mtx._32 = at->z;
    nw_hitboxes_strip(side, 10, key);
    nw_hitboxes_strip(top, 4, key);
}

static void nw_hitboxes_sphere(const void *key, const struct nuvec_s *at, float radius, struct nucolour_s c)
{
    nw_hitboxes_cylinder(key, at, radius, at->y - radius, at->y + radius, c);
}

static void nw_hitboxes_objects(const struct obj_s *player)
{
    for (int i = 0; i < GAMEOBJECTCOUNT && i < 64; i++) {
        struct obj_s *po = pObj[i];
        if (!po || po == player || !po->draw_frame || !(po->flags & 0x4) || (po->flags & 0x1000)) {
            continue;
        }
        struct nuvec_s at = po->pos;
        if ((po->flags & 0x2000) && po->pLOCATOR && po->model && po->model->pLOCATOR[0]) {
            at = (struct nuvec_s){ po->pLOCATOR->_30, po->pLOCATOR->_31, po->pLOCATOR->_32 };
        }
        bool harm = po->attack != 0 || (po->flags & 0x100);
        struct nucolour_s c =
            harm ? (struct nucolour_s){ NW_HITBOXES_HARM, 0 } : (struct nucolour_s){ NW_HITBOXES_OTHER, 0 };
        float bot = po->bot * po->SCALE, top = po->top * po->SCALE;
        if (po->flags & 0x8000) {
            struct nuvec_s lo = { po->min.x * po->SCALE, bot, po->min.z * po->SCALE };
            struct nuvec_s hi = { po->max.x * po->SCALE, top, po->max.z * po->SCALE };
            nw_hitboxes_box(po, &at, po->hdg, &lo, &hi, c);
        } else {
            nw_hitboxes_cylinder(po, &at, po->RADIUS, at.y + bot, at.y + top, c);
        }
    }
}

/* UpdateChase's c_obj for each chaser. */
static void nw_hitboxes_chasers(void)
{
    for (int i = 0; i < 3; i++) {
        const struct chase_s *chase = &Chase[i];
        if (!nw_hitboxes_chased[i] || GameTimer.frame + 1 - nw_hitboxes_chased[i] > 1) {
            continue;
        }
        for (int j = 0; j < 6; j++) {
            int k = chase->character[j];
            if (!chase->ok[j] || k < 0 || k >= (int)(sizeof(CData) / sizeof(CData[0]))) {
                continue;
            }
            const struct CharacterData *cdata = &CData[k];
            float scale = cdata->scale * chase->scale[j];
            const struct nuvec_s *at = &chase->pos[j];
            struct nucolour_s c = { NW_HITBOXES_CHASER, 0 };
            if (chase->cuboid & (1 << j)) {
                struct nuvec_s lo = { cdata->min.x * scale, cdata->min.y * scale, cdata->min.z * scale };
                struct nuvec_s hi = { cdata->max.x * scale, cdata->max.y * scale, cdata->max.z * scale };
                nw_hitboxes_box(at, at, chase->yrot[j], &lo, &hi, c);
            } else {
                nw_hitboxes_cylinder(at, at, fabsf(cdata->radius) * scale, at->y + cdata->min.y * scale,
                                     at->y + cdata->max.y * scale, c);
            }
        }
    }
}

/* CrateCollisions' boxes, in each group's frame. */
static void nw_hitboxes_crates(const struct obj_s *player)
{
    struct nucolour_s c = { NW_HITBOXES_CRATE, 0 };
    for (int i = 0; i < CRATEGROUPCOUNT; i++) {
        const struct crategroup_s *group = &CrateGroup[i];
        for (int j = 0; j < group->nCrates; j++) {
            const struct cratecube_s *crate = &Crate[group->iCrate + j];
            float dx = crate->pos.x - player->pos.x, dz = crate->pos.z - player->pos.z;
            if (!crate->on || dx * dx + dz * dz > NW_HITBOXES_CRATES * NW_HITBOXES_CRATES) {
                continue;
            }
            struct nuvec_s at = { group->origin.x, crate->pos.y, group->origin.z };
            struct nuvec_s lo = { crate->dx * 0.5f, 0.0f, crate->dz * 0.5f };
            struct nuvec_s hi = { crate->dx * 0.5f + 0.5f, 0.5f, crate->dz * 0.5f + 0.5f };
            nw_hitboxes_box(crate, &at, group->angle, &lo, &hi, c);
        }
    }
}

/* UpdateWumpa's flying shots, UpdateKabooms' growing spheres, the particles tested this frame. */
static void nw_hitboxes_shots(void)
{
    struct nucolour_s c = { NW_HITBOXES_SHOT, 0 };
    for (int i = 0; i < (int)(sizeof(Wumpa) / sizeof(Wumpa[0])); i++) {
        if (Wumpa[i].active == 3 && Wumpa[i].time < Wumpa[i].duration) {
            nw_hitboxes_sphere(&Wumpa[i], &Wumpa[i].pos, (1.0f - Wumpa[i].time * 0.5f) * 0.1f, c);
        }
    }
    for (int i = 0; i < (int)(sizeof(Kaboom) / sizeof(Kaboom[0])); i++) {
        const NwKaboom *k = &Kaboom[i];
        if (k->time < k->duration && !(k->type & 0x40)) {
            nw_hitboxes_sphere(k, &k->pos, k->t0 + (k->t1 - k->t0) * (k->time / k->duration), c);
        }
    }
    for (int i = 0; i < nw_hitboxes_nparticles; i++) {
        nw_hitboxes_sphere(&nw_hitboxes_particles[i], &nw_hitboxes_particles[i].pos, nw_hitboxes_particles[i].radius,
                           c);
    }
}

void nw_hitboxes_draw(void)
{
    const struct obj_s *player = &Character[0].obj;
    if (!nw_profile.hitboxes) {
        return;
    }
    NW_DEVIATION(hitboxes);
    if (!nw_hitboxes_mtl) {
        nw_hitboxes_mtl = nw_hitboxes_material();
    }
    NuRndrStreamAddMtl(&nw_hitboxes_mtl->rs, nw_hitboxes_mtl);
    nw_hitboxes_cylinder(player, &player->pos, nw_hazards_radius(player), player->pos.y + player->bot * player->SCALE,
                         player->pos.y + player->top * player->SCALE, (struct nucolour_s){ NW_HITBOXES_PLAYER, 0 });
    nw_hitboxes_objects(player);
    nw_hitboxes_chasers();
    nw_hitboxes_crates(player);
    nw_hitboxes_shots();
    nw_interp_key(NULL);
    nw_hitboxes_nparticles = 0;
}

void nw_hitboxes_release(void)
{
    nw_hitboxes_mtl = NULL;
}
