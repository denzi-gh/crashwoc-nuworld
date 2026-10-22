/* src/port/ps2video.c to the game layer: the EE's two vblank interrupts on the virtual clock. Vblank start (INTC 2,
   hooked through sceGsSyncVCallback) counts nuvideo_global_vbcnt and runs the start list; vblank end (INTC 3)
   runs the auto screen swap and the end list. */
#ifndef NUWORLD_PORT_PS2VIDEO_H
#define NUWORLD_PORT_PS2VIDEO_H

/* Clock edge callback (the clock thread): counts nuvideo_global_vbcnt as the start interrupt would, so a busy
   wait on it ends without a yield point. */
void nw_vblank_count(void *user, unsigned edges);
/* Game thread, at a yield point: the handlers of `edges` new edges, in order. A masked interrupt keeps one
   pending and runs when unmasked, as the INTC's pending bit does. */
void nw_vblank_deliver(unsigned edges);

#endif
