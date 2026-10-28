/* Floating-point traps for sweeps (src/port/fptrap.c). */
#ifndef NUWORLD_PORT_FPTRAP_H
#define NUWORLD_PORT_FPTRAP_H

/* On a game or EE host thread: unmasks the traps when NUWORLD_FP_TRAPS is set. */
void nw_fp_traps_enter(void);

#endif
