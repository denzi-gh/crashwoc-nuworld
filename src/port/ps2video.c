/* nups2/ps2video, which the port replaces (port/units.toml): the parts the decomp reaches so far. */
#include <SDL3/SDL.h>

/* The vblank-end interrupt the debug log masks while it writes (NuDebugMsgFunction): a nesting depth. */
static int nw_vblank_e_depth;

void NuDisableVBlankE(void)
{
    nw_vblank_e_depth++;
}

void NuEnableVBlankE(void)
{
    if (nw_vblank_e_depth > 0) {
        nw_vblank_e_depth--;
    }
}

/* EE timer 0 as timeUserReset programs it (BUSCLK / 256 = 576 kHz, 16 bits); NuPPLoadBuffer times its reads
   with it. */
#define NW_T0_HZ 576000u

static Uint64 nw_t0_base;

void timeUserReset(void)
{
    nw_t0_base = SDL_GetTicksNS();
}

unsigned int timeUserRead(void)
{
    return (unsigned int)((SDL_GetTicksNS() - nw_t0_base) * NW_T0_HZ / SDL_NS_PER_SECOND) & 0xffffu;
}
