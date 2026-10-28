#include "port/wide.h"

#include <math.h>

#include "nu_port.h"

struct nw_wide_s nw_wide = { 1.0f, 0.75f, 640, 1.0f, 1.0f, 1.0f, 1.0f, 0 };

void nw_wide_set(float aspect)
{
    float a = aspect > NW_WIDE_MIN ? aspect < NW_WIDE_MAX ? aspect : NW_WIDE_MAX : NW_WIDE_MIN;
    int width = 2 * (int)lroundf(320.0f * a / NW_WIDE_MIN);
    /* k from the rounded width: the camera then matches the frame's pixels exactly */
    float k = width / 640.0f;
    nw_wide = (struct nw_wide_s){ k, 0.75f / k, width, 1.0f, k, 1.0f, 1.0f, 0 };
}

int nw_wide_width(void)
{
    return nw_wide.width;
}

int nw_wide_anim(struct nuinstance_s *inst, int test)
{
    (void)inst;
    return test;
}

void nw_wide_mark(void)
{
}

int nw_wide_seen(int drawn)
{
    return drawn;
}
