#include "audio/nusound.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/irx.h"
#include "audio/spu2.h"
#include "audio/vag.h"

#define NW_IRX_DT 256     /* AdpcmSetParam's dt[] */
#define NW_IRX_REPORTED 4 /* streams in the reply */
#define NW_IRX_NO_REQUEST 0xffff
#define NW_IRX_TICK_US 16666 /* the stream thread's hard timer */
#define NW_IRX_SECTOR 2048
#define NW_IRX_PATH 600
#define NW_IRX_SPOT 0x7f /* LoopInfo of a resident sample's transfer */
#define NW_IRX_VAG_HEADER 0x30

static NwIrx irx;

/* The port's side: the file system, the clock and how far the stream thread's ticks got. */
static struct {
    const NwVfs *vfs;
    uint64_t (*now_us)(void);
    bool timer;
    uint64_t timer_us;
    uint64_t ticks;
    NwDat global_dat;
    NwDat local_dat;
} iop;

/* A string command's data, NUL-terminated within its words (the IRX strcpys it whole). */
static void nw_irx_string(char *dst, size_t size, const uint16_t *p, int length)
{
    size_t n = (size_t)length * sizeof(*p);
    const char *s = (const char *)p;
    size_t len = strnlen(s, n < size - 1 ? n : size - 1);
    memcpy(dst, s, len);
    dst[len] = '\0';
}

void nw_nusound_init(const NwVfs *vfs, uint64_t (*now_us)(void))
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        free(irx.StreamBuffer[i].IOPAddress);
    }
    free(irx.SPOT_IOPAddress);
    if (irx.global_dat) {
        nw_dat_close(irx.global_dat);
    }
    if (irx.local_dat) {
        nw_dat_close(irx.local_dat);
    }
    memset(&irx, 0, sizeof(irx));
    memset(&iop, 0, sizeof(iop));
    iop.vfs = vfs;
    iop.now_us = now_us;
    irx.IOPRequestLoad = -1;
    irx.LoadFromEE = NW_IRX_NO_REQUEST;
    irx.CD_IDLE = 1;
    nw_spu2_init();
}

const NwIrx *nw_nusound_irx(void)
{
    return &irx;
}

/* 2 (a stream started, not keyed on yet) stays; otherwise whether the envelope is up. The IRX reads ENDX too, but
   it decides nothing. */
static void GetChannelStatus(void)
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        int core = i >= NW_SPU2_VOICES, voice = i - NW_SPU2_VOICES * core;
        if (irx.SPU_KeyStatus[i] != 2) {
            irx.SPU_KeyStatus[i] = nw_spu2_get_param((uint16_t)(core | voice << 1 | NW_SD_VP_ENVX)) != 0;
        }
    }
}

static void AdpcmGetStatus(void)
{
    int32_t *ret = irx.ret;

    ret[0] = 0;
    for (int a = 0; a < NW_IRX_REPORTED; a++) {
        if (irx.cd_stream == a && (irx.cd_stage == 1 || irx.cd_stage == 2)) {
            ret[0] += irx.cd_stage << 2 * a;
        }
    }
    ret[1] = 0;
    for (int a = 0; a < NW_IRX_REPORTED; a++) {
        const STREAMDATA *s = &irx.StreamBuffer[a];
        ret[1] += s->Status << a;
        ret[2 + a] = s->Channel;
        ret[6 + a] = s->ID;
        ret[10 + a] = s->SPUCurrent;
        ret[14 + a] = s->PlayingHalf;
        ret[18 + a] = s->KeyStat;
    }
    ret[22] = irx.ResStatus;
    ret[23] = irx.StreamBusy;
    ret[24] = irx.IOPRequestLoad;
    ret[25] = irx.LoadEECounter;
    ret[26] = irx.CD_SPOT_TRANS;
    ret[27] = irx.StreamShutDown;
    ret[28] = irx.StereoStarted;
    for (int a = 0; a < 3; a++) {
        uint32_t c = 0;
        for (int b = 0; b < 16; b++) {
            c = c << 2 | irx.SPU_KeyStatus[16 * a + b];
        }
        ret[29 + a] = (int32_t)c;
    }
}

static void AdpcmSetMasterVolume(int core, uint16_t vol)
{
    nw_spu2_set_param((uint16_t)(core | NW_SD_P_MVOLL), vol);
    nw_spu2_set_param((uint16_t)(core | NW_SD_P_MVOLR), vol);
}

/* A stream's IOP buffer, kept (parameters included) while one is allocated. */
static void ADPCM_AllocIOPMemory(unsigned i, uint32_t spuaddr, int size)
{
    if (i >= NW_IRX_CHANNELS) {
        return;
    }
    STREAMDATA *s = &irx.StreamBuffer[i];
    if (!s->IOPAddress) {
        s->IOPAddress = malloc(size > 0 ? (size_t)size : 1);
        s->AllocSize = (uint32_t)size;
        s->AllocHalfSize = (uint32_t)(size / 2);
        s->AllocSPUAddress = spuaddr;
    }
}

/* The global bank ends here; a level's samples go after it. */
static void SoundStoreLocalStart(void)
{
    irx.GlobalSfxfileinfo_cnt = irx.fileinfo_cnt;
    irx.GlobalSfxMemoryEnd = irx.StreamMemoryEnd;
}

/* Back to the global bank. The level's entries lose their Pitch (so InitADPCMFiles skips them), not their ID. */
static void SoundRestoreLocalStart(void)
{
    irx.fileinfo_cnt = irx.GlobalSfxfileinfo_cnt;
    for (int n = irx.fileinfo_cnt; n < NW_IRX_FILES; n++) {
        irx.ADPCM_FileNames[n].Pitch = 0;
    }
    irx.StreamMemoryEnd = irx.GlobalSfxMemoryEnd;
}

