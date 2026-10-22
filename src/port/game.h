/* The game thread: the decomp's main runs on it, paced by the virtual vblank clock. It is the EE's main thread:
   the decomp only ever runs there. Yield points (sceGsSyncV, WaitSema, the screen swap) wait for clock edges and
   deliver the vblank interrupts; quitting parks the game thread at its next yield point and the process exits
   around it, so the decomp is never unwound. */
#ifndef NUWORLD_PORT_GAME_H
#define NUWORLD_PORT_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/clock.h"

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
/* Waits for a clock edge after `seen` (see nw_clock_wait); returns the edge count. */
uint64_t nw_game_wait_edge(uint64_t seen, int32_t timeout_ms);
/* Frames the game has swapped. */
uint64_t nw_game_swaps(void);
/* Asks the game thread to park at its next yield point; waits up to timeout_ms for it to stop for any reason. */
NwGameState nw_game_stop(uint32_t timeout_ms);
/* The message of NW_GAME_FATAL. */
const char *nw_game_error(void);

/* Game thread. A yield point: waits for the next clock edge if asked, then delivers the vblank interrupts of the
   edges since the last one; returns the edge count. */
uint64_t nw_game_yield(bool wait_edge);
/* The game cannot go on (the message is printed): parks for good. */
_Noreturn void nw_game_fatal(const char *message);
/* NuPs2ScreenSwap swapped a frame. */
void nw_game_swap(void);

#endif
