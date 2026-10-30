/* The EE's two vblank interrupts on the virtual clock: start (INTC 2) counts nuvideo_global_vbcnt and runs the start
   list, end (INTC 3) runs the auto screen swap and the end list. */
#ifndef NUWORLD_PORT_PS2VIDEO_H
#define NUWORLD_PORT_PS2VIDEO_H

/* Clock thread: counts nuvideo_global_vbcnt as the start interrupt would, so a busy wait on it needs no yield point. */
void nw_vblank_count(void *user, unsigned edges);
/* EE thread at a yield point: runs the handlers of `edges` new edges, then nw_ee_schedule; a masked interrupt stays
   pending until unmasked, as the INTC's pending bit does. */
void nw_vblank_deliver(unsigned edges);

#endif
