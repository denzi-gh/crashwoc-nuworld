/* VAG files (SFX.DAT): a big-endian "VAGp" header of 48 bytes, then PS-ADPCM blocks of 16 bytes (filter/shift,
   loop flags, 28 nibbles), decoded like the SPU2 (PCSX2 XA_decode_block). Errors via SDL_GetError(). */
#ifndef NUWORLD_AUDIO_VAG_H
#define NUWORLD_AUDIO_VAG_H

#include <stdbool.h>
#include <stdint.h>

#define NW_VAG_HEADER 48
#define NW_ADPCM_BLOCK 16
#define NW_ADPCM_SAMPLES 28

/* Loop flags, the second byte of a block. */
#define NW_ADPCM_END 1u    /* ENDX, then the voice jumps to the loop address */
#define NW_ADPCM_REPEAT 2u /* with END: keeps playing; without it the voice stops */
#define NW_ADPCM_START 4u  /* latches the loop address */

typedef struct NwVagHeader {
    uint32_t version;
    uint32_t size; /* ADPCM bytes after the header */
    uint32_t rate; /* Hz */
} NwVagHeader;

bool nw_vag_header(NwVagHeader *h, const void *head, uint64_t file_size);

/* Decoder history; zero at key on. */
typedef struct NwAdpcm {
    int32_t prev1, prev2;
} NwAdpcm;

/* One block to 28 samples; returns its loop flags. */
unsigned nw_adpcm_block(NwAdpcm *st, const uint8_t *block, int16_t out[NW_ADPCM_SAMPLES]);

typedef struct NwVagPass {
    uint32_t samples;   /* through the first END block, all blocks without one */
    int32_t loop_start; /* where that END jumps back to when it has REPEAT (0 without a START), else -1 */
} NwVagPass;

/* Plays count blocks once, as a voice keyed on at the first: START latches the loop address, the first END ends the
   pass. pcm has room for count * 28 samples. */
NwVagPass nw_vag_decode(const uint8_t *blocks, uint32_t count, int16_t *pcm);

#endif
