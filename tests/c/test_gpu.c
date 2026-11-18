#include <stdio.h>

#include <SDL3/SDL.h>

#include "platform/gpu.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static bool flags_are(const char *flags, int preference, const char *want)
{
    char out[96];
    return nw_gpu_flags(flags, preference, out, sizeof(out)) && SDL_strcmp(out, want) == 0;
}

int main(void)
{
    CHECK(flags_are("", 0, ""));
    CHECK(flags_are("", 2, "GpuPreference=2;"));
    CHECK(flags_are("", 1, "GpuPreference=1;"));
    CHECK(flags_are("GpuPreference=1;", 2, "GpuPreference=2;"));
    CHECK(flags_are("GpuPreference=2;", 0, ""));
    CHECK(flags_are("GpuPreference=0;", 0, ""));
    CHECK(flags_are("SwapEffectUpgradeEnable=1;GpuPreference=1;AutoHDREnable=1;", 2,
                    "SwapEffectUpgradeEnable=1;AutoHDREnable=1;GpuPreference=2;"));
    CHECK(flags_are("AutoHDREnable=1;GpuPreference=2;", 0, "AutoHDREnable=1;"));
    CHECK(flags_are("VRROptimizeEnable=0", 1, "VRROptimizeEnable=0;GpuPreference=1;"));
    CHECK(flags_are(";;GpuPreference=2", 0, ""));

    char small[17];
    CHECK(nw_gpu_flags("", 2, small, sizeof(small)) &&
          SDL_strcmp(small, "GpuPreference=2;") == 0);
    CHECK(!nw_gpu_flags("", 2, small, sizeof(small) - 1));
    CHECK(!nw_gpu_flags("AutoHDREnable=1;", 2, small, sizeof(small)));
    CHECK(!nw_gpu_flags("", 0, small, 0));

    if (failures) {
        fprintf(stderr, "gpu: %d checks failed\n", failures);
        return 1;
    }
    printf("gpu: all checks passed\n");
    return 0;
}
