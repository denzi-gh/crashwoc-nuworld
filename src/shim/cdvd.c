/* sceCd* and the file system reset NuFileInitEx calls. */
#include "shim/shim.h"

int sceCdInit(int mode)
{
    (void)mode;
    NW_SHIM_STUB("sceCdInit", 0x002666c8u);
    return 0;
}

int sceCdMmode(int media)
{
    (void)media;
    NW_SHIM_STUB("sceCdMmode", 0x00266ba0u);
    return 0;
}

int sceCdDiskReady(int mode)
{
    (void)mode;
    NW_SHIM_STUB("sceCdDiskReady", 0x002669a8u);
    return 0;
}

int sceFsReset(void)
{
    NW_SHIM_STUB("sceFsReset", 0x0027caa0u);
    return 0;
}
