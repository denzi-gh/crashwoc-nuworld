/* The EE threads of src/shim/ee.c under the decomp's bgload, on a stepped clock and then on a real game thread. */
#include <stdio.h>
#include <string.h>

#include "port/game.h"
#include "shim/shim.h"

/* decomp game/bgload.c */
void BGLoadInit(void);
void BGLoadStart(void (*func)(void *));
void BGSuspendMain(void);
void BGLoadEnd(void);
extern volatile int bgload_complete;
extern int loadthread_id;

/* src/port/ps2video.c and its data image root */
void NuIntAddAllHandlers(void);
void NuEnableVBlank(void);
void NuDisableVBlank(void);
void NuEnableVBlankS(void);
void NuDisableVBlankS(void);
void NuEnableVBlankE(void);
void NuDisableVBlankE(void);
int sceGsSyncV(int mode);
extern volatile int nuvideo_global_vbcnt;

/* src/shim/ee.c */
int nw_ee_CreateThread(void *param);
int nw_ee_DeleteThread(int thid);
int nw_ee_GetThreadId(void);
int nw_ee_ResumeThread(int thid);
int nw_ee_StartThread(int thid, void *arg);
int nw_ee_TerminateThread(int thid);
int nw_ee_iSuspendThread(int thid);

struct ThreadParam {
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
};

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char order[64];
static int order_len;

static void mark(char c)
{
    if (order_len < (int)sizeof(order) - 1) {
        order[order_len++] = c;
        order[order_len] = 0;
    }
}

static void reset_order(void)
{
    order_len = 0;
    order[0] = 0;
}

/* Two steps of a vblank each, then retail's final spin. */
static void loader_steps(void *arg)
{
    (void)arg;
    CHECK(nw_ee_GetThreadId() == loadthread_id);
    mark('L');
    nw_ee_idle();
    mark('L');
    nw_ee_idle();
    mark('C');
    bgload_complete = 1;
    for (;;) {
        nw_ee_idle();
    }
}

static void loader_returns(void *arg)
{
    (void)arg;
    mark('R');
}

static void loader_idle(void *arg)
{
    (void)arg;
    for (;;) {
        nw_ee_idle();
    }
}

/* BGLoadSfx's frame loop. */
static void frames_until_complete(void)
{
    while (!bgload_complete) {
        mark('M');
        BGSuspendMain();
    }
}

static void complete(void)
{
    bgload_complete = 1;
    for (;;) {
        nw_ee_idle();
    }
}

/* A vblank passes without a wait; the yield point delivers it. */
static void vblank_passes(void)
{
    nw_game_wait_edge(nw_game_edges(), 0);
    nw_ee_yield();
}

/* Masked across two vblanks: the switch waits for the unmask, where VBlankFn runs once. */
static void loader_masked(void *arg)
{
    (void)arg;
    mark('L');
    NuDisableVBlank();
    mark('[');
    vblank_passes();
    vblank_passes();
    mark('|');
    NuEnableVBlank();
    mark(']');
    complete();
}

/* Nested: only the outermost unmask switches. */
static void loader_nested(void *arg)
{
    (void)arg;
    mark('L');
    NuDisableVBlank();
    NuDisableVBlankS();
    vblank_passes();
    NuEnableVBlankS();
    mark('|');
    NuEnableVBlank();
    mark(']');
    complete();
}

/* Vblank end alone masked (NuDebugMsgFunction): vblank start still switches, as on the PS2. */
static void loader_end_masked(void *arg)
{
    (void)arg;
    mark('L');
    NuDisableVBlankE();
    vblank_passes();
    mark('e');
    NuEnableVBlankE();
    complete();
}

