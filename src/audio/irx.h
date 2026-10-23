/* NUSOUND.IRX's state under its own names and types (from the module's STABS), for src/audio/nusound.c and its
   tests. IOP addresses are host pointers. */
#ifndef NUWORLD_AUDIO_IRX_H
#define NUWORLD_AUDIO_IRX_H

#include <stdint.h>

#include "audio/nusound.h"

#define NW_IRX_CHANNELS 48 /* core * 24 + voice */
#define NW_IRX_FILES 300

struct NwDat;

/* nusound_filename_info_s, the 0x1A record */
typedef struct NwIrxFilename {
    char Filename[32];
    int Pitch; /* the sample rate; 0: unused */
    int LoopInfo;
    int ID; /* >= 0x1000 resident, else streamed */
} NwIrxFilename;

/* A stream slot, one per channel. */
typedef struct STREAMDATA {
    uint32_t SPUAddress;
    uint8_t *IOPAddress;
    uint32_t IOPBufferSize;
    uint32_t IOPBufferHalfSize;
    uint8_t *IOPCurrentAddress;
    uint32_t IOPCurrentRemainder;
    uint8_t *IOPLoadAddress;
    uint32_t FileSize;
    uint32_t FileRemainder;
    uint32_t FileSeek;
    uint32_t FileHandle;
    uint8_t Status;
    uint8_t iStatus;
    uint8_t PlayingHalf;
    uint8_t Channel;
    uint8_t LoopInfo;
    uint8_t StreamID;
    int16_t VolL;
    int16_t VolR;
    int16_t Pitch;
    int ID;
    int SectorStart;
    int SPUCurrent;
    int KeyStat;
    int AR;
    int USR_Channel;
    int USR_VolL;
    int USR_VolR;
    int USR_StrID;
    int USR_Pitch;
    int USR_LInfo;
    int USR_AR;
    uint32_t AllocSize;
    uint32_t AllocHalfSize;
    uint32_t AllocSPUAddress;
    int Error;
    int Source;
    struct NwDat *datfile;
    int fileoffset;
} STREAMDATA;

typedef struct NwIrx {
    STREAMDATA StreamBuffer[NW_IRX_CHANNELS];
    uint8_t SPU_KeyStatus[NW_IRX_CHANNELS];
    int ResStatus;
    int StreamBusy;
    int IOPRequestLoad;
    int LoadEECounter;
    int8_t CD_SPOT_TRANS;
    int StreamShutDown;
    int StereoStarted;
    unsigned LoadFromEE;
    int cd_stream;
    int cd_stage;
    uint32_t g_ReverbChans[2];
    char nu_filepath[256];
    char nu_fileterm[4];
    char new_global_dat_name[256];
    char new_local_dat_name[256];
    NwIrxFilename ADPCM_FileNames[NW_IRX_FILES];
    int fileinfo_cnt;
    int32_t ret[NW_NUSOUND_REPLY / 4];
} NwIrx;

const NwIrx *nw_nusound_irx(void);

#endif
