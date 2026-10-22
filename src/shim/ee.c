/* EE kernel: threads, semaphores, caches. The decomp calls them by their nw_ee_* names (port/nw_ee.h). The game
   thread is the EE's main thread; there are no other EE threads until the loader's scheduler (M4), so the
   background loader never starts and the game stays on its load screen. */
#include "port/game.h"
#include "shim/shim.h"

/* EE kernel error codes. */
#define NW_KE_ILLEGAL_THID (-406)
#define NW_KE_UNKNOWN_THID (-407)
#define NW_KE_UNKNOWN_SEMID (-408)
#define NW_KE_SEMA_ZERO (-419)
#define NW_KE_SEMA_OVF (-420)
#define NW_KE_ERROR (-1)

#define NW_EE_MAIN_THREAD 1
#define NW_EE_SEMAS 64

/* struct SemaParam of the EE kernel (game/bgload.c). */
typedef struct NwSemaParam {
    int count;
    int max_count;
    int init_count;
    int wait_threads;
    unsigned int attr;
    unsigned int option;
} NwSemaParam;

/* Ids are the index + 1: bgload takes 0 for "no semaphore". */
static struct {
    int used;
    int count;
    int max_count;
} nw_ee_semas[NW_EE_SEMAS];

/* bgload passes &_gp as the loader thread's gp register; nothing reads it (port/data.toml [owned]). */
int _gp;

static int nw_ee_thread_error(int thid)
{
    return thid == 0 ? NW_KE_ILLEGAL_THID : NW_KE_UNKNOWN_THID;
}

/* Priorities order EE threads; with one thread they mean nothing. The result (the old priority) is unread. */
int nw_ee_ChangeThreadPriority(int thid, int prio)
{
    (void)prio;
    return thid == NW_EE_MAIN_THREAD ? 0 : nw_ee_thread_error(thid);
}

int nw_ee_CreateSema(void *param)
{
    const NwSemaParam *p = param;
    for (int i = 0; i < NW_EE_SEMAS; i++) {
        if (!nw_ee_semas[i].used) {
            nw_ee_semas[i].used = 1;
            nw_ee_semas[i].count = p->init_count;
            nw_ee_semas[i].max_count = p->max_count;
            return i + 1;
        }
    }
    return NW_KE_ERROR;
}

static int nw_ee_sema(int sema_id)
{
    return sema_id >= 1 && sema_id <= NW_EE_SEMAS && nw_ee_semas[sema_id - 1].used ? sema_id - 1 : -1;
}

/* The loader thread (M4): 0 is "no thread" to bgload, so it neither waits for nor suspends one. */
int nw_ee_CreateThread(void *param)
{
    (void)param;
    NW_SHIM_STUB("CreateThread", 0x00278aa0u);
    return 0;
}

int nw_ee_DeleteThread(int thid)
{
    return nw_ee_thread_error(thid);
}

/* The EE's caches against DMA: the host has neither. */
void nw_ee_FlushCache(int mode)
{
    (void)mode;
}

int nw_ee_GetThreadId(void)
{
    return NW_EE_MAIN_THREAD;
}

int nw_ee_PollSema(int sema_id)
{
    int i = nw_ee_sema(sema_id);
    if (i < 0) {
        return NW_KE_UNKNOWN_SEMID;
    }
    if (nw_ee_semas[i].count == 0) {
        return NW_KE_SEMA_ZERO;
    }
    nw_ee_semas[i].count--;
    return sema_id;
}

int nw_ee_ResumeThread(int thid)
{
    return nw_ee_thread_error(thid);
}

int nw_ee_StartThread(int thid, void *arg)
{
    (void)arg;
    return nw_ee_thread_error(thid);
}

int nw_ee_TerminateThread(int thid)
{
    return nw_ee_thread_error(thid);
}

/* Only an interrupt handler can signal the main thread: wait for vblanks until one has. */
int nw_ee_WaitSema(int sema_id)
{
    int i = nw_ee_sema(sema_id);
    if (i < 0) {
        return NW_KE_UNKNOWN_SEMID;
    }
    while (nw_ee_semas[i].count == 0) {
        nw_game_yield(true);
    }
    nw_ee_semas[i].count--;
    return sema_id;
}

int nw_ee_iSignalSema(int sema_id)
{
    int i = nw_ee_sema(sema_id);
    if (i < 0) {
        return NW_KE_UNKNOWN_SEMID;
    }
    if (nw_ee_semas[i].count >= nw_ee_semas[i].max_count) {
        return NW_KE_SEMA_OVF;
    }
    nw_ee_semas[i].count++;
    return sema_id;
}

int nw_ee_iSuspendThread(int thid)
{
    return nw_ee_thread_error(thid);
}
