#include <math.h>

#include "vu0.h"

#include "nu_port.h"
#include "port/wide.h"

static struct numtx_s nw_clip_vpc, nw_clip_sci;

/* xs scales clip x: nw_wide.k gives retail's 4:3 frustum under a wider view. */
static unsigned nw_clip_flags(const struct numtx_s *m, float x, float y, float z, float xs)
{
    float p[4];
    nw_vu0_xform(p, m, x, y, z, 1.0f);
    p[0] *= xs;
    float w = fabsf(p[3]);
    return (unsigned)(p[0] > w) | (unsigned)(p[0] < -w) << 1 | (unsigned)(p[1] > w) << 2 |
           (unsigned)(p[1] < -w) << 3 | (unsigned)(p[2] > w) << 4 | (unsigned)(p[2] < -w) << 5;
}

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

int NuCameraClipTestExtentsVU0(struct nuvec_s *min, struct nuvec_s *max)
{
    float hx = min->x + (max->x - min->x), hy = min->y + (max->y - min->y), hz = min->z + (max->z - min->z);
    unsigned all = 0x3F, any = 0, retail = 0x3F;
    for (int i = 0; i < 8; i++) {
        float x = i & 1 ? hx : min->x, y = i & 2 ? hy : min->y, z = i & 4 ? hz : min->z;
        if (i == 7) {
            x = max->x, y = max->y, z = max->z;
        }
        all &= nw_clip_flags(&nw_clip_vpc, x, y, z, 1.0f);
        any |= nw_clip_flags(&nw_clip_sci, x, y, z, 1.0f);
        retail &= nw_wide.k > 1.0f ? nw_clip_flags(&nw_clip_vpc, x, y, z, nw_wide.k) : all;
    }
    nw_wide_clip(retail == 0);
    return (int)(any << 8 | all);
}
