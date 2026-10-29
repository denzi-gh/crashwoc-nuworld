/* The vblank clock (src/platform/clock.c), the game thread's yield points (src/port/game.c), the vblank interrupts
   of src/port/ps2video.c and the EE semaphores of src/shim/ee.c. The test's thread acts as the game thread on a
   stepped clock until the last check, which starts a real one and parks it. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nups2/ps2video.h"
#include "platform/clock.h"
#include "port/game.h"

/* The data image root the vblank start interrupt counts. */
extern volatile int nuvideo_global_vbcnt;

/* src/port/ps2video.c (no header declares them). */
void NuIntAddAllHandlers(void);
void NuIntRemoveAllHandlers(void);
void NuEnableVBlank(void);
void NuDisableVBlank(void);
void NuEnableVBlankS(void);
void NuDisableVBlankS(void);

/* src/shim/ee.c */
int nw_ee_CreateSema(void *param);
int nw_ee_PollSema(int sema_id);
int nw_ee_WaitSema(int sema_id);
int nw_ee_iSignalSema(int sema_id);
int nw_ee_StartThread(int thid, void *arg);
int nw_ee_GetThreadId(void);
int sceGsSyncV(int mode);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#define MS 1000000ull

static void test_due(void)
{
    NwClockTiming t = { .t0 = 1000 * MS, .period = 20 * MS, .k = 0 };
    CHECK(nw_clock_deadline(&t) == 1020 * MS);
    CHECK(nw_clock_due(&t, 500 * MS) == 0);
    CHECK(nw_clock_due(&t, 1019 * MS) == 0);
    CHECK(nw_clock_due(&t, 1020 * MS) == 1);
    CHECK(nw_clock_due(&t, 1020 * MS) == 0);
    CHECK(nw_clock_deadline(&t) == 1040 * MS);
    /* Late but within the clamp: every edge, on the original grid. */
    CHECK(nw_clock_due(&t, 1085 * MS) == 3);
    CHECK(nw_clock_deadline(&t) == 1100 * MS);
    /* A stall: one edge, and the grid restarts. */
    CHECK(nw_clock_due(&t, 2000 * MS) == 1);
    CHECK(nw_clock_deadline(&t) == 2020 * MS);
    CHECK(nw_clock_due(&t, 2019 * MS) == 0);
    CHECK(nw_clock_due(&t, 2080 * MS) == 4);
}

static void test_pacer(void)
{
    CHECK(nw_pacer_next(0, 1000 * MS, 10 * MS) == 1010 * MS);
    /* On time or a little late: the next start on the grid. */
    CHECK(nw_pacer_next(1010 * MS, 1010 * MS, 10 * MS) == 1020 * MS);
    CHECK(nw_pacer_next(1020 * MS, 1027 * MS, 10 * MS) == 1030 * MS);
    /* A period late: the grid starts again from now. */
    CHECK(nw_pacer_next(1030 * MS, 1045 * MS, 10 * MS) == 1055 * MS);
}

static unsigned edge_calls, edge_total;

static void count_edges(void *user, unsigned edges)
{
    (void)user;
    edge_calls++;
    edge_total += edges;
}

static void test_stepped(void)
{
    NwClock c;
    CHECK(nw_clock_start(&c, NW_CLOCK_STEPPED, 50, count_edges, NULL));
    CHECK(nw_clock_edges(&c) == 0);
    nw_clock_step(&c, 3);
    CHECK(nw_clock_edges(&c) == 3 && edge_calls == 1 && edge_total == 3);
    /* A wait for a new edge emits it; a wait for an old one returns at once. */
    CHECK(nw_clock_wait(&c, 3, -1) == 4);
    CHECK(nw_clock_wait(&c, 1, -1) == 4);
    CHECK(edge_total == 4);
    /* A watch never emits one. */
    CHECK(nw_clock_watch(&c, 4, 0) == 4 && edge_total == 4);
    nw_clock_stop(&c);
}

static void test_realtime(void)
{
    NwClock c;
    edge_total = 0;
    CHECK(nw_clock_start(&c, NW_CLOCK_REALTIME, 50, count_edges, NULL));
    uint64_t seen = 0;
    for (int i = 0; i < 3; i++) {
        seen = nw_clock_wait(&c, seen, 2000);
    }
    CHECK(seen >= 3);
    nw_clock_stop(&c);
    CHECK(edge_total >= seen);
}

static char order[64];
static size_t order_len;

static void mark(char c)
{
    if (order_len + 1 < sizeof(order)) {
        order[order_len++] = c;
        order[order_len] = 0;
    }
}

static void fn_a(void)
{
    mark('a');
}

static void fn_b(void)
{
    mark('b');
}

static void fn_c(void)
{
    mark('c');
}

static int sema;

static void fn_signal(void)
{
    mark('s');
    nw_ee_iSignalSema(sema);
}

static void reset_order(void)
{
    order_len = 0;
    order[0] = 0;
}

static struct NuVBlankHandler h_a = { 0, 0, fn_a, 5 };
static struct NuVBlankHandler h_b = { 0, 0, fn_b, 1 };
static struct NuVBlankHandler h_c = { 0, 0, fn_c, 5 };
static struct NuVBlankHandler h_s = { 0, 0, fn_signal, 9 };

