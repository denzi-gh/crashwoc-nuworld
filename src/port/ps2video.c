/* Replaces nups2/ps2video (port/units.toml): retail's logic on the virtual clock, not EE timers and interrupts. */
#include <string.h>

#include <SDL3/SDL.h>

#include "nucore/nuerror.h"
#include "nu3d/nuvport.h"
#include "gameplay/gameplay.h"
#include "nups2/ps2video.h"
#include "port/capture.h"
#include "port/dump.h"
#include "port/game.h"
#include "port/hz.h"
#include "port/level.h"
#include "port/ps2video.h"
#include "port/trace.h"
#include "shim/shim.h"

void *NuMemAllocFn(int size, char *file, int line);
void NuMemFreeFn(void *ptr, char *file, int line);
int sceGsSyncV(int mode);
int NuPs2ScreenSwap(int vsync);

/* Roots of the data image the decomp reads. */
extern volatile int nuvideo_global_vbcnt;
extern int PS2_REZ_W;
extern int PS2_REZ_H;
extern int PS2_VCNTR_X;
extern int PS2_VCNTR_Y;
extern int nusound_irx_loaded;
extern int ps2video_ignore_frameout;
extern int ps2video_novsync;

static char nw_ps2video_file[] = "ps2video.c";

/* INTC state of the two vblank interrupts; the clock thread shares vblank start (it counts it), vblank end belongs to
   the running EE thread alone. */
#define NW_INTC_HOOKED 0x1
#define NW_INTC_MASKED 0x2
#define NW_INTC_PENDING_COUNT 0x4 /* a masked edge not counted yet */
#define NW_INTC_PENDING_RUN 0x8   /* masked edges whose handlers have not run */

static SDL_AtomicInt nw_vbs_intc;
static int nw_vbe_intc;

static int nw_in_vblank;           /* in_vblank_end: inside a vblank interrupt */
static int nw_handlers_installed;
static int nw_vbe_handler;         /* vblank_end_handler */
static int nw_vbe_disable;         /* vblank_end_disable */
static int nw_vbs_handler;         /* vblank_start_handler */
static int nw_vbs_disable;         /* vblank_start_disable */
static struct NuVBlankHandler *nw_vbe_list; /* vblank_end_list */
static struct NuVBlankHandler *nw_vbs_list; /* vblank_start_list */
static int nw_auto_screen_swap;

/* ifnVBlankS without the count, which the clock thread or nw_intc_enable makes. */
static void nw_vbs_run(void)
{
    nw_in_vblank = 1;
    for (struct NuVBlankHandler *h = nw_vbs_list; h; h = h->next) {
        h->fn();
    }
    nw_in_vblank = 0;
}

/* ifnVBlankE */
static void nw_vbe_run(void)
{
    nw_in_vblank = 1;
    if (nw_auto_screen_swap) {
        NuPs2ScreenSwap(1);
    }
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

static int nw_vbs_clear(int bits)
{
    int old;
    do {
        old = SDL_GetAtomicInt(&nw_vbs_intc);
    } while (!SDL_CompareAndSwapAtomicInt(&nw_vbs_intc, old, old & ~bits));
    return old;
}

/* DisableIntc / EnableIntc of INTC 2 and 3: unmasking fires a pending edge at once unless inside an interrupt (the EE
   does not nest them); the return may switch threads between NuEnableVBlank's two unmasks, as on the PS2. */
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
            nw_ee_schedule();
        }
    } else {
        int old = nw_vbe_intc;
        nw_vbe_intc &= ~(NW_INTC_MASKED | (nw_in_vblank ? 0 : NW_INTC_PENDING_RUN));
        if ((old & NW_INTC_HOOKED) && (old & NW_INTC_PENDING_RUN) && !nw_in_vblank) {
            nw_vbe_run();
            nw_ee_schedule();
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
        nw_dump_vblank(nuvideo_global_vbcnt - (int)(edges - i) + 1);
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
        nw_trace_sound_vblank();
        nw_capture_vblank();
    }
    nw_ee_schedule();
}

/* ps2videoAddAllHandlers, vblank half only: the VIF1 and DMAC handlers have nothing to do on the host. */
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

