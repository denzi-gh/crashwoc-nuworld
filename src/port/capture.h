/* --capture: the title cut's frames around given trace vblanks (port/trace.h), the game thread held on each until
   the renderer has saved it; tools/title_frames.py pairs them with PCSX2's savestates (tools/pcsx2_capture.py). */
#ifndef NUWORLD_PORT_CAPTURE_H
#define NUWORLD_PORT_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

/* PCSX2 shows the frame of a few vblanks before its savestate: the frames from BEFORE before to AFTER after each
   requested vblank are saved. */
#define NW_CAPTURE_BEFORE 3
#define NW_CAPTURE_AFTER 1

/* The vblanks: "a,b,..." or a file of "frame <vblank>: ..." lines. False when there are none. */
bool nw_capture_start(const char *list);
/* Game thread, at each vblank: the label line of a requested one (nw_trace_frame). */
void nw_capture_vblank(void);
/* Game thread, packet seq published at vblank count vbcnt: waits while the renderer saves it, when it is wanted. */
void nw_capture_published(uint64_t seq, int vbcnt);
/* Render thread: the packet the game thread waits on, with its trace vblank; 0 when none. */
uint64_t nw_capture_wanted(int *vblank);
/* Render thread: that packet is saved, the game thread goes on. */
void nw_capture_release(void);
/* Every requested frame is past. */
bool nw_capture_finished(void);

#endif