/* The IRX also sprintf's each name over a .rodata string, dropped here. */
static void SoundAddSample(const uint16_t *p, int length)
{
    if (irx.fileinfo_cnt < NW_IRX_FILES) {
        NwIrxFilename *fni = &irx.ADPCM_FileNames[irx.fileinfo_cnt++];
        size_t n = (size_t)length * sizeof(*p);
        memset(fni, 0, sizeof(*fni));
        memcpy(fni, p, n < sizeof(*fni) ? n : sizeof(*fni));
    }
}

/* SetGlobalDat / SetLocalDat: reopened only for another name; a DAT that fails to open is tried again next time. */
static void nw_irx_set_dat(NwDat **dat, NwDat *store, char *dat_name, const char *dfname)
{
    char path[NW_IRX_PATH];

    if (*dat) {
        if (strcmp(dfname, dat_name) == 0) {
            return;
        }
        nw_dat_close(*dat);
        *dat = NULL;
    }
    strcpy(dat_name, dfname);
    if (dat_name[0] && iop.vfs) {
        snprintf(path, sizeof(path), "%s%s%s", irx.nu_filepath, dat_name, irx.nu_fileterm);
        if (nw_dat_open(store, iop.vfs, path)) {
            *dat = store;
        }
    }
}

static void SetGlobalDat(const char *dfname)
{
    nw_irx_set_dat(&irx.global_dat, &iop.global_dat, irx.global_dat_name, dfname);
}

static void SetLocalDat(const char *dfname)
{
    nw_irx_set_dat(&irx.local_dat, &iop.local_dat, irx.local_dat_name, dfname);
}

/* Finds the samples from `start` on in the local DAT (from 0: the global one); a Pitch of 0 marks an unused entry.
   Only cdrom0: is served, the game's path: loose files (sceCdSearchFile) and host0: are not looked up, the disc
   keeps every sample in SFX.DAT. */
static void InitADPCMFiles(int start)
{
    NwDat *current_dat;
    char fn[NW_IRX_PATH];

    if (start) {
        SetLocalDat(irx.new_local_dat_name);
        current_dat = irx.local_dat;
        if (!current_dat) {
            SetGlobalDat(irx.new_global_dat_name);
            current_dat = irx.global_dat;
        }
    } else {
        SetGlobalDat(irx.new_global_dat_name);
        current_dat = irx.global_dat;
    }
    for (int i = start; i < NW_IRX_FILES; i++) {
        const NwIrxFilename *n = &irx.ADPCM_FileNames[i];
        ADPCMFILE *f = &irx.ADPCM_FileInfo[i];
        if (n->ID == -1) {
            break;
        }
        if (n->Pitch == 0) {
            continue;
        }
        snprintf(fn, sizeof(fn), "%s%s%s", irx.nu_filepath, n->Filename, irx.nu_fileterm);
        int ix = current_dat ? nw_dat_find(current_dat, n->Filename) : -1;
        if (ix < 0 || strncmp(fn, "cdrom0:", 7) != 0) {
            f->size = 0;
            continue;
        }
        const NwDatEntry *e = &current_dat->entries[ix];
        irx.FilesOnCD++;
        f->lsn = e->offset / NW_IRX_SECTOR;
        f->size = e->size;
        f->ID = (int16_t)n->ID;
        f->Source = 1;
        f->datfile = current_dat;
        irx.AnyFiles = 1;
    }
}

/* The stream table, the files, then the stream thread on its 60 Hz timer. The IRX drops the slots' buffers
   without freeing them, and a second call starts a second timer; the game sends this once, before the buffers. */
static void InitStreamData(void)
{
    irx.FilesOnCD = 0;
    irx.CD_SPOT_TRANS = 0;
    irx.StereoStarted = irx.StereoChan1 = irx.StereoChan2 = irx.StereoPitch = 0;
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        STREAMDATA *s = &irx.StreamBuffer[i];
        s->Error = 0x8000;
        s->Status = 0;
        s->FileHandle = 0;
        s->Channel = (uint8_t)i;
        s->VolL = s->VolR = 0;
        free(s->IOPAddress);
        s->IOPAddress = NULL;
        s->PlayingHalf = 0;
        s->iStatus = 3;
    }
    InitADPCMFiles(0);
    if (!iop.timer) {
        iop.timer = true;
        iop.timer_us = iop.now_us ? iop.now_us() : 0;
        iop.ticks = 0;
    }
}

/* libsd's id of a channel's voice register. */
static uint16_t nw_irx_voice(int ch, uint16_t reg)
{
    int core = ch >= NW_SPU2_VOICES;
    return (uint16_t)(core | (ch - NW_SPU2_VOICES * core) << 1 | reg);
}

/* Remembered for ResumeAll. */
static void SetChannelPitch(int ch, uint16_t pitch)
{
    if (ch < NW_IRX_CHANNELS) {
        nw_spu2_set_param(nw_irx_voice(ch, NW_SD_VP_PITCH), pitch);
        irx.SPU_PausePitch[ch] = (int16_t)pitch;
    }
}

static void SetChannelVolume(int ch, uint16_t VolL, uint16_t VolR)
{
    if (ch < NW_IRX_CHANNELS) {
        nw_spu2_set_param(nw_irx_voice(ch, NW_SD_VP_VOLL), VolL);
        nw_spu2_set_param(nw_irx_voice(ch, NW_SD_VP_VOLR), VolR);
    }
}

/* AR is the EE's attack | release << 8: ADSR1 gets the attack with decay and sustain level at their maximum, ADSR2
   the release with the fastest sustain. */
static void SetVoiceData(int ch, int Pitch, int VolL, int VolR, uint32_t SPUAddress, int AR)
{
    if (ch >= NW_IRX_CHANNELS) {
        return;
    }
    SetChannelVolume(ch, (uint16_t)VolL, (uint16_t)VolR);
    SetChannelPitch(ch, (uint16_t)Pitch);
    nw_spu2_set_param(nw_irx_voice(ch, NW_SD_VP_ADSR1), (uint16_t)((AR & 0xff) << 8 | 0xff));
    nw_spu2_set_param(nw_irx_voice(ch, NW_SD_VP_ADSR2), (uint16_t)(AR >> 8 | 0x1fc0));
    nw_spu2_set_addr(nw_irx_voice(ch, NW_SD_VA_SSA), SPUAddress);
}

