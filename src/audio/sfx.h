/* SFX.DAT as NUSOUND.IRX reads it: samples by the 0x1A file names, whole for TransferSFX, in pieces for the streams.
   The archive is the caller's nw_dat_open of the 0x1E path ("SFX.DAT"); its file handle seeks, so one per thread. */
#ifndef NUWORLD_AUDIO_SFX_H
#define NUWORLD_AUDIO_SFX_H

#include <stdbool.h>
#include <stdint.h>

#include "audio/vag.h"
#include "vfs/dat.h"

typedef struct NwSfxSample {
    int entry;
    NwVagHeader vag;
} NwSfxSample;

/* name as InitGlobalSfx builds it ("SFX\GENERIC\AKU_BKBN.VAG"), any case. */
bool nw_sfx_find(NwDat *sfx, const char *name, NwSfxSample *s);
/* size bytes of the ADPCM data from offset. */
bool nw_sfx_read(NwDat *sfx, const NwSfxSample *s, uint32_t offset, void *dst, uint32_t size);

#endif