/* EE timer 1 as NuPs2Init programs it (mode 0x83: H-blanks, 16 bits) in game time: PAL 15625 lines/s, NTSC 15734. */
static unsigned int nw_t1_count(void)
{
    return (unsigned int)(nw_game_time_ns() / nw_hz_line_ns()) & 0xffffu;
}

/* Retail's video state minus what only programs the GS. */
static unsigned int nw_video_size; /* ps2_video_size: lines per field */
static int nw_progressive_scan;    /* nuvideo_progressive_scan */
static int nw_field_parity;
static int nw_frame_count;         /* NuPs2FrameCount */
static unsigned int nw_swap_time;  /* NuPs2SwapTime */
static int nw_iop_initialised;

/* Timer bars: per slot, the H-lines between tbslotBegin and tbslotEnd, double-buffered. */
typedef struct TbSet {
    int values[16][2];
    int begin[16];
    int peak[16];
    int colour[16];
    int current[16];
    char *name[16];
} TbSet;

static TbSet *nw_tb_main_set;

/* The colours only matter to tbRender, which draws through the renderer (M3). */
static TbSet *nw_tbset_alloc(int *colours)
{
    TbSet *set = NuMemAllocFn(sizeof(TbSet), nw_ps2video_file, 0x1CF);
    if (set) {
        memset(set, 0, sizeof(TbSet));
        if (colours) {
            memcpy(set->colour, colours, sizeof(set->colour));
        }
    }
    return set;
}

struct TbSet *tbsetCreate(int *colours)
{
    return nw_tbset_alloc(colours);
}

void tbsetDestroy(TbSet *set)
{
    NuMemFreeFn(set, nw_ps2video_file, 0x1E7);
}

void tbslotReset(TbSet *set, int slot)
{
    set = set ? set : nw_tb_main_set;
    slot &= 0xF;
    int current = set->current[slot] ^ 1;
    set->current[slot] = current;
    set->values[slot][current ? 0 : 1] = 0;
}

int tbslotLastValue(TbSet *set, int slot)
{
    set = set ? set : nw_tb_main_set;
    slot &= 0xF;
    return set->values[slot][set->current[slot]];
}

void tbslotBeginFn(TbSet *set, int slot)
{
    set = set ? set : nw_tb_main_set;
    slot &= 0xF;
    set->begin[slot] = (int)nw_t1_count();
    set->name[slot] = 0;
}

void tbslotBeginExFn(TbSet *set, int slot, char *name)
{
    set = set ? set : nw_tb_main_set;
    slot &= 0xF;
    set->begin[slot] = (int)nw_t1_count();
    set->name[slot] = name;
}

/* Retail's wrap correction (adding 0xFFFF to a negative difference) comes out one line short; kept. */
int tbslotEndFn(void *tbset, int slot)
{
    TbSet *set = tbset ? tbset : nw_tb_main_set;
    slot &= 0xF;
    unsigned int elapsed = nw_t1_count() - (unsigned int)set->begin[slot];
    if (elapsed > 0xFFFF) {
        elapsed += 0xFFFF;
    }
    set->values[slot][1 - set->current[slot]] += (int)elapsed;
    return set->values[slot][1 - set->current[slot]];
}

void tbInit(void)
{
    nw_tb_main_set = nw_tbset_alloc(0);
}

/* The VIF, VU and DMA resets have nothing to reset on the host, and only one EE thread asks for its id. */
void NuPs2Init(void)
{
    nw_tb_main_set = nw_tbset_alloc(0);
}

/* No IOP modules: NUSOUND is src/audio/nusound.c behind the SIF shim, pads and memory cards have their own shims. */
void NuPs2InitIOP(void)
{
    if (!nw_iop_initialised) {
        nw_iop_initialised = 1;
        nusound_irx_loaded = 1;
    }
}

