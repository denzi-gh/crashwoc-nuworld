#include "port/game.h"

#include <stdio.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "app.h"
#include "port/fptrap.h"
#include "port/ps2video.h"

/* A yield that finds more edges than this delivers only this many (the game was stalled, not slow). */
#define NW_GAME_MAX_CATCHUP 8

static struct {
    NwClock clock;
    SDL_Mutex *lock;
    SDL_Condition *cond;
    SDL_Thread *thread;
    NwGameState state; /* under lock */
    SDL_AtomicInt stop;
    SDL_AtomicInt swaps;
    NwGameEntry entry;
    int argc;
    char **argv;
    uint64_t delivered; /* the running EE thread: the edges whose interrupts ran */
    uint64_t spent_us;  /* the running EE thread: its time spent since edge spent_edge */
    uint64_t spent_edge;
    char error[512]; /* written before NW_GAME_FATAL */
} nw_game;

bool nw_game_init(NwClockMode mode, unsigned hz)
{
    nw_game.lock = SDL_CreateMutex();
    nw_game.cond = SDL_CreateCondition();
    if (!nw_game.lock || !nw_game.cond || !nw_clock_start(&nw_game.clock, mode, hz, nw_vblank_count, NULL)) {
        SDL_DestroyCondition(nw_game.cond);
        SDL_DestroyMutex(nw_game.lock);
        nw_game.cond = NULL;
        nw_game.lock = NULL;
        return false;
    }
    nw_game.state = NW_GAME_IDLE;
    nw_game.delivered = nw_clock_edges(&nw_game.clock);
    return true;
}

void nw_game_shutdown(void)
{
    nw_clock_stop(&nw_game.clock);
    SDL_DestroyCondition(nw_game.cond);
    SDL_DestroyMutex(nw_game.lock);
    nw_game.cond = NULL;
    nw_game.lock = NULL;
}

static void nw_game_set_state(NwGameState state)
{
    SDL_LockMutex(nw_game.lock);
    nw_game.state = state;
    SDL_BroadcastCondition(nw_game.cond);
    SDL_UnlockMutex(nw_game.lock);
}

_Noreturn static void nw_game_park(NwGameState state)
{
    nw_game_set_state(state);
    SDL_LockMutex(nw_game.lock);
    for (;;) {
        SDL_WaitCondition(nw_game.cond, nw_game.lock);
    }
}

void nw_game_host_enter(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    /* The EE FPU has no denormals: flush them to zero (FTZ) and read them as zero (DAZ). */
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    nw_fp_traps_enter();
}

static int SDLCALL nw_game_run(void *arg)
{
    (void)arg;
    nw_game_host_enter();
    nw_game.entry(nw_game.argc, nw_game.argv);
    nw_game_set_state(NW_GAME_RETURNED);
    return 0;
}

bool nw_game_start(NwGameEntry entry, int argc, char **argv)
{
    nw_game.entry = entry;
    nw_game.argc = argc;
    nw_game.argv = argv;
    nw_game.state = NW_GAME_RUNNING;
    nw_game.delivered = nw_clock_edges(&nw_game.clock); /* the game owes nothing to edges before it ran */
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)nw_game_run);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "nw_game");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, NW_GAME_STACK);
    nw_game.thread = props ? SDL_CreateThreadWithProperties(props) : NULL;
    SDL_DestroyProperties(props);
    if (!nw_game.thread) {
        nw_game.state = NW_GAME_IDLE;
        return false;
    }
    SDL_DetachThread(nw_game.thread);
    return true;
}

NwGameState nw_game_state(void)
{
    SDL_LockMutex(nw_game.lock);
    NwGameState state = nw_game.state;
    SDL_UnlockMutex(nw_game.lock);
    return state;
}

uint64_t nw_game_edges(void)
{
    return nw_clock_edges(&nw_game.clock);
}

double nw_game_edge_time(void)
{
    return nw_clock_time(&nw_game.clock);
}

uint64_t nw_game_us(void)
{
    uint64_t edges = nw_clock_edges(&nw_game.clock);
    return edges * nw_game.clock.timing.period / 1000 + (edges == nw_game.spent_edge ? nw_game.spent_us : 0);
}

