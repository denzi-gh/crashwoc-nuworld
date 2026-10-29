/* 50/60 Hz: the game logic's tick, PAL's 50 Hz (retail) or NTSC's 60 Hz, which the decomp's NW_HZ seams (nu_port.h)
   read. Set once before the game thread starts. */
#ifndef NUWORLD_PORT_HZ_H
#define NUWORLD_PORT_HZ_H

#include <stddef.h>
#include <stdint.h>

/* 60 takes NTSC's tick, anything else PAL's; returns the rate taken. */
int nw_hz_set(int hz);
/* An H-line of the rate's video mode in ns, the EE timer 1 unit: PAL 64 us, NTSC 63.556 us. */
unsigned nw_hz_line_ns(void);

/* A data value NTSC's 60 Hz changes (hz_table.inc, tools/ntsc_diff.py table): an int of size bytes, or a float
   (size 0), at its PAL address. */
typedef struct NwHzPatch {
    const char *name;
    uint32_t addr;
    int size;
    uint32_t pal, ntsc;
    float fpal, fntsc;
} NwHzPatch;

/* Writes each NTSC value where host(addr) holds the PAL one; returns how many kept theirs (another value there, or
   no host address). */
int nw_hz_apply(const NwHzPatch *patches, size_t count, void *(*host)(uint32_t addr));
/* hz_table.inc through the data image, after nw_image_load. */
int nw_hz_patch(void);

#endif
