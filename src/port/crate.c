/* game/crate's explosion quad (port/units.toml override): AddQuad3DrotXYZ as a VERTS record in GS coordinates instead
   of a DIRECT GIF packet in the material's stream. */
#include <string.h>

#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/numtl.h"
#include "numath/numtx.h"

/* Declared by no header. */
void NuVec4MtxTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx);
void NuVec4ScaleXYZVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale);
extern struct rndrstream_s *rndrstream_3d;

/* The corners turned by a, at pos, in GS coordinates, which retail sends as integers. */
void AddQuad3DrotXYZ(struct nuvec_s *pos, struct nuvec_s *shape, struct numtl_s *mtl, s32 *a, f32 *uvs, u32 colour)
{
    struct numtx_s m;
    NwVuSpriteVertex q[4], *v;
    if (NuCameraClipTestPoints(pos, 1, 0)) {
        return;
    }
    struct numtx_s *vpcs = NuCameraGetVPCSMtx();
    NuMtxSetRotateXYZ(&m, (struct nuangvec_s *)a);
    NuMtxTranslate(&m, pos);
    NuMtxMulH(&m, &m, vpcs);
    memset(q, 0, sizeof(q));
    for (int i = 0; i < 4; i++) {
        struct nuvec4_s v4 = { shape[i].x, shape[i].y, shape[i].z, 1.0f };
        NuVec4MtxTransformVU0(&v4, &v4, &m);
        NuVec4ScaleXYZVU0(&v4, &v4, 1.0f / v4.w);
        q[i].pos[0] = (float)(int)v4.x;
        q[i].pos[1] = (float)(int)v4.y;
        q[i].pos[2] = (float)(int)v4.z;
        q[i].pos[3] = 1.0f;
        q[i].tex[0] = uvs[i * 2] * 4096.0f;
        q[i].tex[1] = uvs[i * 2 + 1] * 4096.0f;
        memcpy(q[i].col, &colour, 4);
    }
    rndrstream_3d = &mtl->rs;
    nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, 6, &v);
    nw_rndr_quad(v, q);
}
