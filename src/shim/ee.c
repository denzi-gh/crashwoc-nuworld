/* EE kernel: threads, semaphores, caches. The decomp calls them by their nw_ee_* names (port/nw_ee.h). Each EE
   thread is a host thread; they pass a baton, so one runs at a time and switches only where the EE kernel would:
   in a call that blocks or readies a thread, and on the return from an interrupt (nw_ee_schedule). */
#include <setjmp.h>
#include <stdbool.h>

#include <SDL3/SDL.h>

#include "port/game.h"
#include "shim/shim.h"

/* EE kernel error codes. */
#define NW_KE_ILLEGAL_THID (-406)
#define NW_KE_UNKNOWN_THID (-407)
#define NW_KE_UNKNOWN_SEMID (-408)
#define NW_KE_DORMANT (-413)
#define NW_KE_NOT_DORMANT (-414)
#define NW_KE_NOT_SUSPEND (-415)
#define NW_KE_SEMA_ZERO (-419)
#define NW_KE_SEMA_OVF (-420)
#define NW_KE_ERROR (-1)

#define NW_EE_THREADS 4
#define NW_EE_SEMAS 64
#define NW_EE_ENDS 16 /* the terminations whose vblank is kept */

/* struct ThreadParam of the EE kernel (game/bgload.c). */
typedef struct NwThreadParam {
    int status;
    void (*entry)(void *);
    void *stack;
    int stack_size;
    void *gp_reg;
    int init_priority;
    int current_priority;
    unsigned int attr;
    unsigned int option;
    int wait_type;
    int wait_id;
    int wakeup_count;
} NwThreadParam;

/* struct SemaParam of the EE kernel (game/bgload.c). */
typedef struct NwSemaParam {
    int count;
    int max_count;
    int init_count;
    int wait_threads;
    unsigned int attr;
    unsigned int option;
} NwSemaParam;

typedef enum NwEeStatus {
    NW_EE_DORMANT,
    NW_EE_READY, /* or running */
    NW_EE_WAIT,
    NW_EE_SUSPEND,
    NW_EE_WAITSUSPEND,
} NwEeStatus;

typedef struct NwEeThread {
    bool used;
    NwEeStatus status;
    int prio;
    int sema; /* the index it waits on */
    void (*entry)(void *);
    void *arg;
    SDL_Thread *host;
    bool kill; /* under lock */
    jmp_buf exit;
} NwEeThread;

/* Thread ids are the slot + 1; slot 0 is the main thread, running from the start. Only the thread holding the
   baton changes thread states; the hand-over goes through the lock. */
static struct {
    NwEeThread threads[NW_EE_THREADS];
    int current;  /* under lock: the slot holding the baton */
    bool idling;  /* nobody is ready: the interrupts the idle loop delivers do not reschedule */
    SDL_Mutex *lock;
    SDL_Condition *cond;
    SDL_AtomicInt terminated;
    uint64_t terminated_at[NW_EE_ENDS];
    unsigned park_after;
} nw_ee = { .threads = { { .used = true, .status = NW_EE_READY, .prio = 1 } } };

/* Ids are the index + 1: bgload takes 0 for "no semaphore". */
static struct {
    int used;
    int count;
    int max_count;
} nw_ee_semas[NW_EE_SEMAS];

/* bgload passes &_gp as the loader thread's gp register; nothing reads it (port/data.toml [owned]). */
int _gp;

static NwEeThread *nw_ee_thread(int thid, int *error)
{
    if (thid >= 1 && thid <= NW_EE_THREADS && nw_ee.threads[thid - 1].used) {
        return &nw_ee.threads[thid - 1];
    }
    *error = thid == 0 ? NW_KE_ILLEGAL_THID : NW_KE_UNKNOWN_THID;
    return NULL;
}

/* The ready thread of the highest priority (lowest number); the running one keeps the CPU on a tie. */
static int nw_ee_pick(void)
{
    int best = nw_ee.threads[nw_ee.current].status == NW_EE_READY ? nw_ee.current : -1;
    for (int i = 0; i < NW_EE_THREADS; i++) {
        const NwEeThread *t = &nw_ee.threads[i];
        if (t->used && t->status == NW_EE_READY && (best < 0 || t->prio < nw_ee.threads[best].prio)) {
            best = i;
        }
    }
    return best;
}

