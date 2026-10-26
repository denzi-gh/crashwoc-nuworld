/* game/cloudfx's draw (port/units.toml override): cloudRender's clouds as the sprites the clouds program
   (clouds.vsm) makes of them, a VERTS record in the clouds stream instead of its header and the REF of the clouds. */
#include <stdint.h>
#include <string.h>

#include "game/cloudfx.h"
#include "gfx/rndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "numath/nuvec.h"

extern struct rndrstream_s *clouds_rs;

/* An offset folded back into +-rng as clouds.vsm does: past rng (by its 1e6 scale's one unit) by 2 * rng. */
static f32 nw_cloud_fold(f32 d, f32 rng)
{
    f32 above = (d - rng) * 1000000.0f >= 1.0f ? -rng - rng : 0.0f;
    f32 below = (d + rng) * 1000000.0f <= -1.0f ? rng + rng : 0.0f;
    return d + above + below;
}

/* Each cloud's point (1/16 units) off the scroll wrapped into 2 * rng, folded into +-rng about it, at pos; a sprite
   of +-size by +-size / 2 GS units before the divide, s and t 0 to 1, in col times density with alpha 126. Retail
   also drops a cloud nearer than w 1 and one off the screen, which the GPU clips. */
void cloudRender(struct nuvec_s *pos, struct nuvec_s *off, f32 size, f32 rng, s32 density, s32 maxc,
                 struct cloud_s *cl, struct numtl_s *mtl, struct nuvec_s *col)
{
    static const float identity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    struct nuvec_s scroll;
    NwVuSpriteVertex q[4], *v;
    struct rndrstream_s *rs = clouds_rs;
    NuRndrStreamAddMtl(rs, mtl);
    NuVecScale(&scroll, off, rng);
    f32 wrap = rng + rng;
    f32 x = pos->x + scroll.x, y = pos->y + scroll.y, z = pos->z + scroll.z;
    f32 wx = x - wrap * (s32)(x / wrap), wy = y - wrap * (s32)(y / wrap), wz = z - wrap * (s32)(z / wrap);
    const uint8_t rgba[4] = { (uint8_t)(s32)(col->x * (f32)density), (uint8_t)(s32)(col->y * (f32)density),
                              (uint8_t)(s32)(col->z * (f32)density), 126 };
    memset(q, 0, sizeof(q));
    for (int k = 0; k < 4; k++) {
        q[k].tex[0] = (float)(k & 1);
        q[k].tex[1] = (float)(k >> 1);
        q[k].tex[2] = -size;
        q[k].tex[3] = -size * 0.5f;
        memcpy(q[k].col, rgba, 4);
    }
    int n = maxc > 0 ? maxc : 0;
    NwCmd *c = nw_rndr_verts(rs, NW_VERTS_SPRITE, (uint32_t)n * 6, &v);
    memcpy(c->local, identity, sizeof(identity));
    for (int i = 0; i < n; i++) {
        f32 px = nw_cloud_fold(cl[i].x / 16.0f - wx, rng) + pos->x;
        f32 py = nw_cloud_fold(cl[i].y / 16.0f - wy, rng) + pos->y;
        f32 pz = nw_cloud_fold(cl[i].z / 16.0f - wz, rng) + pos->z;
        for (int k = 0; k < 4; k++) {
            q[k].pos[0] = px;
            q[k].pos[1] = py;
            q[k].pos[2] = pz;
        }
        v = nw_rndr_quad(v, q);
    }
}
