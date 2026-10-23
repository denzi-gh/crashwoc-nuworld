/* nu3d/numtl's state block (port/units.toml override): the GS state and the VU words of a material, kept for the
   renderer instead of retail's DMA packet. */
#include <string.h>

#include "gfx/rndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"

extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC
extern int numtl_force_mipmap;
extern int sys_mm_k;
extern int sys_mm_l;
extern struct rndrstream_s *rndrstream_mtl_sort;

_Static_assert(sizeof(NwStateBlock) <= sizeof(((struct numtl_s *)0)->sbdata), "the state block fits sbdata");

/* NuTexWriteMaterialSettings' K, L and mip levels with CreateStateBlock's registers. keep leaves the VU 0x14-0x15
   words (NuMtlAnimate's offset); the row is zeroed either way, as retail's STROW. */
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

/* Retail's order: the materials as NuMtlInsert sorted them (sort_pri, then tid), each one's stream after mtl_sort's
   records; a record carries its state block, texture and microcode, which retail calls before the stream. */
void NuMtlBuildRenderList(void)
{
    for (struct numtl_s *mtl = NuMtlIterate(1); mtl; mtl = mtl->snext) {
        nw_rndr_splice(rndrstream_mtl_sort, &mtl->rs);
        NuRndrStreamResetStream(&mtl->rs, 1);
    }
}
