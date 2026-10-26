/* gamelib/nubridge's rope writer (port/units.toml override): NuBridgeStreamRope as a VERTS record in the fx stream
   instead of fx2 packets. */
#include <string.h>

#include "gfx/rndr.h"
#include "nu3d/nustream.h"

/* Declared by no header. */
float NuFsqrt(float x);

/* Each segment's four faces, the offsets turning a quarter about the rope from face to face as retail's strips do,
   the texcoords a strip of DebMat's texture. */
int NuBridgeStreamRope(struct rndrstream_s *rs, struct nuvec_s *points, int npoints, int colour)
{
    struct nuvec_s side, up, tmp;
    NwVuSpriteVertex q[4], *v;
    if (npoints < 2) {
        return 1;
    }
    side.x = points[1].z - points[0].z;
    side.y = 0.0f;
    side.z = points[0].x - points[1].x;
    float scale = 0.05f / NuFsqrt(side.x * side.x + side.z * side.z);
    side.x *= scale;
    side.z *= scale;
    up.x = 0.0f;
    up.y = 0.05f;
    up.z = 0.0f;
    memset(q, 0, sizeof(q));
    for (int k = 0; k < 4; k++) {
        q[k].tex[0] = k & 1 ? 0x5F0 : 0x400;
        q[k].tex[1] = k & 2 ? 0xFF0 : 0xC00;
        memcpy(q[k].col, &colour, 4);
    }
    nw_rndr_verts(rs, NW_VERTS_TRIS, (uint32_t)(npoints - 1) * 4 * 6, &v);
    for (int i = 0; i < npoints - 1; i++) {
        for (int face = 0; face < 4; face++) {
            for (int k = 0; k < 4; k++) {
                const struct nuvec_s *p = &points[i + (k >> 1)], *d = k & 1 ? &up : &side;
                q[k].pos[0] = p->x + d->x;
                q[k].pos[1] = p->y + d->y;
                q[k].pos[2] = p->z + d->z;
            }
            v = nw_rndr_quad(v, q);
            tmp = side;
            side.x = -up.x;
            side.y = -up.y;
            side.z = -up.z;
            up = tmp;
        }
    }
    return 1;
}