static void nw_irx_key_on(int ch)
{
    if (ch < NW_IRX_CHANNELS) {
        int core = ch >= NW_SPU2_VOICES;
        nw_spu2_set_switch((uint16_t)(core | NW_SD_S_KON), 1u << (ch - NW_SPU2_VOICES * core));
    }
}

/* The resident sample's SPU address, -1 when none has the ID (the low 16 bits compared signed). */
static uint32_t GetFXSPUAddress(int ID)
{
    for (int i = 0; i < NW_IRX_FILES; i++) {
        if ((int16_t)irx.ADPCM_SpotFX[i].ID == ID) {
            return irx.ADPCM_SpotFX[i].SPUAddress;
        }
    }
    return 0xffffffffu;
}

/* Keyed on even when the ID has no sample, from address -1. */
static void PlayResidentSFX(int ID, int Channel, int VolL, int VolR, int Pitch, int AR)
{
    SetVoiceData(Channel, Pitch, VolL, VolR, GetFXSPUAddress(ID), AR);
    nw_irx_key_on(Channel);
}

/* Pausing stops the voices' pitch, saved for ResumeAll; a second pause before the resume saves the zeros. */
static void PauseAll(void)
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        irx.SPU_PausePitch[i] = (int16_t)nw_spu2_get_param(nw_irx_voice(i, NW_SD_VP_PITCH));
        nw_spu2_set_param(nw_irx_voice(i, NW_SD_VP_PITCH), 0);
    }
}

static void ResumeAll(void)
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        nw_spu2_set_param(nw_irx_voice(i, NW_SD_VP_PITCH), (uint16_t)irx.SPU_PausePitch[i]);
    }
}

/* KOFF and the fastest release. The IRX takes the channel on trust; here one past 47 is left alone. */
static void nw_irx_key_off(int ch)
{
    if (ch < NW_IRX_CHANNELS) {
        int core = ch >= NW_SPU2_VOICES;
        nw_spu2_set_switch((uint16_t)(core | NW_SD_S_KOFF), 1u << (ch - NW_SPU2_VOICES * core));
        nw_spu2_set_param(nw_irx_voice(ch, NW_SD_VP_ADSR2), 0x1fc0);
    }
}

static void nw_irx_stopped(int ch)
{
    if (ch < NW_IRX_CHANNELS) {
        irx.SPU_KeyStatus[ch] = 0;
    }
}

/* Ch >= 0x40: stream slot Ch & 0x3f; otherwise the first active stream on channel Ch (keyed off twice). The IRX's
   busy waits between the register writes are left out. */
static void StopStream(int Ch)
{
    if (Ch >= 0x40) {
        int i = Ch & 0x3f;
        if (i >= NW_IRX_CHANNELS || !irx.StreamBuffer[i].Status) {
            return;
        }
        STREAMDATA *s = &irx.StreamBuffer[i];
        s->Status = 0;
        s->iStatus = 3;
        SetChannelPitch(s->Channel, 0);
        nw_irx_key_off(s->Channel);
        nw_irx_stopped(s->Channel);
        return;
    }
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        STREAMDATA *s = &irx.StreamBuffer[i];
        if (s->Status && s->Channel == Ch) {
            s->Status = 0;
            s->iStatus = 3;
            SetChannelPitch(Ch, 0);
            nw_irx_key_off(Ch);
            nw_irx_key_off(Ch);
            nw_irx_stopped(Ch);
            return;
        }
    }
}

/* The sample entry of an ID: NW_IRX_FILES when it is not there, -1 at an ID of -1. */
static int GetFileID(int ID)
{
    int a;
    for (a = 0; a < NW_IRX_FILES; a++) {
        if (irx.ADPCM_FileNames[a].ID == -1) {
            return -1;
        }
        if (irx.ADPCM_FileNames[a].ID == ID) {
            break;
        }
    }
    return a;
}

/* The IRX reads ADPCM_FileInfo[300] (past the table) for an ID it has no entry for; here that is a missing file. */
static const ADPCMFILE *nw_irx_file(int ix)
{
    static const ADPCMFILE missing;
    return ix >= 0 && ix < NW_IRX_FILES ? &irx.ADPCM_FileInfo[ix] : &missing;
}

/* 1 when the file is missing. */
static int OpenADPCMFile(int Stream, int idx)
{
    STREAMDATA *s = &irx.StreamBuffer[Stream];
    const ADPCMFILE *f = nw_irx_file(idx);
    s->SectorStart = (int)f->lsn;
    s->FileSize = f->size;
    s->Source = f->Source;
    s->datfile = f->datfile;
    s->fileoffset = 0; /* host0: only */
    if (f->size == 0) {
        return 1;
    }
    s->FileRemainder = f->size - NW_IRX_VAG_HEADER;
    return 0;
}

/* sceCdCallback: a seek or read finished. */
static void _CdStreamInt(void)
{
    if (irx.cd_stage != 0) {
        irx.CD_EVENT_OCCURED = 1;
    }
}

/* sceCdRead of the DAT's sectors, done at once; past the end of the DAT it reads zeros. */
static int nw_irx_cd_read(int lsn, int sectors, uint8_t *dst)
{
    size_t size = (size_t)sectors * NW_IRX_SECTOR;
    int64_t got = 0;
    if (irx.cd_dat && irx.cd_dat->file && lsn >= 0 &&
        nw_vfs_seek(irx.cd_dat->file, (int64_t)lsn * NW_IRX_SECTOR, 0) >= 0) {
        got = nw_vfs_read(irx.cd_dat->file, dst, size);
    }
    if (got < 0) {
        return 0;
    }
    memset(dst + got, 0, size - (size_t)got);
    _CdStreamInt();
    return 1;
}

