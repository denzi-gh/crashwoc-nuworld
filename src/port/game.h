/* The game thread (the EE main thread) runs the decomp's main on the virtual vblank clock; yield points (sceGsSyncV,
   WaitSema, the screen swap) deliver vblank interrupts, and quitting parks the EE thread there instead of unwinding. */
#ifndef NUWORLD_PORT_GAME_H
#define NUWORLD_PORT_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/clock.h"

/* The EE main thread's stack is the rest of RAM; the host gives each EE thread a generous fixed one. */
#define NW_GAME_STACK (16u << 20)

typedef int (*NwGameEntry)(int argc, char **argv);

typedef enum NwGameState {
    NW_GAME_IDLE,     /* not started */
    NW_GAME_RUNNING,
    NW_GAME_PARKED,   /* stopped at a yield point, as asked */
    NW_GAME_FATAL,    /* stopped by nw_game_fatal */
    NW_GAME_RETURNED,
} NwGameState;

/* Starts the clock; until nw_game_start the calling thread may act as the game thread (tests). */
bool nw_game_init(NwClockMode mode, unsigned hz);
/* Stops the clock; only when no game thread was started. */
void nw_game_shutdown(void);
bool nw_game_start(NwGameEntry entry, int argc, char **argv);

/* Any thread. */
NwGameState nw_game_state(void);
uint64_t nw_game_edges(void);
/* Edges plus the fraction of a period since the last one (nw_clock_time). */
double nw_game_edge_time(void);
/* Edges at the clock's rate plus what the running EE thread spent since the last one. */
uint64_t nw_game_us(void);
/* EE time between vblanks (a SIF RPC's round trip): stepped, it emits the edges it passes so IOP-only loops go on; in
   real time it stops short of the next edge. */
void nw_game_spend_us(uint64_t us);
/* What the EE timers count: host time in real time, nw_game_us when stepped. */
uint64_t nw_game_time_ns(void);
/* Waits for a clock edge after `seen` (see nw_clock_wait); returns the edge count. */
uint64_t nw_game_wait_edge(uint64_t seen, int32_t timeout_ms);
/* The same for a thread that only watches the game: it never steps the clock. */
uint64_t nw_game_watch_edge(uint64_t seen, int32_t timeout_ms);
uint64_t nw_game_swaps(void);
/* Asks the running EE thread to park at its next yield point, without waiting. */
void nw_game_request_stop(void);
/* The same, then waits up to timeout_ms for it to stop for any reason. */
NwGameState nw_game_stop(uint32_t timeout_ms);
const char *nw_game_error(void);

/* Yield point of the running EE thread: waits for an edge if asked, then delivers the vblank interrupts since the last
   yield; returns the edge count. */
uint64_t nw_game_yield(bool wait_edge);
/* Prints the message and parks the game for good. */
_Noreturn void nw_game_fatal(const char *message);
/* Fuzzers: called with the message first; it may longjmp out of the load that failed. */
void nw_game_on_fatal(void (*fn)(const char *message));
/* Called by NuPs2ScreenSwap for each swapped frame. */
void nw_game_swap(void);
/* Sets the EE FPU's float mode on a new host thread of an EE thread. */
void nw_game_host_enter(void);

#endif
