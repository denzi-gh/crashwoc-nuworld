/* --trace-sound: the sound key status the game reads, its title cut's state, its copies of the sound module's
   stream loads (request, busy, CD stages) and the logo's texanim frame, one line whenever they change at a vblank,
   numbered from the vblank on which the logos cut first holds for its audio (cutmovie_hold 0 -> 1), with --level
   from the one on which its level runs (port/level.h).
   tools/pcsx2_trace.py prints the same lines from PCSX2. */
#ifndef NUWORLD_PORT_TRACE_H
#define NUWORLD_PORT_TRACE_H

#include <stdbool.h>

void nw_trace_sound_start(void);
/* The running EE thread, after a vblank's interrupts ran (stdout). */
void nw_trace_sound_vblank(void);
/* The trace's number of vblank count vbcnt; false before the logos cut first holds. */
bool nw_trace_vblank(int vbcnt, int *vblank);
/* "frame <vblank>: world <cutworldix> time <its cut's time> tex <logo frame>", as tools/pcsx2_capture.py labels
   a PCSX2 savestate (stdout). */
void nw_trace_frame(int vblank);

#endif
