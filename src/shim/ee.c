/* EE kernel: threads, semaphores, caches. The decomp calls them by their nw_ee_* names (port/nw_ee.h). */
#include "shim/shim.h"

/* bgload passes &_gp as the loader thread's gp register; nothing reads it (port/data.toml [owned]). */
int _gp;

int nw_ee_ChangeThreadPriority(int thid, int prio)
{
    (void)thid, (void)prio;
    NW_SHIM_STUB("ChangeThreadPriority", 0x00278b30u);
    return 0;
}

int nw_ee_CreateSema(void *param)
{
    (void)param;
    NW_SHIM_STUB("CreateSema", 0x00278ca0u);
    return 0;
}

int nw_ee_CreateThread(void *param)
{
    (void)param;
    NW_SHIM_STUB("CreateThread", 0x00278aa0u);
    return 0;
}

int nw_ee_DeleteThread(int thid)
{
    (void)thid;
    NW_SHIM_STUB("DeleteThread", 0x00278ab0u);
    return 0;
}

void nw_ee_FlushCache(int mode)
{
    (void)mode;
    NW_SHIM_STUB("FlushCache", 0x00278f20u);
}

int nw_ee_GetThreadId(void)
{
    NW_SHIM_STUB("GetThreadId", 0x00278b90u);
    return 0;
}

int nw_ee_PollSema(int sema_id)
{
    (void)sema_id;
    NW_SHIM_STUB("PollSema", 0x00278cf0u);
    return 0;
}

int nw_ee_ResumeThread(int thid)
{
    (void)thid;
    NW_SHIM_STUB("ResumeThread", 0x00278c30u);
    return 0;
}

int nw_ee_StartThread(int thid, void *arg)
{
    (void)thid, (void)arg;
    NW_SHIM_STUB("StartThread", 0x00278ac0u);
    return 0;
}

int nw_ee_TerminateThread(int thid)
{
    (void)thid;
    NW_SHIM_STUB("TerminateThread", 0x00278af0u);
    return 0;
}

int nw_ee_WaitSema(int sema_id)
{
    (void)sema_id;
    NW_SHIM_STUB("WaitSema", 0x00278ce0u);
    return 0;
}

int nw_ee_iSignalSema(int sema_id)
{
    (void)sema_id;
    NW_SHIM_STUB("iSignalSema", 0x00278cd0u);
    return 0;
}

int nw_ee_iSuspendThread(int thid)
{
    (void)thid;
    NW_SHIM_STUB("iSuspendThread", 0x00279cf0u);
    return 0;
}