/* Under lock. A terminated thread leaves its stack instead, as the EE drops it. */
static void nw_ee_wait_baton(int self)
{
    NwEeThread *t = &nw_ee.threads[self];
    while (nw_ee.current != self && !t->kill) {
        SDL_WaitCondition(nw_ee.cond, nw_ee.lock);
    }
    if (t->kill) {
        SDL_UnlockMutex(nw_ee.lock);
        longjmp(t->exit, 1);
    }
}

static void nw_ee_switch(int next, bool wait)
{
    int self = nw_ee.current;
    SDL_LockMutex(nw_ee.lock);
    nw_ee.current = next;
    SDL_BroadcastCondition(nw_ee.cond);
    if (wait) {
        nw_ee_wait_baton(self);
    }
    SDL_UnlockMutex(nw_ee.lock);
}

/* With nobody ready, waits for vblanks until their interrupts ready a thread. */
static void nw_ee_run_next(bool wait)
{
    for (;;) {
        int next = nw_ee_pick();
        if (next >= 0) {
            if (next != nw_ee.current) {
                nw_ee_switch(next, wait);
            }
            return;
        }
        nw_ee.idling = true;
        nw_game_yield(true);
        nw_ee.idling = false;
    }
}

void nw_ee_schedule(void)
{
    if (!nw_ee.idling) {
        nw_ee_run_next(true);
    }
}

void nw_ee_yield(void)
{
    if (nw_ee.current != 0) {
        nw_game_yield(false);
    }
}

void nw_ee_idle(void)
{
    nw_game_yield(true);
}

void nw_ee_park_after(unsigned n)
{
    nw_ee.park_after = n;
}

unsigned nw_ee_terminated(uint64_t *at, unsigned max)
{
    unsigned n = (unsigned)SDL_GetAtomicInt(&nw_ee.terminated);
    for (unsigned i = 0; i < n && i < max && i < NW_EE_ENDS; i++) {
        at[i] = nw_ee.terminated_at[i];
    }
    return n;
}

static int SDLCALL nw_ee_host_run(void *arg)
{
    NwEeThread *t = arg;
    int self = (int)(t - nw_ee.threads);
    nw_game_host_enter();
    if (setjmp(t->exit) == 0) {
        SDL_LockMutex(nw_ee.lock);
        nw_ee_wait_baton(self);
        SDL_UnlockMutex(nw_ee.lock);
        t->entry(t->arg);
        t->status = NW_EE_DORMANT;
        nw_ee_run_next(false);
    }
    return 0;
}

/* The result (the old priority) is unread. */
int nw_ee_ChangeThreadPriority(int thid, int prio)
{
    int error;
    NwEeThread *t = nw_ee_thread(thid, &error);
    if (!t) {
        return error;
    }
    int old = t->prio;
    t->prio = prio;
    nw_ee_schedule();
    return old;
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

int nw_ee_CreateThread(void *param)
{
    const NwThreadParam *p = param;
    for (int i = 1; i < NW_EE_THREADS; i++) {
        NwEeThread *t = &nw_ee.threads[i];
        if (!t->used) {
            *t = (NwEeThread){ .used = true, .status = NW_EE_DORMANT, .prio = p->init_priority, .entry = p->entry };
            return i + 1;
        }
    }
    return NW_KE_ERROR;
}

int nw_ee_DeleteThread(int thid)
{
    int error;
    NwEeThread *t = nw_ee_thread(thid, &error);
    if (!t) {
        return error;
    }
    if (t->status != NW_EE_DORMANT || t == &nw_ee.threads[nw_ee.current]) {
        return NW_KE_NOT_DORMANT;
    }
    if (t->host) {
        SDL_WaitThread(t->host, NULL);
    }
    *t = (NwEeThread){ 0 };
    return thid;
}

/* The EE's caches against DMA: the host has neither. */
void nw_ee_FlushCache(int mode)
{
    (void)mode;
}

int nw_ee_GetThreadId(void)
{
    return nw_ee.current + 1;
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
    int error;
    NwEeThread *t = nw_ee_thread(thid, &error);
    if (!t) {
        return error;
    }
    if (t->status == NW_EE_SUSPEND) {
        t->status = NW_EE_READY;
    } else if (t->status == NW_EE_WAITSUSPEND) {
        t->status = NW_EE_WAIT;
    } else {
        return NW_KE_NOT_SUSPEND;
    }
    nw_ee_schedule();
    return thid;
}

int nw_ee_StartThread(int thid, void *arg)
{
    int error;
    NwEeThread *t = nw_ee_thread(thid, &error);
    if (!t) {
        return error;
    }
    if (t->status != NW_EE_DORMANT) {
        return NW_KE_NOT_DORMANT;
    }
    if (!nw_ee.lock) {
        nw_ee.lock = SDL_CreateMutex();
        nw_ee.cond = SDL_CreateCondition();
        if (!nw_ee.lock || !nw_ee.cond) {
            nw_game_fatal("EE threads: no mutex or condition");
        }
    }
    if (t->host) {
        SDL_WaitThread(t->host, NULL);
    }
    t->arg = arg;
    t->status = NW_EE_READY;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)nw_ee_host_run);
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, t);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "nw_ee");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, NW_GAME_STACK);
    t->host = props ? SDL_CreateThreadWithProperties(props) : NULL;
    SDL_DestroyProperties(props);
    if (!t->host) {
        t->status = NW_EE_DORMANT;
        return NW_KE_ERROR;
    }
    nw_ee_schedule();
    return thid;
}