/* Starts a read of size bytes at readptr into a file of filelen bytes at sector startsec; it runs one step per
   tick (_CdHandleEvent) and calls endfunc when done. The seek finishes at once. */
static void _CdStreamStart(int startsec, int readptr, uint8_t *writeptr, int filelen, int size, int i,
                           void (*endfunc)(int), NwDat *datfile)
{
    if (irx.cd_stage != 0) {
        return;
    }
    irx.cd_stream = i;
    irx.cd_startsec = startsec;
    irx.cd_readptr = readptr;
    irx.cd_writeptr = writeptr;
    irx.cd_filelen = filelen;
    irx.cd_dataleft = size;
    irx.cd_readlen = 0;
    irx.cd_stage = 1;
    irx.cd_localcopy = 0;
    irx.cd_localstart = 0;
    irx.cd_func = endfunc;
    irx.cd_dat = datfile;
    irx.CD_IDLE = 0;
    _CdStreamInt();
}

/* One step of the read: an unaligned head or a tail shorter than a sector goes through cd_buffer, whole sectors
   straight to the target. At the end of the file it goes on at 0x30, past the header (looping streams). */
static void _CdHandleEvent(void)
{
    int local = 0;

    irx.CD_EVENT_OCCURED = 0;
    if (irx.cd_stage == 0) {
        irx.CD_IDLE = 1;
    } else {
        irx.cd_stage = 2;
        if (irx.cd_localcopy) {
            memcpy(irx.cd_writeptr, irx.cd_buffer + irx.cd_localstart, (size_t)irx.cd_readlen);
        }
        int last_readptr = irx.cd_readptr, last_dataleft = irx.cd_dataleft;
        uint8_t *last_writeptr = irx.cd_writeptr;
        irx.cd_readptr += irx.cd_readlen;
        irx.cd_dataleft -= irx.cd_readlen;
        irx.cd_writeptr += irx.cd_readlen;
        if (irx.cd_dataleft == 0) {
            irx.cd_stage = 0;
        } else {
            if (irx.cd_readptr == irx.cd_filelen) {
                irx.cd_readptr = NW_IRX_VAG_HEADER;
            }
            int start = irx.cd_readptr, end = irx.cd_readptr + irx.cd_dataleft;
            if ((start & (NW_IRX_SECTOR - 1)) == 0) {
                if (end > irx.cd_filelen) {
                    end = irx.cd_filelen;
                }
                if (end & (NW_IRX_SECTOR - 1)) {
                    if (end - start > NW_IRX_SECTOR) {
                        end &= ~(NW_IRX_SECTOR - 1);
                    } else {
                        local = 1;
                    }
                }
            } else {
                local = 1;
                if ((start / NW_IRX_SECTOR + 1) * NW_IRX_SECTOR < end) {
                    end = (start / NW_IRX_SECTOR + 1) * NW_IRX_SECTOR;
                }
                if (end > irx.cd_filelen) {
                    end = irx.cd_filelen;
                }
            }
            irx.cd_readlen = end - start;
            int result;
            if (local) {
                irx.cd_localstart = start % NW_IRX_SECTOR;
                result = nw_irx_cd_read(start / NW_IRX_SECTOR + irx.cd_startsec, 1, irx.cd_buffer);
                irx.cd_localcopy = 1;
            } else {
                result = nw_irx_cd_read(start / NW_IRX_SECTOR + irx.cd_startsec, (end - start) / NW_IRX_SECTOR,
                                        irx.cd_writeptr);
                irx.cd_localcopy = 0;
            }
            if (!result) { /* tried again next tick */
                irx.cd_readptr = last_readptr;
                irx.cd_dataleft = last_dataleft;
                irx.cd_writeptr = last_writeptr;
                irx.cd_stage = 2;
                _CdStreamInt();
            }
        }
    }
    if (irx.cd_stage == 0) {
        irx.CD_IDLE = 1;
        irx.cd_func(1);
    }
}

static void cdend11(int moo)
{
    STREAMDATA *s = &irx.StreamBuffer[irx.cd_stream];
    (void)moo;
    s->FileSeek += (uint32_t)irx.ldd_size;
    s->FileRemainder -= (uint32_t)irx.ldd_size;
    irx.endfunc_load(1);
}

/* A loop read past the end and on from the start: the end's two block flags are cleared, so the voice runs on.
   With less than two blocks left the IRX writes before its buffer; not here (nor in cdend12). */
static void cdend10(int moo)
{
    STREAMDATA *s = &irx.StreamBuffer[irx.cd_stream];
    (void)moo;
    if (irx.ldd_remain >= 2 * NW_ADPCM_BLOCK) {
        s->IOPCurrentAddress[irx.ldd_remain - 0xf] = 0;
        s->IOPCurrentAddress[irx.ldd_remain - 0x1f] = 0;
    }
    s->FileSeek = (uint32_t)(irx.ldd_size - irx.ldd_remain + NW_IRX_VAG_HEADER);
    s->FileRemainder = s->FileSize - s->FileSeek;
    irx.endfunc_load(1);
}

/* A one-shot's end is in: its last data block ends the voice, nothing more to read. */
static void cdend12(int moo)
{
    STREAMDATA *s = &irx.StreamBuffer[irx.cd_stream];
    (void)moo;
    s->LoopInfo = 2;
    if (irx.ldd_remain >= 2 * NW_ADPCM_BLOCK) {
        s->IOPCurrentAddress[irx.ldd_remain - 0x1f] = 1;
        s->IOPCurrentAddress[irx.ldd_remain - 0xf] = 7;
    }
    irx.endfunc_load(1);
}

/* Reads the next Size bytes of slot i's file into its IOP buffer, then endfunc: a loop (LoopInfo 0) wraps to the
   file's start, a one-shot (1) or a resident sample reads what is left. */
