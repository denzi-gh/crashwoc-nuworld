/* nu3d/numtl override (port/units.toml): material state kept for the renderer instead of retail's DMA packet. */
#include <string.h>

#include "gfx/rndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"

extern struct nutexslot_s *tinfo;
#define TexList tinfo
extern int numtl_force_mipmap;
extern int sys_mm_k;
extern int sys_mm_l;
extern struct rndrstream_s *rndrstream_mtl_sort;

_Static_assert(sizeof(NwStateBlock) <= sizeof(((struct numtl_s *)0)->sbdata), "the state block fits sbdata");

/* keep leaves the VU 0x14-0x15 words (NuMtlAnimate's offset); the row is zeroed either way, as retail's STROW. */
int CreateStateBlock(struct numtl_s *mtl, int keep)
{
    NwStateBlock *sb = (NwStateBlock *)mtl->sbdata;
    uint32_t attrib[2];
    memcpy(attrib, &mtl->attrib, sizeof(attrib));
    int k = numtl_force_mipmap ? sys_mm_k : mtl->K;
    int l = numtl_force_mipmap ? sys_mm_l : mtl->L;
    uint32_t levels = mtl->tid > 0 ? TexList[mtl->tid - 1].ps2tex.mipcount : 0;
    sb->gs = nw_mtl_state(attrib, (int16_t)k, (uint8_t)l, levels);
    sb->diffuse[0] = mtl->diffuse.r * 128.0f;
    sb->diffuse[1] = mtl->diffuse.g * 128.0f;
    sb->diffuse[2] = mtl->diffuse.b * 128.0f;
    sb->diffuse[3] = mtl->alpha * 128.0f;
    if (!keep) {
        memset(sb->vuattr, 0, 8 * sizeof(uint32_t));
    }
    memset(sb->vuattr + 8, 0, 5 * sizeof(uint32_t));
    mtl->vuattr.u32 = sb->vuattr;
    return 1;
}

const NwStateBlock *nw_rndr_state(const struct numtl_s *mtl)
{
    return (const NwStateBlock *)mtl->sbdata;
}

/* The port's per-material fades and effects (port/opening.c), keyed by the material's address. Each holds only while
   it is set again (the opening does every frame it draws them): NW_MTL_HOLD frames after that it goes, so a scene
   freed and another allocated at the same addresses (a load, a level, the warp room) never inherits them. */
#define NW_MTL_HOLD 2u
static uint32_t nw_mtl_frame; /* the frames the renderer has snapped (nw_rndr_mtl_frame) */

void nw_rndr_mtl_frame(void)
{
    nw_mtl_frame++;
}

static struct {
    const struct numtl_s *mtl;
    float fx[NW_MTL_FX_LEN];
    uint32_t set;
} nw_mtl_fxs[48];

void nw_rndr_mtl_fx(struct numtl_s *mtl, const float fx[NW_MTL_FX_LEN])
{
    int slot = -1;
    for (int i = 0; i < 48; i++) {
        if (nw_mtl_fxs[i].mtl == mtl) {
            slot = i;
            break;
        }
        if ((!nw_mtl_fxs[i].mtl || nw_mtl_frame - nw_mtl_fxs[i].set > NW_MTL_HOLD) && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return;
    }
    nw_mtl_fxs[slot].mtl = fx ? mtl : NULL;
    nw_mtl_fxs[slot].set = nw_mtl_frame;
    if (fx) {
        memcpy(nw_mtl_fxs[slot].fx, fx, sizeof(nw_mtl_fxs[slot].fx));
    }
}

void nw_rndr_mtl_fx_of(const struct numtl_s *mtl, float fx[NW_MTL_FX_LEN])
{
    for (int i = 0; i < 48; i++) {
        if (nw_mtl_fxs[i].mtl == mtl) {
            if (nw_mtl_frame - nw_mtl_fxs[i].set > NW_MTL_HOLD) {
                nw_mtl_fxs[i].mtl = NULL; /* not set again: gone */
                break;
            }
            memcpy(fx, nw_mtl_fxs[i].fx, sizeof(nw_mtl_fxs[i].fx));
            return;
        }
    }
    memset(fx, 0, NW_MTL_FX_LEN * sizeof(float));
}

void NuMtlBuildRenderList(void)
{
    for (struct numtl_s *mtl = NuMtlIterate(1); mtl; mtl = mtl->snext) {
        nw_rndr_splice(rndrstream_mtl_sort, &mtl->rs);
        NuRndrStreamResetStream(&mtl->rs, 1);
        mtl->rs.mtl = mtl;
    }
}
