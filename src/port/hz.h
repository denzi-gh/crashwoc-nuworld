/* 50/60 Hz: the game logic's tick, PAL's 50 Hz (retail) or NTSC's 60 Hz, which the decomp's NW_HZ seams (nu_port.h)
   read. Set once before the game thread starts. */
#ifndef NUWORLD_PORT_HZ_H
#define NUWORLD_PORT_HZ_H

/* 60 takes NTSC's tick, anything else PAL's; returns the rate taken. */
int nw_hz_set(int hz);
/* An H-line of the rate's video mode in ns, the EE timer 1 unit: PAL 64 us, NTSC 63.556 us. */
unsigned nw_hz_line_ns(void);

#endif
