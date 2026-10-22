/* nu3d/nucamvu0.s, the VU0 camera clip tests (port/units.toml: hle). The point tests, which retail never reaches,
   stay generated stubs. */
#include <math.h>

#include "vu0.h"

/* What retail keeps in VU0 registers between the two calls: vf1-vf4 and vf5-vf8. */
static struct numtx_s nw_clip_vpc, nw_clip_sci;

/* vclipw.xyz: bits 0-5 for x > |w|, x < -|w|, y > |w|, y < -|w|, z > |w|, z < -|w|. */
static unsigned nw_clip_flags(const struct numtx_s *m, float x, float y, float z)
{
    float p[4];
    nw_vu0_xform(p, m, x, y, z, 1.0f);
    float w = fabsf(p[3]);
    return (unsigned)(p[0] > w) | (unsigned)(p[0] < -w) << 1 | (unsigned)(p[1] > w) << 2 |
           (unsigned)(p[1] < -w) << 3 | (unsigned)(p[2] > w) << 4 | (unsigned)(p[2] < -w) << 5;
}

/* Loads the two clip matrices for NuCameraClipTestExtentsVU0, each after the world matrix wm when it is given.
   A NULL matrix keeps the one loaded before. */
void NuCameraInitClipTestVU0(struct numtx_s *vpc, struct numtx_s *scissor, struct numtx_s *wm)
{
    if (vpc) {
        if (wm) {
            nw_vu0_mtx_mul(&nw_clip_vpc, wm, vpc);
        } else {
            nw_clip_vpc = *vpc;
        }
    }
    if (scissor) {
        if (wm) {
            nw_vu0_mtx_mul(&nw_clip_sci, wm, scissor);
        } else {
            nw_clip_sci = *scissor;
        }
    }
}

/* The 8 corners of the box through both clip matrices. Bits 0-5: the viewport flags all corners share (the box is
   off that side); bits 8-13: the scissor flags of any corner. Like retail, the far corners other than max itself
   are min + (max - min). */
int NuCameraClipTestExtentsVU0(struct nuvec_s *min, struct nuvec_s *max)
{
    float hx = min->x + (max->x - min->x), hy = min->y + (max->y - min->y), hz = min->z + (max->z - min->z);
    unsigned all = 0x3F, any = 0;
    for (int i = 0; i < 8; i++) {
        float x = i & 1 ? hx : min->x, y = i & 2 ? hy : min->y, z = i & 4 ? hz : min->z;
        if (i == 7) {
            x = max->x, y = max->y, z = max->z;
        }
        all &= nw_clip_flags(&nw_clip_vpc, x, y, z);
        any |= nw_clip_flags(&nw_clip_sci, x, y, z);
    }
    return (int)(any << 8 | all);
}
