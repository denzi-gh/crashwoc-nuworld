/* SIF: the IOP reboot, module loads and the NUSOUND RPC. There is no IOP: the reboot NuFileInitEx waits for
   is done at once (both loops spin until a non-zero return). */
#include "shim/shim.h"

void sceSifInitRpc(unsigned int mode)
{
    (void)mode;
}

int sceSifRebootIop(const char *img_name)
{
    (void)img_name;
    return 1;
}

int sceSifSyncIop(void)
{
    return 1;
}

int sceSifLoadModule(const char *name, int arg_size, const char *args)
{
    (void)name, (void)arg_size, (void)args;
    NW_SHIM_STUB("sceSifLoadModule", 0x00280540u);
    return -1;
}

int sceSifBindRpc(void *client, unsigned int rpc_number, unsigned int mode)
{
    (void)client, (void)rpc_number, (void)mode;
    NW_SHIM_STUB("sceSifBindRpc", 0x0027b9d8u);
    return -1;
}

int sceSifCallRpc(void *client, unsigned int fno, unsigned int mode, void *send, int ssize, void *receive,
                  int rsize, void *end_func, void *end_para)
{
    (void)client, (void)fno, (void)mode, (void)send, (void)ssize, (void)receive, (void)rsize;
    (void)end_func, (void)end_para;
    NW_SHIM_STUB("sceSifCallRpc", 0x0027bba8u);
    return -1;
}

int sceSifCheckStatRpc(void *rpcd)
{
    (void)rpcd;
    NW_SHIM_STUB("sceSifCheckStatRpc", 0x0027bd98u);
    return 0;
}
