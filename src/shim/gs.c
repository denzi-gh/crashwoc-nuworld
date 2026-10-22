/* GS: the vblank wait cut.c and nusound.c call directly. */
#include "shim/shim.h"

int sceGsSyncV(int mode)
{
    (void)mode;
    NW_SHIM_STUB("sceGsSyncV", 0x00261348u);
    return 0;
}
