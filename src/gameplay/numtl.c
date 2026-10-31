/* Wraps of nu3d/numtl: the features' own materials go when retail frees its own. */
#include "nu3d/numtl.h"
#include "nu3d/numtl.retail.h"
#include "gameplay/airindicator.h"
#include "gameplay/bosshealth.h"

void NuMtlRelease(void)
{
    NuMtlRelease__retail();
    nw_air_indicator_release();
    nw_boss_health_release();
}
