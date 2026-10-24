/* libmc: only the editors' memory card files (gamelib/edfile, media 2) reach it; saves go through gamelib/saveload. */
#include "shim/shim.h"

int sceMcOpen(int port, int slot, const char *name, int mode)
{
    (void)port, (void)slot, (void)name, (void)mode;
    NW_SHIM_STUB("sceMcOpen", 0x00266fc8u);
    return -1;
}

int sceMcClose(int fd)
{
    (void)fd;
    NW_SHIM_STUB("sceMcClose", 0x00267138u);
    return -1;
}

int sceMcSync(int mode, int *cmd, int *result)
{
    (void)mode, (void)cmd;
    NW_SHIM_STUB("sceMcSync", 0x00267668u);
    if (result != NULL) {
        *result = -1;
    }
    return 1;
}