static int LoadDataFromDisk(int i, int Size, void (*endfunc)(int))
{
    STREAMDATA *s = &irx.StreamBuffer[i];
    void (*done)(int);
    int size = Size;

    if (s->LoopInfo == 2) { /* all of it is loaded */
        irx.cd_stream = i;
        endfunc(1);
        return 0;
    }
    int remain = (int)s->FileRemainder;
    irx.ldd_size = Size;
    irx.ldd_remain = remain;
    irx.endfunc_load = endfunc;
    if (Size < remain) {
        done = cdend11;
    } else if (s->LoopInfo == 0) {
        done = cdend10;
    } else if (s->LoopInfo == 1) {
        size = remain;
        done = cdend12;
    } else if (s->LoopInfo == NW_IRX_SPOT) {
        size = remain;
        done = cdend11;
    } else {
        return -1;
    }
    _CdStreamStart(s->SectorStart, (int)s->FileSeek, s->IOPCurrentAddress, (int)s->FileSize, size, i, done,
                   s->datfile);
    return 0;
}

/* DMAs Size bytes of the IOP buffer to SPU RAM: all of it (BufferHalf 2) or a half, whose block flags make the
   SPU buffer a ring (the first block starts the loop, the last one jumps back); after a half the next one is read
   ahead. All of a file in (LoopInfo 2) goes to the half once and plays out (PlayingHalf 3). The DMA is done when
   the transfer returns, so its interrupt (_AdpcmDmaInt) clears SYSTEMWAIT at once. */
static int UpdateStream(int i, uint32_t Size, int BufferHalf, void (*endfunc)(int))
{
    STREAMDATA *s = &irx.StreamBuffer[i];

    irx.SYSTEMWAIT = 1;
    if (s->LoopInfo == 2) {
        if (BufferHalf == 2) {
            BufferHalf = 0;
        }
        nw_spu2_voice_trans(s->SPUAddress + (uint32_t)BufferHalf * s->AllocHalfSize, s->IOPCurrentAddress, Size);
        s->PlayingHalf = 3;
        irx.StreamBusy = 0;
        irx.cd_stream = i;
        endfunc(1);
        irx.SYSTEMWAIT = 0;
        return 0;
    }
    if (BufferHalf == 2) {
        if (s->LoopInfo != NW_IRX_SPOT) {
            s->IOPAddress[1] = NW_ADPCM_START | NW_ADPCM_REPEAT;
            s->IOPAddress[0x11] = NW_ADPCM_REPEAT;
            s->IOPAddress[Size - 0xf] = NW_ADPCM_END | NW_ADPCM_REPEAT;
        }
        nw_spu2_voice_trans(s->SPUAddress, s->IOPLoadAddress, Size);
    } else {
        s->IOPLoadAddress[1] = BufferHalf ? NW_ADPCM_REPEAT : NW_ADPCM_START | NW_ADPCM_REPEAT;
        s->IOPLoadAddress[0x11] = NW_ADPCM_REPEAT;
        s->IOPLoadAddress[Size - 0xf] = BufferHalf ? NW_ADPCM_END | NW_ADPCM_REPEAT : NW_ADPCM_REPEAT;
        nw_spu2_voice_trans(s->SPUAddress + (uint32_t)BufferHalf * s->AllocHalfSize, s->IOPCurrentAddress, Size);
    }
    irx.SYSTEMWAIT = 0;
    s->IOPLoadAddress = s->IOPCurrentAddress;
    s->IOPCurrentAddress += Size;
    s->IOPCurrentRemainder -= Size;
    if (s->IOPCurrentRemainder == 0) {
        s->IOPCurrentRemainder = s->AllocSize;
        s->IOPCurrentAddress = s->IOPAddress;
        s->IOPLoadAddress = s->IOPCurrentAddress;
    }
    if (BufferHalf == 2) {
        endfunc(1);
        return 1;
    }
    return LoadDataFromDisk(i, (int)s->AllocHalfSize, endfunc);
}

/* The stream's start: the whole buffer is in SPU RAM, the key-on comes with the next handshake. */
static void cdend2(int moo)
{
    (void)moo;
    irx.StreamBuffer[irx.cd_stream].iStatus = 1;
    irx.StreamBusy = 0;
}

static void cdend1(int moo)
{
    (void)moo;
    UpdateStream(irx.cd_stream, irx.StreamBuffer[irx.cd_stream].AllocSize, 2, cdend2);
}

/* Keyed on, the next half read ahead: streaming. */
static void cdend3(int moo)
{
    STREAMDATA *s = &irx.StreamBuffer[irx.cd_stream];
    (void)moo;
    if (s->iStatus == 1) {
        s->iStatus = 0;
    }
    irx.StreamBusy = 0;
}

static void cdend4(int moo)
{
    (void)moo;
    irx.StreamBuffer[irx.cd_stream].PlayingHalf = 1;
    irx.StreamBusy = 0;
}

static void cdend5(int moo)
{
    (void)moo;
    irx.StreamBuffer[irx.cd_stream].PlayingHalf = 0;
    irx.StreamBusy = 0;
}

/* A resident sample is in SPU RAM: the next one goes after it, header bytes included. */
static void cdend7(int moo)
{
    (void)moo;
    irx.StreamBuffer[irx.cd_stream].Status = 0;
    irx.StreamMemoryEnd += nw_irx_file(GetFileID(irx.ADPCM_FileNames[irx.CD_SPOT_FILE_ID].ID))->size;
    free(irx.SPOT_IOPAddress);
    irx.SPOT_IOPAddress = NULL;
    irx.CD_SPOT_TRANS = 0;
    irx.CD_IDLE = 1;
}

static void cdend6(int moo)
{
    (void)moo;
    UpdateStream(irx.cd_stream, irx.StreamBuffer[irx.cd_stream].IOPBufferSize, 2, cdend7);
}

