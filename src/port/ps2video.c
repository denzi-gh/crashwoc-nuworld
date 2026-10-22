/* nups2/ps2video, which the port replaces (port/units.toml): the parts the decomp reaches so far, retail's logic
   on the virtual clock (src/port/game.c) instead of the EE's timers and interrupts. */
#include <SDL3/SDL.h>

#include "nups2/ps2video.h"
#include "port/ps2video.h"

/* Roots of the data image the decomp reads. */
extern volatile int nuvideo_global_vbcnt;

/* The two vblank interrupts as the INTC sees them: hooked, masked (DisableIntc), pending (fired while masked).
   Vblank start is shared with the clock thread, which counts it; vblank end is the game thread's alone. */
#define NW_INTC_HOOKED 0x1
#define NW_INTC_MASKED 0x2
#define NW_INTC_PENDING_COUNT 0x4 /* a masked edge not counted yet */
#define NW_INTC_PENDING_RUN 0x8   /* masked edges whose handlers have not run */

static SDL_AtomicInt nw_vbs_intc;
static int nw_vbe_intc;

static int nw_in_vblank;           /* in_vblank_end: inside a vblank interrupt */
static int nw_handlers_installed;  /* handlers_installed */
static int nw_vbe_handler;         /* vblank_end_handler */
static int nw_vbe_disable;         /* vblank_end_disable */
static int nw_vbs_handler;         /* vblank_start_handler */
static int nw_vbs_disable;         /* vblank_start_disable */
static struct NuVBlankHandler *nw_vbe_list; /* vblank_end_list */
static struct NuVBlankHandler *nw_vbs_list; /* vblank_start_list */

/* ifnVBlankS without the count (the clock thread made it, or nw_vbs_enable). */
static void nw_vbs_run(void)
{
    nw_in_vblank = 1;
    for (struct NuVBlankHandler *h = nw_vbs_list; h; h = h->next) {
        h->fn();
    }
    nw_in_vblank = 0;
}

static void nw_vbe_run(void)
{
    nw_in_vblank = 1;
    for (struct NuVBlankHandler *h = nw_vbe_list; h; h = h->next) {
        h->fn();
    }
    nw_in_vblank = 0;
}

static void nw_vbs_or(int bits)
{
    int old;
    do {
        old = SDL_GetAtomicInt(&nw_vbs_intc);
    } while (!SDL_CompareAndSwapAtomicInt(&nw_vbs_intc, old, old | bits));
}

/* Clears `bits`; returns the state before. */
static int nw_vbs_clear(int bits)
{
    int old;
    do {
        old = SDL_GetAtomicInt(&nw_vbs_intc);
    } while (!SDL_CompareAndSwapAtomicInt(&nw_vbs_intc, old, old & ~bits));
    return old;
}

/* DisableIntc / EnableIntc of INTC 2 and 3. Unmasking with an edge pending fires it at once, unless already
   inside an interrupt (the EE does not nest them): then the next yield point runs it. */
static void nw_intc_disable(int cause)
{
    if (cause == 2) {
        nw_vbs_or(NW_INTC_MASKED);
    } else {
        nw_vbe_intc |= NW_INTC_MASKED;
    }
}

static void nw_intc_enable(int cause)
{
    if (cause == 2) {
        int old = nw_vbs_clear(NW_INTC_MASKED | NW_INTC_PENDING_COUNT | (nw_in_vblank ? 0 : NW_INTC_PENDING_RUN));
        if ((old & NW_INTC_HOOKED) && (old & NW_INTC_PENDING_COUNT)) {
            __atomic_fetch_add(&nuvideo_global_vbcnt, 1, __ATOMIC_RELAXED);
        }
        if ((old & NW_INTC_HOOKED) && (old & NW_INTC_PENDING_RUN) && !nw_in_vblank) {
            nw_vbs_run();
        }
    } else {
        int old = nw_vbe_intc;
        nw_vbe_intc &= ~(NW_INTC_MASKED | (nw_in_vblank ? 0 : NW_INTC_PENDING_RUN));
        if ((old & NW_INTC_HOOKED) && (old & NW_INTC_PENDING_RUN) && !nw_in_vblank) {
            nw_vbe_run();
        }
    }
}

void nw_vblank_count(void *user, unsigned edges)
{
    (void)user;
    for (;;) {
        int old = SDL_GetAtomicInt(&nw_vbs_intc);
        if (!(old & NW_INTC_HOOKED)) {
            return;
        }
        if (!(old & NW_INTC_MASKED)) {
            __atomic_fetch_add(&nuvideo_global_vbcnt, (int)edges, __ATOMIC_RELAXED);
            return;
        }
        if (SDL_CompareAndSwapAtomicInt(&nw_vbs_intc, old, old | NW_INTC_PENDING_COUNT | NW_INTC_PENDING_RUN)) {
            return;
        }
    }
}

