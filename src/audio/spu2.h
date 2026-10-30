/* The SPU2 as NUSOUND.IRX drives it through libsd, timed and mixed as PCSX2's (Mixer.cpp, ADSR.cpp, Reverb.cpp).
   Register ids are libsd's: the core in bit 0, the voice in bits 1-5, the register in bits 8+. */
#ifndef NUWORLD_AUDIO_SPU2_H
#define NUWORLD_AUDIO_SPU2_H

#include <stdint.h>

#define NW_SPU2_VOICES 24
#define NW_SPU2_RAM (2u << 20) /* bytes */

/* sceSdSetParam / sceSdGetParam: voice registers, then core registers (bit 7) */
#define NW_SD_VP_VOLL 0x0000
#define NW_SD_VP_VOLR 0x0100
#define NW_SD_VP_PITCH 0x0200
#define NW_SD_VP_ADSR1 0x0300
#define NW_SD_VP_ADSR2 0x0400
#define NW_SD_VP_ENVX 0x0500
#define NW_SD_P_MMIX 0x0880
#define NW_SD_P_MVOLL 0x0980
#define NW_SD_P_MVOLR 0x0a80
#define NW_SD_P_EVOLL 0x0b80
#define NW_SD_P_EVOLR 0x0c80
#define NW_SD_P_AVOLL 0x0d80 /* core 1: core 0's output in */
#define NW_SD_P_AVOLR 0x0e80

/* sceSdSetSwitch / sceSdGetSwitch: one bit per voice */
#define NW_SD_S_KON 0x1500
#define NW_SD_S_KOFF 0x1600
#define NW_SD_S_ENDX 0x1700
#define NW_SD_S_VMIXL 0x1800 /* dry, left */
#define NW_SD_S_VMIXEL 0x1900 /* into the reverb, left */
#define NW_SD_S_VMIXR 0x1a00
#define NW_SD_S_VMIXER 0x1b00

/* sceSdSetAddr / sceSdGetAddr, in bytes: the core's reverb work area (it ends at the last byte of EEA's 128 KB),
   then voice addresses (bit 6). */
#define NW_SD_A_ESA 0x1c00
#define NW_SD_A_EEA 0x1d00
#define NW_SD_VA_SSA 0x2040
#define NW_SD_VA_LSAX 0x2140
#define NW_SD_VA_NAX 0x2240

/* sceSdSetCoreAttr / sceSdGetCoreAttr */
#define NW_SD_C_EFFECT_ENABLE 0x0002
#define NW_SD_C_SPDIF_MODE 0x000a

/* sceSdEffectAttr */
typedef struct NwSpu2Effect {
    int mode; /* SD_REV_MODE_*, 0x100: clear the work area */
    int16_t depth_L;
    int16_t depth_R;
    int delay;    /* ECHO and DELAY */
    int feedback;
} NwSpu2Effect;

#define NW_SPU2_REVERB_MODES 10 /* SD_REV_MODE_OFF .. PIPE */
#define NW_SPU2_REVERB_REGS 32  /* in the SPU1's order: APF sizes, volumes, addresses, IN_COEF */

/* libsd's reverb table: per mode the work area in 8-byte units and the registers sceSdSetEffectAttr writes, the
   addresses in 8-byte units; a mask bit per register, 0 for all. */
typedef struct NwSpu2Preset {
    uint32_t mask;
    uint16_t reg[NW_SPU2_REVERB_REGS];
} NwSpu2Preset;

typedef struct NwSpu2Presets {
    uint32_t size[NW_SPU2_REVERB_MODES];
    NwSpu2Preset mode[NW_SPU2_REVERB_MODES];
} NwSpu2Presets;

/* sceSdInit: the registers and voices as libsd leaves them; sound RAM, the clock, the output and the presets keep
   theirs. */
void nw_spu2_init(void);

/* NULL: none, every mode then has no work area and the reverb stays silent. */
void nw_spu2_set_presets(const NwSpu2Presets *presets);

/* Stereo frames (left, right) at 48 kHz, in batches as the SPU runs; NULL: nothing is mixed. */
typedef void (*NwSpu2Output)(void *user, const int16_t *frames, unsigned count);
void nw_spu2_set_output(NwSpu2Output out, void *user);

/* The port's own gain on a voice (0-47) after its envelope, 0x8000 (the default) for none. Any thread. */
#define NW_SPU2_UNITY 0x8000
void nw_spu2_set_gain(unsigned voice, int gain);

/* Each voice's decoded samples (voice 0-47), 4 at a time in the order its decoder reads them. */
typedef void (*NwSpu2Tap)(void *user, unsigned voice, const int16_t samples[4]);
void nw_spu2_set_tap(NwSpu2Tap tap, void *user);

/* Runs the SPU up to IOP cycle `cycle` (36.864 MHz); a cycle it has passed changes nothing. */
void nw_spu2_run_to(uint64_t cycle);
/* The IOP cycle the SPU has run to. */
uint64_t nw_spu2_cycle(void);

void nw_spu2_set_param(uint16_t entry, uint16_t value);
uint16_t nw_spu2_get_param(uint16_t entry);
void nw_spu2_set_switch(uint16_t entry, uint32_t value);
uint32_t nw_spu2_get_switch(uint16_t entry);
void nw_spu2_set_addr(uint16_t entry, uint32_t addr);
uint32_t nw_spu2_get_addr(uint16_t entry);
void nw_spu2_set_core_attr(uint16_t entry, uint16_t value);
uint16_t nw_spu2_get_core_attr(uint16_t entry);
/* As libsd: EVOL takes the depths, the reverb registers the mode's preset (ECHO and DELAY moved by delay and
   feedback), ESA the work area below EEA; a mode past PIPE changes nothing. */
void nw_spu2_set_effect_attr(int core, const NwSpu2Effect *attr);
NwSpu2Effect nw_spu2_get_effect_attr(int core);

/* sceSdVoiceTrans to sound RAM, done when it returns; clipped at the end of RAM. */
void nw_spu2_voice_trans(uint32_t spu_addr, const void *src, uint32_t size);
const uint8_t *nw_spu2_ram(void);

#endif
