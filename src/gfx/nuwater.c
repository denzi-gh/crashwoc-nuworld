/* nu3d/nuwater's wave tables (port/units.toml overrides): retail's waves from the same NuRandSeed(0x11) draws, each
   material's as a WAVES record where retail unpacks them to VU 0x1B2 at the head of its stream. */
#include <stdint.h>

#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "numath/numtx.h"
#include "numath/nurand.h"
#include "numath/nuvec.h"

/* nuwater.c's WATERTABLEENTRY */
struct waterTableEntry_s {
    struct nuvec4_s wave[32];
    float fx1;
    float fx4;
};

extern int nwater_mtl;
extern int nwater_tables;
extern struct numtl_s *water_mtls[8];
extern int water_mtl_tables[8];
extern struct waterTableEntry_s water_table[8];
extern struct nucamera_s global_camera;
extern float NuTrigTable[];

void NuRndrStreamLink(struct rndrstream_s *stream);

/* 32 waves at time freq and amplitude amp, in the camera's axes at half scale: the texcoord offset x, y, then 1 and
   the lift along the local y, the unturned x. The two draws of each axis are its speed, then its phase. */
static void nw_water_table(float table[32][4], float freq, float amp, struct numtx_s *mtx)
{
    struct nuvec_s wave, pos;
    NuRandSeed(0x11);
    for (int j = 0; j < 32; j++) {
        float speed = NuRandFloat() * 0.4f + 0.8f;
        float phase = NuRandFloat() * 6.284f;
        wave.x = NuTrigTable[(u16)(int32_t)((freq * speed + phase) * 10430.378f)] * amp;
        wave.y = 0.0f;
        speed = NuRandFloat() * 0.4f + 0.7f;
        phase = NuRandFloat() * 6.284f;
        wave.z = NuTrigTable[(u16)(int32_t)((freq * speed + phase) * 10430.378f)] * amp;
        NuVecMtxTransformH(&pos, &wave, mtx);
        table[j][0] = pos.x;
        table[j][1] = pos.y;
        table[j][2] = 1.0f;
        table[j][3] = wave.x;
    }
}

static void nw_water_mtx(struct numtx_s *mtx)
{
    struct nuvec_s scale = { 0.5f, 0.5f, 0.5f };
    NuMtxInvR(mtx, &global_camera.mtx);
    NuMtxScale(mtx, &scale);
    mtx->_03 = mtx->_13 = mtx->_23 = 0.0f;
}

/* One table per (fx1, fx4) pair, kept in water_table, then each material's into its stream. */
void animateWaterTables(float scale, float time)
{
    struct numtx_s mtx;
    nw_water_mtx(&mtx);
    for (int i = 0; i < nwater_tables; i++) {
        nw_water_table((float(*)[4])water_table[i].wave, time * water_mtls[i]->fx1.f32,
                       water_mtls[i]->fx4.f32 * scale, &mtx);
    }
    for (int i = 0; i < nwater_mtl; i++) {
        struct rndrstream_s *stream = &water_mtls[i]->rs;
        NuRndrStreamLink(stream);
        NwWaves *w = nw_rndr_waves(stream);
        const struct nuvec4_s *table = water_table[water_mtl_tables[i]].wave;
        for (int j = 0; j < 32; j++) {
            w->wave[j][0] = table[j].x;
            w->wave[j][1] = table[j].y;
            w->wave[j][2] = table[j].z;
            w->wave[j][3] = table[j].w;
        }
    }
}

/* Each material's own table, from its fx1 and fx4. */
void animateWater(float scale, float time)
{
    struct numtx_s mtx;
    nw_water_mtx(&mtx);
    for (int i = 0; i < nwater_mtl; i++) {
        struct numtl_s *mtl = water_mtls[i];
        NuRndrStreamLink(&mtl->rs);
        nw_water_table(nw_rndr_waves(&mtl->rs)->wave, time * mtl->fx1.f32, mtl->fx4.f32 * scale, &mtx);
    }
}
