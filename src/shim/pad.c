/* libpad: the NuPs2OpenPad / NuPs2ReadPad state machine in nups2/ps2ctl.c. */
#include "shim/shim.h"

int scePadPortOpen(int port, int slot, void *addr)
{
    (void)port, (void)slot, (void)addr;
    NW_SHIM_STUB("scePadPortOpen", 0x00264938u);
    return 0;
}

int scePadPortClose(int port, int slot)
{
    (void)port, (void)slot;
    NW_SHIM_STUB("scePadPortClose", 0x00264b20u);
    return 0;
}

int scePadRead(int port, int slot, void *data)
{
    (void)port, (void)slot, (void)data;
    NW_SHIM_STUB("scePadRead", 0x00264c88u);
    return 0;
}

int scePadGetState(int port, int slot)
{
    (void)port, (void)slot;
    NW_SHIM_STUB("scePadGetState", 0x00264d08u);
    return 0;
}

int scePadGetReqState(int port, int slot)
{
    (void)port, (void)slot;
    NW_SHIM_STUB("scePadGetReqState", 0x00264e20u);
    return 0;
}

int scePadInfoAct(int port, int slot, int actno, int term)
{
    (void)port, (void)slot, (void)actno, (void)term;
    NW_SHIM_STUB("scePadInfoAct", 0x00264ea8u);
    return 0;
}

int scePadInfoMode(int port, int slot, int term, int offs)
{
    (void)port, (void)slot, (void)term, (void)offs;
    NW_SHIM_STUB("scePadInfoMode", 0x002650e8u);
    return 0;
}

int scePadSetMainMode(int port, int slot, int offs, int lock)
{
    (void)port, (void)slot, (void)offs, (void)lock;
    NW_SHIM_STUB("scePadSetMainMode", 0x00265220u);
    return 0;
}

int scePadSetActDirect(int port, int slot, unsigned char *data)
{
    (void)port, (void)slot, (void)data;
    NW_SHIM_STUB("scePadSetActDirect", 0x002652d8u);
    return 0;
}

int scePadSetActAlign(int port, int slot, unsigned char *data)
{
    (void)port, (void)slot, (void)data;
    NW_SHIM_STUB("scePadSetActAlign", 0x00265390u);
    return 0;
}

int scePadInfoPressMode(int port, int slot)
{
    (void)port, (void)slot;
    NW_SHIM_STUB("scePadInfoPressMode", 0x002655d0u);
    return 0;
}

int scePadEnterPressMode(int port, int slot)
{
    (void)port, (void)slot;
    NW_SHIM_STUB("scePadEnterPressMode", 0x00265630u);
    return 0;
}