static void test_vblank(void)
{
    int vbcnt = nuvideo_global_vbcnt;
    /* Not hooked yet: an edge counts nothing and runs nothing. */
    NuIntAddVBlankS(&h_a);
    nw_game_yield(true);
    CHECK(nuvideo_global_vbcnt == vbcnt);
    CHECK(order_len == 0);

    /* Hooking clears the lists, as retail's NuPs2InitVideo does. */
    NuIntAddAllHandlers();
    NuIntAddVBlankS(&h_a);
    NuIntAddVBlankS(&h_b);
    NuIntAddVBlankS(&h_c);
    nw_game_yield(true);
    CHECK(strcmp(order, "bac") == 0);
    CHECK(nuvideo_global_vbcnt == vbcnt + 1);

    /* A yield without a wait delivers only edges it has not seen. */
    reset_order();
    nw_game_yield(false);
    CHECK(order_len == 0);

    /* Masked: the edges count nothing and the handlers wait; unmasking runs them once and counts once. */
    NuDisableVBlank();
    NuDisableVBlankS(); /* nested */
    nw_game_yield(true);
    nw_game_yield(true);
    nw_game_yield(true);
    CHECK(order_len == 0);
    CHECK(nuvideo_global_vbcnt == vbcnt + 1);
    NuEnableVBlankS();
    CHECK(order_len == 0);
    NuEnableVBlank();
    CHECK(strcmp(order, "bac") == 0);
    CHECK(nuvideo_global_vbcnt == vbcnt + 2);

    reset_order();
    NuIntRemVBlankS(&h_a);
    nw_game_yield(true);
    CHECK(strcmp(order, "bc") == 0);
    NuIntRemVBlankS(&h_b);
    NuIntRemVBlankS(&h_c);
    reset_order();
}

static void test_semaphores(void)
{
    int param[6] = { 0, 1, 0, 0, 0, 0 }; /* count, max_count, init_count, wait_threads, attr, option */
    sema = nw_ee_CreateSema(param);
    CHECK(sema >= 1);
    CHECK(nw_ee_PollSema(sema) == -419);
    CHECK(nw_ee_PollSema(sema + 1) == -408);
    CHECK(nw_ee_iSignalSema(sema) == sema);
    CHECK(nw_ee_iSignalSema(sema) == -420);
    CHECK(nw_ee_PollSema(sema) == sema);
    CHECK(nw_ee_PollSema(sema) == -419);

    /* WaitSema waits for vblanks until the interrupt handler signals. */
    NuIntAddVBlankS(&h_s);
    uint64_t before = nw_game_yield(false);
    CHECK(nw_ee_WaitSema(sema) == sema);
    CHECK(nw_game_yield(false) == before + 1);
    CHECK(strcmp(order, "s") == 0);
    CHECK(nw_ee_PollSema(sema) == -419);
    NuIntRemVBlankS(&h_s);

    /* The main thread is thread 1; 0 is no thread (the EE threads: test_ee.c). */
    CHECK(nw_ee_GetThreadId() == 1);
    CHECK(nw_ee_StartThread(0, NULL) < 0);

    /* The field sceGsSyncV returns alternates. */
    int field = sceGsSyncV(0);
    CHECK(sceGsSyncV(0) == !field);
}

/* EE time between vblanks moves the game's time on; past the next vblank the stepped clock emits it on the spending
   thread, and a wait starts the time again at its edge. The EE timers count the same time. */
static void test_spend(void)
{
    uint64_t edges = nw_game_edges(), us = nw_game_us();
    CHECK(us == edges * 20000);
    nw_game_spend_us(15000);
    CHECK(nw_game_edges() == edges && nw_game_us() == us + 15000);
    nw_game_spend_us(15000);
    CHECK(nw_game_edges() == edges + 1 && nw_game_us() == us + 30000);
    CHECK(nw_game_time_ns() == nw_game_us() * 1000);
    sceGsSyncV(0);
    CHECK(nw_game_edges() == edges + 2 && nw_game_us() == (edges + 2) * 20000);
    CHECK(nw_game_watch_edge(edges + 2, 0) == edges + 2);
}

static int spin_entry(int argc, char **argv)
{
    (void)argc, (void)argv;
    for (;;) {
        sceGsSyncV(0);
    }
}

int main(void)
{
    test_due();
    test_pacer();
    test_stepped();
    test_realtime();

    CHECK(nw_game_init(NW_CLOCK_STEPPED, 50));
    CHECK(nw_game_state() == NW_GAME_IDLE);
    test_vblank();
    test_semaphores();
    test_spend();
    nw_game_shutdown();

    /* A real game thread parks at its next yield point when asked. */
    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(spin_entry, 0, NULL));
    CHECK(nw_game_wait_edge(nw_game_wait_edge(0, 2000), 2000) >= 2);
    CHECK(nw_game_stop(2000) == NW_GAME_PARKED);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("clock: all checks passed\n");
    return 0;
}
