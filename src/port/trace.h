/* --trace-sound: a line whenever the sound key status, title cut state, sound stream loads or logo texanim frame change
   at a vblank, numbered from the logos cut's first hold (cutmovie_hold 0 -> 1) or, with --level, its level's start;
   tools/pcsx2_trace.py prints the same from PCSX2. */
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
