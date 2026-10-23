/* The SPU2 as NUSOUND.IRX drives it through libsd: two cores of 24 voices. Register ids are libsd's: the core in
   bit 0, the voice in bits 1-5, the register in bits 8+. No time passes: a keyed-on voice keeps its envelope until
   it is keyed off. */
#ifndef NUWORLD_AUDIO_SPU2_H
#define NUWORLD_AUDIO_SPU2_H

#include <stdint.h>

#define NW_SPU2_VOICES 24

/* sceSdSetParam / sceSdGetParam */
#define NW_SD_VP_ENVX 0x0500

/* sceSdSetSwitch / sceSdGetSwitch: one bit per voice */
#define NW_SD_S_KON 0x1500
#define NW_SD_S_KOFF 0x1600
#define NW_SD_S_ENDX 0x1700

/* sceSdInit */
void nw_spu2_init(void);

void nw_spu2_set_param(uint16_t entry, uint16_t value);
uint16_t nw_spu2_get_param(uint16_t entry);
void nw_spu2_set_switch(uint16_t entry, uint32_t value);
uint32_t nw_spu2_get_switch(uint16_t entry);

#endif