static void test_bgload(void)
{
    NuIntAddAllHandlers();
    BGLoadInit();
    CHECK(nw_ee_GetThreadId() == 1);

    /* The loader's priority is the lower one: starting it runs nothing yet. */
    uint64_t edges = nw_game_edges(), swaps = nw_game_swaps();
    BGLoadStart(loader_steps);
    CHECK(loadthread_id > 1);
    CHECK(order_len == 0);

    /* Main waits in BGSuspendMain; the loader runs until VBlankFn suspends it, swaps and wakes main. */
    frames_until_complete();
    CHECK(strcmp(order, "MLMLMC") == 0);
    CHECK(nw_game_edges() == edges + 3);
    CHECK(nw_game_swaps() == swaps + 3);
    CHECK(nw_ee_GetThreadId() == 1);

    /* BGLoadEnd ends the loader in its spin; then VBlankFn only wakes main. */
    BGLoadEnd();
    CHECK(loadthread_id == 0);
    edges = nw_game_edges();
    swaps = nw_game_swaps();
    BGSuspendMain();
    CHECK(nw_game_edges() == edges + 1);
    CHECK(nw_game_swaps() == swaps);

    /* A loader that returns is dormant: BGLoadEnd's TerminateThread fails, its DeleteThread joins it. */
    reset_order();
    BGLoadStart(loader_returns);
    BGSuspendMain();
    CHECK(strcmp(order, "R") == 0);
    BGLoadEnd();

    /* Ended before it ever ran. */
    BGLoadStart(loader_steps);
    BGLoadEnd();
    CHECK(strcmp(order, "R") == 0);

    /* The slots are free again. */
    BGLoadStart(loader_steps);
    frames_until_complete();
    BGLoadEnd();
    CHECK(strcmp(order, "RMLMLMC") == 0);
}

static void run_load(void (*loader)(void *))
{
    reset_order();
    BGLoadStart(loader);
    frames_until_complete();
    BGLoadEnd();
}

static void test_masks(void)
{
    uint64_t edges = nw_game_edges(), swaps = nw_game_swaps();
    int vbcnt = nuvideo_global_vbcnt;
    run_load(loader_masked);
    CHECK(strcmp(order, "ML[|M]") == 0);
    CHECK(nw_game_edges() == edges + 3);
    CHECK(nw_game_swaps() == swaps + 2);
    CHECK(nuvideo_global_vbcnt == vbcnt + 2);

    run_load(loader_nested);
    CHECK(strcmp(order, "ML|M]") == 0);

    run_load(loader_end_masked);
    CHECK(strcmp(order, "MLMe") == 0);

    /* Main masked across a vblank: the ready loader does not run. */
    reset_order();
    BGLoadStart(loader_steps);
    NuDisableVBlank();
    sceGsSyncV(0);
    NuEnableVBlank();
    CHECK(order_len == 0);
    BGLoadEnd();
}

static void test_states(void)
{
    struct ThreadParam param = { .entry = loader_idle, .init_priority = 2 };
    CHECK(nw_ee_ResumeThread(0) == -406);
    CHECK(nw_ee_ResumeThread(9) == -407);
    int id = nw_ee_CreateThread(&param);
    CHECK(id > 1);
    CHECK(nw_ee_iSuspendThread(id) == -413);
    CHECK(nw_ee_StartThread(id, NULL) == id);
    CHECK(nw_ee_StartThread(id, NULL) == -414);
    CHECK(nw_ee_ResumeThread(id) == -415);
    CHECK(nw_ee_iSuspendThread(id) == id);
    CHECK(nw_ee_ResumeThread(id) == id);
    CHECK(nw_ee_DeleteThread(id) == -414);
    CHECK(nw_ee_TerminateThread(1) == -406);
    unsigned terminated = nw_ee_terminated(NULL, 0);
    CHECK(nw_ee_TerminateThread(id) == id);
    CHECK(nw_ee_terminated(NULL, 0) == terminated + 1);
    CHECK(nw_ee_TerminateThread(id) == -413);
    CHECK(nw_ee_DeleteThread(id) == id);
    CHECK(nw_ee_DeleteThread(id) == -407);
}

/* Loads forever (BGLoadInit ran in test_bgload). */
static int loading_entry(int argc, char **argv)
{
    (void)argc, (void)argv;
    for (;;) {
        BGLoadStart(loader_idle);
        for (int i = 0; i < 3; i++) {
            BGSuspendMain();
        }
        BGLoadEnd();
    }
}

int main(void)
{
    CHECK(nw_game_init(NW_CLOCK_STEPPED, 50));
    test_bgload();
    test_masks();
    test_states();
    nw_game_shutdown();

    /* A real game thread with a loader parks when asked, whichever EE thread holds the baton. */
    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(loading_entry, 0, NULL));
    CHECK(nw_game_wait_edge(nw_game_wait_edge(0, 2000), 2000) >= 2);
    CHECK(nw_game_stop(2000) == NW_GAME_PARKED);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ee: all checks passed\n");
    return 0;
}