void nw_vblank_deliver(unsigned edges)
{
    if (nw_in_vblank) {
        return;
    }
    for (unsigned i = 0; i < edges; i++) {
        int vbs = SDL_GetAtomicInt(&nw_vbs_intc);
        if (vbs & NW_INTC_HOOKED) {
            if (vbs & NW_INTC_MASKED) {
                nw_vbs_or(NW_INTC_PENDING_RUN);
            } else {
                nw_vbs_run();
            }
        }
        if (nw_vbe_intc & NW_INTC_HOOKED) {
            if (nw_vbe_intc & NW_INTC_MASKED) {
                nw_vbe_intc |= NW_INTC_PENDING_RUN;
            } else {
                nw_vbe_run();
            }
        }
    }
}

/* ps2videoAddAllHandlers, the vblank half (the VIF1 and DMAC handlers have nothing to do on the host). */
void NuIntAddAllHandlers(void)
{
    if (!nw_handlers_installed) {
        nw_vbe_list = 0;
        nw_vbe_intc = NW_INTC_HOOKED;
        nw_vbe_handler = 1;
        nw_vbs_list = 0;
        SDL_SetAtomicInt(&nw_vbs_intc, NW_INTC_HOOKED);
        nw_vbs_handler = 1;
        nw_handlers_installed = 1;
    }
}

void NuIntRemoveAllHandlers(void)
{
    if (nw_handlers_installed) {
        nw_vbe_handler = 0;
        nw_vbs_handler = 0;
        SDL_SetAtomicInt(&nw_vbs_intc, 0);
        nw_vbe_intc = 0;
        nw_vbe_list = 0;
        nw_vbs_list = 0;
        nw_handlers_installed = 0;
    }
}

void NuEnableVBlankE(void)
{
    if (nw_vbe_handler) {
        if (--nw_vbe_disable == 0) {
            nw_intc_enable(3);
        }
    }
}

void NuDisableVBlankE(void)
{
    if (nw_vbe_handler) {
        if (nw_vbe_disable == 0) {
            nw_intc_disable(3);
        }
        nw_vbe_disable++;
    }
}

static void nw_vblank_insert(struct NuVBlankHandler **list, struct NuVBlankHandler *handler)
{
    struct NuVBlankHandler *prev = 0, *cur = *list;
    while (cur && cur->priority <= handler->priority) {
        prev = cur;
        cur = cur->next;
    }
    if (prev) {
        handler->next = prev->next;
        handler->prev = prev;
        prev->next = handler;
        if (handler->next) {
            handler->next->prev = handler;
        }
    } else {
        if (*list) {
            (*list)->prev = handler;
        }
        handler->prev = 0;
        handler->next = *list;
        *list = handler;
    }
}

static void nw_vblank_remove(struct NuVBlankHandler **list, struct NuVBlankHandler *handler)
{
    if (handler->prev) {
        handler->prev->next = handler->next;
    } else {
        *list = handler->next;
    }
    if (handler->next) {
        handler->next->prev = handler->prev;
    }
}

void NuIntAddVBlankE(struct NuVBlankHandler *handler)
{
    nw_intc_disable(3);
    nw_vblank_insert(&nw_vbe_list, handler);
    nw_intc_enable(3);
}

void NuIntRemVBlankE(struct NuVBlankHandler *handler)
{
    nw_intc_disable(3);
    nw_vblank_remove(&nw_vbe_list, handler);
    nw_intc_enable(3);
}

void NuEnableVBlankS(void)
{
    if (nw_vbs_handler) {
        if (--nw_vbs_disable == 0) {
            nw_intc_enable(2);
        }
    }
}

void NuDisableVBlankS(void)
{
    if (nw_vbs_handler) {
        if (nw_vbs_disable == 0) {
            nw_intc_disable(2);
        }
        nw_vbs_disable++;
    }
}

void NuIntAddVBlankS(struct NuVBlankHandler *handler)
{
    nw_intc_disable(2);
    nw_vblank_insert(&nw_vbs_list, handler);
    nw_intc_enable(2);
}

void NuIntRemVBlankS(struct NuVBlankHandler *handler)
{
    nw_intc_disable(2);
    nw_vblank_remove(&nw_vbs_list, handler);
    nw_intc_enable(2);
}

void NuEnableVBlank(void)
{
    NuEnableVBlankS();
    NuEnableVBlankE();
}

void NuDisableVBlank(void)
{
    NuDisableVBlankE();
    NuDisableVBlankS();
}

/* EE timer 0 as timeUserReset programs it (BUSCLK / 256 = 576 kHz, 16 bits); NuPPLoadBuffer times its reads
   with it. */
#define NW_T0_HZ 576000u

static Uint64 nw_t0_base;

void timeUserReset(void)
{
    nw_t0_base = SDL_GetTicksNS();
}

unsigned int timeUserRead(void)
{
    return (unsigned int)((SDL_GetTicksNS() - nw_t0_base) * NW_T0_HZ / SDL_NS_PER_SECOND) & 0xffffu;
}