/* Sets slot Stream up for its file; a resident sample's transfer (LoopInfo 0x7f) gets an IOP buffer of its own
   and goes to the end of the resident samples. -1 when the file is missing. */
static int StartStream(int Stream, int SPUChannel, int VolL, int VolR, int StreamID, int Pitch, int LoopInfo, int AR)
{
    STREAMDATA *s = &irx.StreamBuffer[Stream];
    uint8_t *IOPAddress;
    int FileIndex;

    if (LoopInfo == NW_IRX_SPOT) {
        FileIndex = GetFileID(StreamID);
        uint32_t file_size = nw_irx_file(FileIndex)->size;
        uint32_t size = file_size > NW_IRX_VAG_HEADER ? file_size - NW_IRX_VAG_HEADER : 0;
        free(irx.SPOT_IOPAddress);
        irx.SPOT_IOPAddress = malloc(size ? size : 1);
        IOPAddress = irx.SPOT_IOPAddress;
        s->IOPBufferSize = size;
        s->SPUAddress = irx.StreamMemoryEnd;
    } else {
        IOPAddress = s->IOPAddress;
        s->IOPBufferSize = s->AllocSize;
        s->SPUAddress = s->AllocSPUAddress;
        s->ID = StreamID;
        FileIndex = GetFileID(StreamID);
    }
    s->AR = AR;
    s->PlayingHalf = 0;
    s->VolL = (int16_t)VolL;
    s->VolR = (int16_t)VolR;
    s->IOPLoadAddress = IOPAddress;
    s->IOPCurrentAddress = IOPAddress;
    s->IOPBufferHalfSize = s->AllocHalfSize;
    s->Channel = (uint8_t)SPUChannel;
    s->IOPCurrentRemainder = s->AllocSize;
    s->FileSeek = NW_IRX_VAG_HEADER;
    s->LoopInfo = (uint8_t)(LoopInfo == 2 && FileIndex >= 0 && FileIndex < NW_IRX_FILES
                                ? irx.ADPCM_FileNames[FileIndex].LoopInfo
                                : LoopInfo);
    if (LoopInfo != NW_IRX_SPOT) {
        int a = Pitch;
        if (Pitch == 1) { /* the file's rate */
            a = FileIndex >= 0 && FileIndex < NW_IRX_FILES ? irx.ADPCM_FileNames[FileIndex].Pitch : 0;
            if (a > 11024) {
                a = (a << 12) / 48000;
            }
        }
        s->Pitch = (int16_t)a;
    }
    /* Without a buffer (no 0xD for the slot) the IRX would read to IOP address 0; a missing file here. */
    if (!IOPAddress || OpenADPCMFile(Stream, FileIndex)) {
        if (LoopInfo == NW_IRX_SPOT) {
            free(irx.SPOT_IOPAddress);
            irx.SPOT_IOPAddress = NULL;
            irx.CD_SPOT_TRANS = 0;
        }
        s->Status = 0;
        return -1;
    }
    return Stream;
}

/* Queues a stream for the next ticks: bank 0x80 takes the first free slot, any other bank is the slot. */
static int USR_StartStream(int SPUChannel, int VolL, int VolR, int StreamID, int Pitch, int LoopInfo, int AR)
{
    int S = -1, a = (SPUChannel >> 8) & 0xff;

    SPUChannel &= 0xff;
    StopStream(SPUChannel);
    if (a == 0x80) {
        for (int i = 0; i < (int)irx.UserMaxStreams; i++) {
            if (irx.StreamBuffer[i].Status == 0) {
                S = i;
                break;
            }
        }
    } else {
        S = a;
        if (S >= (int)irx.UserMaxStreams) {
            return -3;
        }
    }
    if (S == -1) {
        return -2;
    }
    STREAMDATA *s = &irx.StreamBuffer[S];
    s->iStatus = LoopInfo == NW_IRX_SPOT ? 4 : 2;
    s->USR_Channel = SPUChannel;
    s->USR_VolL = VolL;
    s->USR_VolR = VolR;
    s->USR_StrID = StreamID;
    s->USR_Pitch = Pitch;
    s->USR_LInfo = LoopInfo;
    s->USR_AR = AR;
    s->Status = 1;
    return S;
}

/* Slot 299 marks slot 300 free in the IRX (past the table); not here. */
static int SpotFX_LoadAndTransfer(int ID, int Slot)
{
    if (Slot >= NW_IRX_FILES) {
        return -1;
    }
    irx.CD_SPOT_TRANS = 1;
    irx.ADPCM_SpotFX[Slot].ID = ID;
    if (Slot + 1 < NW_IRX_FILES) {
        irx.ADPCM_SpotFX[Slot + 1].ID = -1;
    }
    int last = (int)irx.UserMaxStreams - 1;
    USR_StartStream(last | last << 8, 0, 0, ID, 0, NW_IRX_SPOT, 0);
    irx.ADPCM_SpotFX[Slot].SPUAddress = irx.StreamMemoryEnd;
    return 0;
}

/* TransferSFX: 0 started, 1 no such ID (the end of the bank), 2 its file is missing, 3 a transfer is running. The
   IRX scans on past the 300 entries until an ID of -1; the game never sends one, so here the table's end is the
   end of the bank. */
static int TransferResidentSampleBank(int id, int slot)
{
    if (irx.CD_SPOT_TRANS) {
        return 3;
    }
    for (int i = 0; i < NW_IRX_FILES; i++) {
        int list_id = irx.ADPCM_FileNames[i].ID;
        if (list_id == id) {
            if (nw_irx_file(GetFileID(id))->size == 0) {
                return 2;
            }
            irx.CD_SPOT_FILE_ID = i;
            SpotFX_LoadAndTransfer(list_id, slot);
            return 0;
        }
        if (list_id == -1) {
            return 1;
        }
    }
    return 1;
}

