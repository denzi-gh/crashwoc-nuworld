/* --trace-sound: the sound key status the game reads and its title cut's state, one line whenever they change at a
   vblank, numbered from the vblank on which the logos cut first holds for its audio (cutmovie_hold 0 -> 1).
   tools/pcsx2_trace.py prints the same lines from PCSX2. */
#ifndef NUWORLD_PORT_TRACE_H
#define NUWORLD_PORT_TRACE_H

void nw_trace_sound_start(void);
/* The running EE thread, after a vblank's interrupts ran (stdout). */
void nw_trace_sound_vblank(void);

#endif
