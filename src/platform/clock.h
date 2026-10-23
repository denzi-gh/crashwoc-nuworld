/* The virtual vblank clock: edges at a fixed rate on absolute deadlines (t0 + k * period, so no drift), counted
   for the game thread. Real time runs it on its own thread; stepped mode emits an edge whenever someone waits for
   one (deterministic runs and tests). */
#ifndef NUWORLD_PLATFORM_CLOCK_H
#define NUWORLD_PLATFORM_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

/* More periods late than this (the machine slept, a debugger stopped it) and the clock resyncs to now with one
   edge instead of a burst. */
#define NW_CLOCK_MAX_LATE 4

typedef struct NwClockTiming {
    uint64_t t0;     /* ns */
    uint64_t period; /* ns */
    uint64_t k;      /* edges since t0 */
} NwClockTiming;

/* The edges due at `now` (0 before the next deadline); advances the timing past them. */
unsigned nw_clock_due(NwClockTiming *t, uint64_t now);
uint64_t nw_clock_deadline(const NwClockTiming *t);

typedef enum NwClockMode {
    NW_CLOCK_REALTIME,
    NW_CLOCK_STEPPED,
} NwClockMode;

/* Called with the new edges before the waiters wake: on the clock thread in real time, on the caller's thread
   when stepped. */
typedef void (*NwClockEdge)(void *user, unsigned edges);

typedef struct NwClock {
    NwClockMode mode;
    NwClockTiming timing;
    NwClockEdge on_edge;
    void *user;
    SDL_Mutex *lock;
    SDL_Condition *cond;
    SDL_Thread *thread;
    uint64_t edges; /* under lock */
    uint64_t wakes; /* under lock: nw_clock_wake calls */
    bool quit;      /* under lock */
} NwClock;

bool nw_clock_start(NwClock *c, NwClockMode mode, unsigned hz, NwClockEdge on_edge, void *user);
void nw_clock_stop(NwClock *c);
uint64_t nw_clock_edges(NwClock *c);
/* Emits `n` edges now (any mode). */
void nw_clock_step(NwClock *c, unsigned n);
/* Blocks until the clock has emitted more than `seen` edges, `timeout_ms` passes (-1: never) or nw_clock_wake;
   returns the edge count. Stepped, it emits the edge itself. */
uint64_t nw_clock_wait(NwClock *c, uint64_t seen, int32_t timeout_ms);
/* nw_clock_wait that never steps: a stepped clock moves only for the thread that runs on it. */
uint64_t nw_clock_watch(NwClock *c, uint64_t seen, int32_t timeout_ms);
/* Wakes every waiter (they return with the count as it is). */
void nw_clock_wake(NwClock *c);

#endif