/* The EE answered slot i's load request: read the first buffer (iStatus 2), key on and read ahead (1), or refill
   the half the voice left (0). */
static void HandleEERequest(int i)
{
    STREAMDATA *s = &irx.StreamBuffer[i];

    if (s->Status != 1) {
        return;
    }
    irx.StreamBusy = 1;
    if (s->iStatus == 2) {
        LoadDataFromDisk(i, (int)s->AllocSize, cdend1);
    } else if (s->iStatus == 1) {
        SetVoiceData(s->Channel, s->Pitch, s->VolL, s->VolR, s->SPUAddress, s->AR);
        nw_irx_key_on(s->Channel);
        if (s->Channel < NW_IRX_CHANNELS) {
            irx.SPU_KeyStatus[s->Channel] = 1;
        }
        if (s->PlayingHalf != 3) {
            LoadDataFromDisk(i, (int)s->AllocHalfSize, cdend3);
        } else {
            s->iStatus = 0;
            irx.StreamBusy = 0;
        }
    } else if (s->iStatus == 0) {
        if (s->PlayingHalf == 0) {
            UpdateStream(i, s->AllocHalfSize, 0, cdend4);
        } else if (s->PlayingHalf == 1) {
            UpdateStream(i, s->AllocHalfSize, 1, cdend5);
        }
    }
}

/* The IRX reads NAX until two reads agree, as it moves while the voice plays. */
static int GetNAX(int core, int ch)
{
    return (int)nw_spu2_get_addr((uint16_t)(core | ch << 1 | NW_SD_VA_NAX));
}

/* One tick of the stream thread. Its CD-not-ready handling (sceCdDiskReady 6 pauses the streams) is left out: the
   disc is always ready here. */
static void StreamADPCM(void)
{
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        STREAMDATA *s = &irx.StreamBuffer[i];
        if (s->Status == 1) {
            int core = s->Channel >= NW_SPU2_VOICES, ch = s->Channel - NW_SPU2_VOICES * core;
            s->SPUCurrent = GetNAX(core, ch);
            s->KeyStat = nw_spu2_get_param((uint16_t)(core | ch << 1 | NW_SD_VP_ENVX));
        }
    }
    if (irx.CD_EVENT_OCCURED) {
        _CdHandleEvent();
    }
    if (irx.IOPRequestLoad != -1 && irx.StreamBuffer[irx.IOPRequestLoad].Status == 0) {
        /* The stream went before the EE answered. The IRX also clears SPU_KeyStatus at the channel of slot 48 (its
           loop index), past its table; left out. */
        irx.IOPRequestLoad = -1;
        irx.LoadFromEE = NW_IRX_NO_REQUEST;
        irx.SYSTEMWAIT = 0;
    }
    if (irx.LoadFromEE != NW_IRX_NO_REQUEST && irx.IOPRequestLoad == (int)irx.LoadFromEE) {
        if (irx.LoadEECounter == irx.MyCounter) {
            int i = (int)irx.LoadFromEE;
            irx.IOPRequestLoad = -1;
            irx.LoadFromEE = NW_IRX_NO_REQUEST;
            HandleEERequest(i);
            irx.MyCounter++;
        }
    } else if (!irx.StreamBusy) {
        /* A slot that needs the CD asks the EE (IOPRequestLoad); its GetStatus echo starts the load next tick. The
           slots are scanned up and down by turns. A start that fails leaves SYSTEMWAIT set, as the IRX does. */
        irx.LoadEECounter = irx.MyCounter;
        int i = irx.Direction ? (int)irx.UserMaxStreams - 1 : 0;
        for (int a = 0; a < (int)irx.UserMaxStreams; a++, i += irx.Direction ? -1 : 1) {
            STREAMDATA *s = &irx.StreamBuffer[i];
            if (irx.SYSTEMWAIT || irx.CD_IDLE != 1 || irx.SYSTEMWAIT2 || irx.IOPRequestLoad != -1 || s->Status != 1) {
                continue;
            }
            if (s->iStatus == 2) {
                int stat = StartStream(i, s->USR_Channel, s->USR_VolL, s->USR_VolR, s->USR_StrID, s->USR_Pitch,
                                       s->USR_LInfo, s->USR_AR);
                irx.SYSTEMWAIT = 1;
                if (stat != -1) {
                    if (s->USR_Channel < NW_IRX_CHANNELS) {
                        irx.SPU_KeyStatus[s->USR_Channel] = 2;
                    }
                    irx.IOPRequestLoad = i;
                }
            } else if (s->iStatus == 1) {
                irx.IOPRequestLoad = i;
            } else if (s->iStatus == 0) {
                int core = s->Channel >= NW_SPU2_VOICES, ch = s->Channel - NW_SPU2_VOICES * core;
                uint32_t StreamAddress = (uint32_t)GetNAX(core, ch), half = s->SPUAddress + s->AllocHalfSize;
                if (s->PlayingHalf == 0) {
                    if (half < StreamAddress) {
                        irx.IOPRequestLoad = i;
                    }
                } else if (s->PlayingHalf == 1 && StreamAddress < half) {
                    irx.IOPRequestLoad = i;
                }
                if (nw_spu2_get_param((uint16_t)(core | ch << 1 | NW_SD_VP_ENVX)) == 0) { /* played out */
                    SetChannelVolume(s->Channel, 0, 0);
                    if (s->Channel < NW_IRX_CHANNELS) {
                        nw_spu2_set_switch((uint16_t)(core | NW_SD_S_KOFF), 1u << ch);
                    }
                    s->Status = 0;
                }
            }
        }
    }
    for (int i = 0; i < (int)irx.UserMaxStreams; i++) { /* resident sample transfers */
        STREAMDATA *s = &irx.StreamBuffer[i];
        if (!irx.SYSTEMWAIT && irx.CD_IDLE == 1 && s->Status == 1 && s->iStatus == 4 &&
            StartStream(i, s->USR_Channel, s->USR_VolL, s->USR_VolR, s->USR_StrID, s->USR_Pitch, s->USR_LInfo,
                        s->USR_AR) != -1) {
            LoadDataFromDisk(i, (int)s->IOPBufferSize, cdend6);
        }
    }
    irx.Direction ^= 1;
}

