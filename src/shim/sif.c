/* SIF without an IOP: the reboot NuFileInitEx waits for is done at once (both loops spin until a non-zero return), and
   the NUSOUND RPC runs src/audio/nusound.c on the calling thread. */
#include "audio/nusound.h"
#include "port/game.h"
#include "shim/shim.h"

#define NW_SIF_RPC_US 100 /* the EE's time for one call, the order of a SIF round trip */

/* sceSifClientData as decomp nusound.c declares it. */
typedef struct NwSifClient {
    struct {
        void *paddr;
        unsigned int pid;
        int tid;
        unsigned int mode;
    } rpcd;
    unsigned int command;
    void *buff;
    void *cbuff;
    void *end_func;
    void *end_param;
    void *serve;
} NwSifClient;

static int nw_sif_nusound; /* what a bound client's serve points at */

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

/* Only NUSOUND serves; another number leaves serve NULL, as a bind before the server registers. */
int sceSifBindRpc(void *client, unsigned int rpc_number, unsigned int mode)
{
    NwSifClient *c = client;
    (void)mode;
    c->command = 0;
    c->serve = rpc_number == NW_NUSOUND_RPC ? &nw_sif_nusound : NULL;
    return 0;
}

/* Done when it returns, after its time on the EE. In mode 0 the EE waits for the reply, a yield point. */
int sceSifCallRpc(void *client, unsigned int fno, unsigned int mode, void *send, int ssize, void *receive,
                  int rsize, void *end_func, void *end_para)
{
    const NwSifClient *c = client;
    (void)end_func, (void)end_para;
    if (c->serve != &nw_sif_nusound) {
        return -1;
    }
    nw_game_spend_us(NW_SIF_RPC_US);
    nw_nusound_rpc(fno, send, ssize > 0 ? (size_t)ssize : 0, receive, rsize > 0 ? (size_t)rsize : 0);
    if (mode == 0) {
        nw_ee_yield();
    }
    return 0;
}

int sceSifCheckStatRpc(void *rpcd)
{
    (void)rpcd;
    return 0;
}
