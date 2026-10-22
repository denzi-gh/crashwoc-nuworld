/* sceCd* and the file system reset NuFileInitEx calls: the imported disc is always ready. */
#include "shim/shim.h"

#define NW_SCE_CD_COMPLETE 2

int sceCdInit(int mode)
{
    (void)mode;
    return 1;
}

int sceCdMmode(int media)
{
    (void)media;
    return 1;
}

int sceCdDiskReady(int mode)
{
    (void)mode;
    return NW_SCE_CD_COMPLETE;
}

int sceFsReset(void)
{
    return 0;
}