/* The ticks due since the timer started, all before the call they precede. */
static void nw_iop_catch_up(void)
{
    if (!iop.timer || !iop.now_us) {
        return;
    }
    uint64_t now = iop.now_us();
    if (now < iop.timer_us) {
        return;
    }
    uint64_t due = (now - iop.timer_us) / NW_IRX_TICK_US;
    while (iop.ticks < due) {
        iop.ticks++;
        StreamADPCM();
    }
}

/* The EE's current path (NuFileGetCurrentPath), where the DATs open; each call resets the SPU. */
static void AdpcmSdInit(const uint16_t *p, int length)
{
    nw_irx_string(irx.nu_filepath, sizeof(irx.nu_filepath), p, length);
    strcpy(irx.nu_fileterm, strncmp(irx.nu_filepath, "cdrom0:", 7) == 0 ? ";1" : "");
    nw_spu2_init();
    nw_spu2_set_core_attr(NW_SD_C_SPDIF_MODE, 0x800);
    AdpcmSetMasterVolume(0, 0x3fff);
    AdpcmSetMasterVolume(1, 0x3fff);
    irx.g_ReverbChans[0] = irx.g_ReverbChans[1] = 0;
}

/* count, then per command {cmd, nwords, data[nwords]}, all s16. The commands with a string or a struct take the
   data in place, the others a copy in dt[] read zero-extended. The IRX trusts the counts; here a batch that runs
   past the buffer ends. */
static void AdpcmSetParam(const uint16_t *jdata, size_t words)
{
    uint16_t dt[NW_IRX_DT] = { 0 };
    size_t at = 1;
    int count = words ? (int16_t)jdata[0] : 0;

    for (int i = 0; i < count && at + 2 <= words; i++) {
        int f = (int16_t)jdata[at];
        int length = (int16_t)jdata[at + 1];
        at += 2;
        if (length < 0 || (size_t)length > words - at) {
            return;
        }
        const uint16_t *p = jdata + at;
        switch (f) {
        case 7:
            AdpcmSdInit(p, length);
            break;
        case 0x1e: /* opened when the files are looked up */
            nw_irx_string(irx.new_global_dat_name, sizeof(irx.new_global_dat_name), p, length);
            break;
        case 0x1f:
            nw_irx_string(irx.new_local_dat_name, sizeof(irx.new_local_dat_name), p, length);
            break;
        case 0x1a:
            SoundAddSample(p, length);
            break;
        case 0x1b:
            SoundStoreLocalStart();
            break;
        case 0x1c:
            SoundRestoreLocalStart();
            break;
        case 0x1d:
            InitADPCMFiles(irx.GlobalSfxfileinfo_cnt);
            break;
        default:
            memcpy(dt, p, (size_t)(length < NW_IRX_DT ? length : NW_IRX_DT) * sizeof(*dt));
            break;
        }
        at += (size_t)length;
        switch (f) {
        case 9:
            irx.LoadFromEE = dt[0];
            GetChannelStatus();
            AdpcmGetStatus();
            break;
        case 0x10:
            if (dt[0] < NW_IRX_CHANNELS) {
                irx.UserMaxStreams = dt[0];
            }
            break;
        case 0xe:
            irx.StreamMemoryEnd = (uint32_t)dt[0] << 16 | dt[1];
            break;
        case 0xd:
            ADPCM_AllocIOPMemory(dt[0], (uint32_t)dt[1] << 16 | dt[2], (int)((uint32_t)dt[3] << 16 | dt[4]));
            break;
        case 0xf: /* the IRX frees it under a playing stream too; here an active slot keeps it */
            if (dt[0] < NW_IRX_CHANNELS && !irx.StreamBuffer[dt[0]].Status) {
                free(irx.StreamBuffer[dt[0]].IOPAddress);
                irx.StreamBuffer[dt[0]].IOPAddress = NULL;
            }
            break;
        case 0xc:
            InitStreamData();
            break;
        case 6:
            irx.ResStatus = irx.AnyFiles == 1 ? TransferResidentSampleBank(dt[0], dt[1]) : 1;
            break;
        case 0:
            if (irx.AnyFiles == 1) {
                PlayResidentSFX(dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);
            }
            break;
        case 5:
            if (irx.AnyFiles == 1) {
                StopStream(dt[0]);
            }
            break;
        case 1: /* stream, channel | bank << 8, volumes, pitch, LoopInfo, AR */
            if (irx.AnyFiles == 1) {
                irx.SYSTEMWAIT2 = 1;
                USR_StartStream(dt[1], dt[2], dt[3], dt[0], dt[4], dt[5], dt[6]);
                irx.SYSTEMWAIT2 = 0;
            }
            break;
        case 2:
            SetChannelVolume(dt[0], dt[1], dt[2]);
            break;
        case 3:
            SetChannelPitch(dt[0], dt[1]);
            break;
        case 4: /* StopSound: the key status follows the envelope, the stream slots stay */
            nw_irx_key_off(dt[0]);
            break;
        case 0xa:
            PauseAll();
            break;
        case 0xb:
            ResumeAll();
            break;
        default: /* 8, >= 0x22: no handler */
            break;
        }
    }
}

void nw_nusound_rpc(unsigned fno, const void *send, size_t ssize, void *recv, size_t rsize)
{
    nw_iop_catch_up();
    if (fno == 0 && send) {
        AdpcmSetParam(send, ssize / sizeof(uint16_t));
    }
    if (recv) {
        memcpy(recv, irx.ret, rsize < sizeof(irx.ret) ? rsize : sizeof(irx.ret));
    }
}
