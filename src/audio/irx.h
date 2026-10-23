/* NUSOUND.IRX's state under its own names and types (from the module's STABS), for src/audio/nusound.c and its
   tests. IOP addresses are host pointers. */
#ifndef NUWORLD_AUDIO_IRX_H
#define NUWORLD_AUDIO_IRX_H

#include <stdint.h>

#include "audio/nusound.h"
#include "vfs/dat.h"

#define NW_IRX_CHANNELS 48 /* core * 24 + voice */
#define NW_IRX_FILES 300

/* nusound_filename_info_s, the 0x1A record */
typedef struct NwIrxFilename {
    char Filename[32];
    int Pitch; /* the sample rate; 0: unused */
    int LoopInfo;
    int ID; /* >= 0x1000 resident, else streamed */
} NwIrxFilename;

/* Where a sample's VAG lies. Only cdrom0: is served, whose sectors here are the DAT's own; so datfile, which the
   IRX keeps for host0: only, is kept. */
typedef struct ADPCMFILE {
    int16_t ID;
    uint32_t size; /* 0: not found */
    uint32_t lsn;  /* the sector in the DAT */
    int Source;    /* 1: cdrom0: */
    NwDat *datfile;
} ADPCMFILE;

/* A resident sample in SPU RAM, by TransferSFX slot. */
typedef struct ADPCM_SPOTINFO {
    uint8_t SPUChannel;
    uint8_t SPUCore;
    uint8_t Status;
    int ID;
    uint32_t SPUAddress;
    int16_t LVol;
    int16_t RVol;
    int16_t Pitch;
    int AR;
} ADPCM_SPOTINFO;

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
    NwDat *datfile;
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
    int StereoChan1;
    int StereoChan2;
    int StereoPitch;
    int Direction;
    unsigned LoadFromEE;
    int SYSTEMWAIT;  /* a DMA to SPU RAM in flight */
    int SYSTEMWAIT2; /* a PlayStream being queued */
    int MyCounter;
    int CD_IDLE;
    int CD_EVENT_OCCURED;
    int CD_SPOT_FILE_ID;
    uint8_t *SPOT_IOPAddress;
    ADPCM_SPOTINFO ADPCM_SpotFX[NW_IRX_FILES];
    int16_t SPU_PausePitch[NW_IRX_CHANNELS];
    int ldd_size;
    int ldd_remain;
    void (*endfunc_load)(int);
    /* the read in progress (_CdStreamStart) */
    int cd_stream;
    int cd_stage; /* 0 idle, 1 seeking, 2 reading */
    int cd_startsec;
    int cd_readptr;
    uint8_t *cd_writeptr;
    int cd_filelen;
    int cd_dataleft;
    int cd_readlen;
    int cd_localcopy;
    int cd_localstart;
    void (*cd_func)(int);
    NwDat *cd_dat;
    uint8_t cd_buffer[2048];
    uint32_t g_ReverbChans[2];
    char nu_filepath[256];
    char nu_fileterm[4];
    char new_global_dat_name[256];
    char new_local_dat_name[256];
    char global_dat_name[256];
    char local_dat_name[256];
    NwDat *global_dat;
    NwDat *local_dat;
    NwIrxFilename ADPCM_FileNames[NW_IRX_FILES];
    int fileinfo_cnt;
    ADPCMFILE ADPCM_FileInfo[NW_IRX_FILES];
    int FilesOnCD;
    int AnyFiles;
    unsigned UserMaxStreams;
    uint32_t StreamMemoryEnd; /* SPU RAM: where the next resident sample goes */
    int GlobalSfxfileinfo_cnt; /* where a level's samples start */
    uint32_t GlobalSfxMemoryEnd;
    int32_t ret[NW_NUSOUND_REPLY / 4];
} NwIrx;

const NwIrx *nw_nusound_irx(void);

#endif