void nw_game_spend_us(uint64_t us)
{
    uint64_t edges = nw_clock_edges(&nw_game.clock), period = nw_game.clock.timing.period / 1000;
    if (!period) { /* no clock */
        return;
    }
    if (edges != nw_game.spent_edge) {
        nw_game.spent_edge = edges;
        nw_game.spent_us = 0;
    }
    nw_game.spent_us += us;
    if (nw_game.spent_us < period) {
        return;
    }
    if (nw_game.clock.mode == NW_CLOCK_STEPPED) {
        unsigned n = (unsigned)(nw_game.spent_us / period);
        nw_clock_step(&nw_game.clock, n);
        nw_game.spent_edge = edges + n;
        nw_game.spent_us %= period;
    } else {
        nw_game.spent_us = period - 1;
    }
}

uint64_t nw_game_time_ns(void)
{
    return nw_game.clock.mode == NW_CLOCK_STEPPED ? nw_game_us() * 1000 : SDL_GetTicksNS();
}

uint64_t nw_game_wait_edge(uint64_t seen, int32_t timeout_ms)
{
    return nw_clock_wait(&nw_game.clock, seen, timeout_ms);
}

uint64_t nw_game_watch_edge(uint64_t seen, int32_t timeout_ms)
{
    return nw_clock_watch(&nw_game.clock, seen, timeout_ms);
}

uint64_t nw_game_swaps(void)
{
    return (uint32_t)SDL_GetAtomicInt(&nw_game.swaps);
}

void nw_game_request_stop(void)
{
    SDL_SetAtomicInt(&nw_game.stop, 1);
    nw_clock_wake(&nw_game.clock);
}

NwGameState nw_game_stop(uint32_t timeout_ms)
{
    nw_game_request_stop();
    SDL_LockMutex(nw_game.lock);
    uint64_t start = SDL_GetTicks();
    while (nw_game.state == NW_GAME_RUNNING) {
        uint64_t spent = SDL_GetTicks() - start;
        if (spent >= timeout_ms) {
            break;
        }
        SDL_WaitConditionTimeout(nw_game.cond, nw_game.lock, (Sint32)(timeout_ms - spent));
    }
    NwGameState state = nw_game.state;
    SDL_UnlockMutex(nw_game.lock);
    return state;
}

const char *nw_game_error(void)
{
    return nw_game.error;
}

uint64_t nw_game_yield(bool wait_edge)
{
    if (SDL_GetAtomicInt(&nw_game.stop)) {
        nw_game_park(NW_GAME_PARKED);
    }
    uint64_t edges = nw_clock_edges(&nw_game.clock);
    if (wait_edge) {
        edges = nw_clock_wait(&nw_game.clock, edges, -1);
        if (SDL_GetAtomicInt(&nw_game.stop)) {
            nw_game_park(NW_GAME_PARKED);
        }
    }
    if (edges > nw_game.delivered) {
        uint64_t n = edges - nw_game.delivered;
        nw_game.delivered = edges;
        if (n > NW_GAME_MAX_CATCHUP) {
            static bool reported;
            if (!reported) {
                reported = true;
                fprintf(stderr, NW_APP_NAME ": the game fell %llu vblanks behind; it gets %d of them (reported once)\n",
                        (unsigned long long)n, NW_GAME_MAX_CATCHUP);
            }
            n = NW_GAME_MAX_CATCHUP;
        }
        nw_vblank_deliver((unsigned)n);
    }
    return edges;
}

static void (*nw_game_fatal_hook)(const char *message);

void nw_game_on_fatal(void (*fn)(const char *message))
{
    nw_game_fatal_hook = fn;
}

void nw_game_fatal(const char *message)
{
    if (nw_game_fatal_hook) {
        nw_game_fatal_hook(message);
    }
    SDL_strlcpy(nw_game.error, message, sizeof(nw_game.error));
    nw_game_park(NW_GAME_FATAL);
}

void nw_game_swap(void)
{
    SDL_AddAtomicInt(&nw_game.swaps, 1);
}
