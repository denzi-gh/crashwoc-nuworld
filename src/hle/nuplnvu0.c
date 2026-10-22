#include <math.h>

#include "numath/numathtypes.h"

int NuPlnLineVU0(struct nuplane_s *pln, struct nuvec_s *p0, struct nuvec_s *p1, struct nuvec_s *hit)
{
    float d0 = p0->x * pln->a + p0->y * pln->b + p0->z * pln->c + pln->d;
    float d1 = p1->x * pln->a + p1->y * pln->b + p1->z * pln->c + pln->d;
    if (!signbit(d0) == !signbit(d1)) {
        return 0;
    }
    float q = (0.0f - d0) / (d1 - d0);
    hit->x = (p1->x - p0->x) * q + p0->x;
    hit->y = (p1->y - p0->y) * q + p0->y;
    hit->z = (p1->z - p0->z) * q + p0->z;
    return 1;
}