/* The mode and the render size; the GS has no registers to program. */
void NuPs2InitVideo(int width, int height, int psm, int zpsm, int format)
{
    (void)psm, (void)zpsm;
    switch (format) {
    case 0:
        nw_video_size = 480;
        nw_progressive_scan = 0;
        break;
    case 3:
    case 6:
        nw_video_size = 312;
        nw_progressive_scan = 0;
        break;
    case 4:
        nw_video_size = 312;
        height *= 2;
        nw_progressive_scan = 1;
        break;
    case 5:
        nw_video_size = 262;
        height *= 2;
        nw_progressive_scan = 1;
        break;
    default:
        nw_video_size = 262;
        nw_progressive_scan = 0;
        break;
    }
    PS2_VCNTR_X = 0x800;
    PS2_VCNTR_Y = 0x800;
    PS2_REZ_W = width;
    PS2_REZ_H = height;
    nw_field_parity = !sceGsSyncV(0);
    nw_frame_count = 0;
    NuIntAddAllHandlers();
}

/* Moves the picture on the TV (the display registers): nothing to move. */
void NuPs2VideoSetPos(int x, int y)
{
    (void)x, (void)y;
}

float NuPs2GetRenderWidth(void)
{
    return (float)PS2_REZ_W;
}

float NuPs2GetRenderHeight(void)
{
    return (float)PS2_REZ_H;
}

float NuPs2GetRenderXCentre(void)
{
    return (float)PS2_VCNTR_X;
}

float NuPs2GetRenderYCentre(void)
{
    return (float)PS2_VCNTR_Y;
}

void NuPs2GetViewport(struct nuviewport_s *vp)
{
    memset(vp, 0, sizeof(struct nuviewport_s));
    vp->zmin = 8388607.0f;
    vp->x = (PS2_VCNTR_X - PS2_REZ_W / 2) * 16;
    vp->y = (PS2_VCNTR_Y - PS2_REZ_H / 2) * 16;
    vp->width = PS2_REZ_W * 16;
    vp->height = PS2_REZ_H * 16;
    vp->zmax = 0.0f;
}

/* GS memory left after retail's 640-wide buffers, so a wider frame (port/wide.h) keeps the game's texture VRAM plan. */
void ps2videoGetFreeMem(int *pages, int *bytes)
{
    int used = (640 * PS2_REZ_H * 3 + 63) / 64;
    if (pages) {
        *pages = used;
    }
    if (bytes) {
        *bytes = 0x400000 - used * 256;
    }
}

/* Clears VRAM through the GIF: nothing to clear yet. */
void NuPs2FlushVRam(int r, int g, int b)
{
    (void)r, (void)g, (void)b;
}

/* Retail first waits for the GS to finish the frame in flight; the host has no GS. */
void NuPs2EnableAutoScreenSwap(void)
{
    nw_auto_screen_swap = 1;
}

void NuPs2DisableAutoScreenSwap(void)
{
    nw_auto_screen_swap = 0;
}

/* Returns whether the next frame draws with the field's half-line offset; the GS writes belong to the renderer (M3). */
int NuPs2ScreenSwap(int vsync)
{
    int half_offset;

    if (!nw_in_vblank) {
        tbslotEndFn(0, 0);
    }
    unsigned int swap_elapsed = nw_t1_count() - nw_swap_time;
    if (swap_elapsed > 0xFFFF) {
        swap_elapsed += 0xFFFF;
    }
    if (ps2video_ignore_frameout || nw_in_vblank) {
        swap_elapsed = 50;
    }
    if (vsync) {
        nw_frame_count++;
    } else {
        NuDebugMsgProlog(nw_ps2video_file, 0x921)("swap not");
    }
    if (!nw_in_vblank) {
        nw_field_parity = ps2video_novsync ? 0 : sceGsSyncV(0);
    } else {
        nw_field_parity = 1 - nw_field_parity;
    }
    half_offset = swap_elapsed <= nw_video_size && !nw_progressive_scan;
    if (vsync) {
        nw_game_swap();
        nw_gameplay_swap();
        nw_level_poll();
    } else {
        half_offset = 1;
    }
    nw_swap_time = nw_t1_count();
    return half_offset;
}

/* EE timer 0 as timeUserReset programs it (BUSCLK / 256, 16 bits); NuPPLoadBuffer times its reads with it. */
#define NW_T0_HZ 576000u

static Uint64 nw_t0_base;

void timeUserReset(void)
{
    nw_t0_base = nw_game_time_ns();
}

unsigned int timeUserRead(void)
{
    return (unsigned int)((nw_game_time_ns() - nw_t0_base) * NW_T0_HZ / SDL_NS_PER_SECOND) & 0xffffu;
}
