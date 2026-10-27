/* libsd's reverb table from the disc's SYS/LIBSD.IRX (libsd 2.3.10, as the game loads it), read at run time: it is
   Sony's data, so the port carries none of it. */
#ifndef NUWORLD_AUDIO_LIBSD_H
#define NUWORLD_AUDIO_LIBSD_H

#include <stdbool.h>

#include "audio/spu2.h"

struct NwVfs;

/* false with an SDL error when the module is missing or not the one the game ships. */
bool nw_libsd_presets(const struct NwVfs *vfs, NwSpu2Presets *out);

#endif
