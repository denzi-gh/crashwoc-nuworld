/* The game thread: the decomp's main runs on it, paced by the virtual vblank clock. It is the EE's main thread;
   the EE threads the game starts run on host threads of their own, one at a time (src/shim/ee.c). Yield points
   (sceGsSyncV, WaitSema, the screen swap) wait for clock edges and deliver the vblank interrupts; quitting parks
   the running EE thread at its next yield point and the process exits around it, so quitting never unwinds the
   decomp. */
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
    NW_GAME_RETURNED, /* the entry returned */
} NwGameState;

/* Starts the clock. Until nw_game_start, the calling thread may act as the game thread (tests). */
bool nw_game_init(NwClockMode mode, unsigned hz);
/* Stops the clock; only when no game thread was started. */
void nw_game_shutdown(void);
bool nw_game_start(NwGameEntry entry, int argc, char **argv);

/* Any thread. */
NwGameState nw_game_state(void);
uint64_t nw_game_edges(void);
/* The time of those edges at the clock's rate, in microseconds. */
uint64_t nw_game_us(void);
/* Waits for a clock edge after `seen` (see nw_clock_wait); returns the edge count. */
uint64_t nw_game_wait_edge(uint64_t seen, int32_t timeout_ms);
/* Frames the game has swapped. */
uint64_t nw_game_swaps(void);
/* Asks the running EE thread to park at its next yield point, without waiting. */
void nw_game_request_stop(void);
/* Asks the running EE thread to park at its next yield point; waits up to timeout_ms for it to stop for any reason. */
NwGameState nw_game_stop(uint32_t timeout_ms);
/* The message of NW_GAME_FATAL. */
const char *nw_game_error(void);

/* The running EE thread. A yield point: waits for the next clock edge if asked, then delivers the vblank
   interrupts of the edges since the last one; returns the edge count. */
uint64_t nw_game_yield(bool wait_edge);
/* The game cannot go on (the message is printed): parks for good. */
_Noreturn void nw_game_fatal(const char *message);
/* NuPs2ScreenSwap swapped a frame. */
void nw_game_swap(void);
/* A new host thread of an EE thread: the EE FPU's float mode. */
void nw_game_host_enter(void);

#endif