/* The thread's host thread leaves its stack (as the EE drops it) and is joined, so the slot is free again. */
int nw_ee_TerminateThread(int thid)
{
    int error;
    NwEeThread *t = nw_ee_thread(thid, &error);
    if (!t) {
        return error;
    }
    if (t == &nw_ee.threads[nw_ee.current]) {
        return NW_KE_ILLEGAL_THID;
    }
    if (t->status == NW_EE_DORMANT) {
        return NW_KE_DORMANT;
    }
    t->status = NW_EE_DORMANT;
    SDL_LockMutex(nw_ee.lock);
    t->kill = true;
    SDL_BroadcastCondition(nw_ee.cond);
    SDL_UnlockMutex(nw_ee.lock);
    SDL_WaitThread(t->host, NULL);
    t->host = NULL;
    t->kill = false;
    unsigned n = (unsigned)SDL_GetAtomicInt(&nw_ee.terminated);
    if (n < NW_EE_ENDS) {
        nw_ee.terminated_at[n] = nw_game_edges();
    }
    SDL_SetAtomicInt(&nw_ee.terminated, (int)n + 1);
    if (n + 1 == nw_ee.park_after) {
        nw_game_request_stop();
    }
    return thid;
}

int nw_ee_WaitSema(int sema_id)
{
    int i = nw_ee_sema(sema_id);
    if (i < 0) {
        return NW_KE_UNKNOWN_SEMID;
    }
    if (nw_ee_semas[i].count > 0) {
        nw_ee_semas[i].count--;
        return sema_id;
    }
    NwEeThread *t = &nw_ee.threads[nw_ee.current];
    t->status = NW_EE_WAIT;
    t->sema = i;
    nw_ee_schedule();
    return sema_id;
}

/* A waiting thread takes the signal; the count rises only without one. */
int nw_ee_iSignalSema(int sema_id)
{
    int i = nw_ee_sema(sema_id);
    if (i < 0) {
        return NW_KE_UNKNOWN_SEMID;
    }
    NwEeThread *waiter = NULL;
    for (int j = 0; j < NW_EE_THREADS; j++) {
        NwEeThread *t = &nw_ee.threads[j];
        if (t->used && (t->status == NW_EE_WAIT || t->status == NW_EE_WAITSUSPEND) && t->sema == i &&
            (!waiter || t->prio < waiter->prio)) {
            waiter = t;
        }
    }
    if (waiter) {
        waiter->status = waiter->status == NW_EE_WAIT ? NW_EE_READY : NW_EE_SUSPEND;
        return sema_id;
    }
    if (nw_ee_semas[i].count >= nw_ee_semas[i].max_count) {
        return NW_KE_SEMA_OVF;
    }
    nw_ee_semas[i].count++;
    return sema_id;
}

/* VBlankFn suspends the thread it interrupted: it stops at the return from the interrupt. */
int nw_ee_iSuspendThread(int thid)
{
    int error;
    NwEeThread *t = nw_ee_thread(thid, &error);
    if (!t) {
        return error;
    }
    if (t->status == NW_EE_READY) {
        t->status = NW_EE_SUSPEND;
    } else if (t->status == NW_EE_WAIT) {
        t->status = NW_EE_WAITSUSPEND;
    } else {
        return t->status == NW_EE_DORMANT ? NW_KE_DORMANT : NW_KE_ERROR;
    }
    return thid;
}
