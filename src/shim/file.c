/* sce file I/O (the nufile layer's device calls). */
#include "shim/shim.h"

int sceOpen(char *name, int flag, ...)
{
    (void)name, (void)flag;
    NW_SHIM_STUB("sceOpen", 0x0027cad8u);
    return -1;
}

int sceClose(int fd)
{
    (void)fd;
    NW_SHIM_STUB("sceClose", 0x0027cd60u);
    return 0;
}

int sceRead(int fd, void *addr, int size)
{
    (void)fd, (void)addr, (void)size;
    NW_SHIM_STUB("sceRead", 0x0027d118u);
    return -1;
}

int sceWrite(int fd, void *addr, int size)
{
    (void)fd, (void)addr, (void)size;
    NW_SHIM_STUB("sceWrite", 0x0027d388u);
    return -1;
}

int sceLseek(int fd, int offset, int how)
{
    (void)fd, (void)offset, (void)how;
    NW_SHIM_STUB("sceLseek", 0x0027cee0u);
    return 0;
}

int sceIoctl(int fd, int req, void *arg)
{
    (void)fd, (void)req, (void)arg;
    NW_SHIM_STUB("sceIoctl", 0x0027d648u);
    return 0;
}
