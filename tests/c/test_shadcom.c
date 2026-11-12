/* Native shadows work without a VFS or any SHD files, and reuse the visible model's skin palette. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game/shadcom.h"
#include "gfx/rndr.h"
#include "gfx/shadow.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "numath/numtx.h"
#include "port/shadows.h"

struct shaddata_s *ShadDataLoad(char *, union variptr_u *);
struct shaddata_s *InstShadDataLoad(char *);
struct shadentry_s *ShadFindData(struct shaddata_s *, char *);
void ShadDataDestroy(struct shaddata_s *);
void InstShadDataDestroy(struct shaddata_s *);
void ShadRndr(struct numtx_s *, struct shadentry_s *, f32, f32);
void NuRndrInitEx(int);
void NuRndrEndScene(void);
extern int no_default_font;

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void point(float out[3], const float p[3], const float m[16])
{
    for (int k = 0; k < 3; k++) {
        out[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + m[12 + k];
    }
}

static void projection(void)
{
    struct numtx_s plane;
    NuMtxSetIdentity(&plane);
    plane._31 = 2.0f;
    float m[16], out[3], light[3] = { 1, 2, 0 };
    const float p[3] = { 3, 6, 5 };
    CHECK(nw_shadow_projection(m, &plane._00, light));
    point(out, p, m);
    CHECK(fabsf(out[0] - 1) < 1e-5f && fabsf(out[1] - 2) < 1e-5f && fabsf(out[2] - 5) < 1e-5f);
    /* A sloping receiver: every projected point lies on its plane, including points already on it. */
    plane._10 = 0.6f;
    plane._11 = 0.8f;
    CHECK(nw_shadow_projection(m, &plane._00, light));
    point(out, p, m);
    CHECK(fabsf(0.6f * out[0] + 0.8f * (out[1] - 2)) < 1e-5f);
    float q[3];
    point(q, out, m);
    CHECK(fabsf(q[0] - out[0]) < 1e-5f && fabsf(q[1] - out[1]) < 1e-5f);
    /* Grazing, zero, below-ground and invalid light vectors have bounded, finite results. */
    const float lights[][3] = { { 1, 0, 0 }, { 0, 0, 0 }, { 0, -1, 0 }, { NAN, 1, 0 } };
    NuMtxSetIdentity(&plane);
    for (unsigned i = 0; i < sizeof(lights) / sizeof(lights[0]); i++) {
        CHECK(nw_shadow_projection(m, &plane._00, lights[i]));
        point(out, p, m);
        CHECK(isfinite(out[0]) && fabsf(out[1]) < 1e-5f);
    }
    plane._11 = 0;
    CHECK(nw_shadow_projection(m, &plane._00, light)); /* actual ScaleFlatShadow matrix */
    plane._00 = 0;
    CHECK(!nw_shadow_projection(m, &plane._00, light));
    NuMtxSetIdentity(&plane);
    plane._31 = NAN;
    CHECK(!nw_shadow_projection(m, &plane._00, light));
}

static void native(void)
{
    /* Neither the compatibility loader nor animation lookup touches a file or consumes the loading heap. */
    char heap[16];
    union variptr_u buffer = { .voidptr = heap };
    struct shaddata_s *handle = ShadDataLoad("does-not-exist.shd", &buffer);
    CHECK(handle && buffer.voidptr == heap);
    CHECK(InstShadDataLoad("also-missing.shd") == handle);
    struct shadentry_s *entry = ShadFindData(handle, "unseen animation");
    CHECK(entry && ShadFindData(handle, NULL) == entry && ShadFindData(NULL, NULL) == NULL);
    ShadDataDestroy(handle);
    InstShadDataDestroy(handle);

    no_default_font = 1;
    NuTexInit();
    NuMtlInit();
    NuRndrInitEx(0x10000);
    ShadComInit();
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, count = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < count; i++) {
        CHECK(jobs[i].op != NW_GPU_UPLOAD_TEX); /* no shadow CLUTs */
    }
    SDL_free(jobs);

    struct numtx_s world, plane;
    NuMtxSetIdentity(&world);
    NuMtxSetIdentity(&plane);
    plane._31 = 0.025f;
    struct NUHGOBJ_s obj = { 0 };
    obj.bbox_min = (struct nuvec_s){ -1, 0, -1 };
    obj.bbox_max = (struct nuvec_s){ 1, 2, 1 };
    struct numtl_s *mtl = NuMtlCreate(1);
    NuMtlUpdate(mtl);
    NuRndrStreamAddMtl(&mtl->rs, mtl);
    NuRndrSetLocalMtx(&world, 1);
    nw_shadow_character_begin();
    bool captured = nw_shadow_model_begin(&obj, &world);
    struct numtx_s *joints = nw_rndr_joints(1);
    NuMtxSetIdentity(joints);
    joints->_30 = 0.5f;
    NwGpuSkin skin = { 0 }, reflected = { 0 };
    NwCmd *draw = nw_rndr_cmd(&mtl->rs, NW_CMD_SKIN);
    draw->skin = &skin;
    draw->key = 42;
    nw_shadow_record(draw);
    nw_shadow_model_end(captured);
    CHECK(!nw_shadow_model_begin(&obj, &world));
    draw = nw_rndr_cmd(&mtl->rs, NW_CMD_SKIN);
    draw->skin = &reflected;
    nw_shadow_record(draw); /* reflection must not replace the main caster */
    ShadRndr(&plane, entry, 10000, 1);
    ShadRndr(&plane, entry, 0, 1); /* one caster cannot accidentally be submitted twice */
    nw_shadow_character_end();
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    CHECK(p->ncmds == 5 && p->njoints == 1 && p->joints[0][12] == 0.5f);
    unsigned begins = 0, ends = 0, shadows = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        begins += c->op == NW_CMD_SHADOW_BEGIN;
        ends += c->op == NW_CMD_SHADOW_END;
        if (c->op == NW_CMD_SHADOW_BEGIN) {
            CHECK(c->local[13] == plane._31 && c->alpha == 127);
        }
        if (c->op == NW_CMD_SKIN && c->shadow) {
            shadows++;
            CHECK(c->skin == &skin && c->first == 0 && c->count == 1 && c->key != 42);
        }
    }
    CHECK(begins == 1 && ends == 1 && shadows == 1);
    nw_shadow_reset();
}

int main(void)
{
    projection();
    native();
    printf("native shadows: %s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
