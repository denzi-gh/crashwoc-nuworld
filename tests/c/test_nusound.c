/* The NUSOUND.IRX port (src/audio/nusound.c) behind the SIF shim, driven by the decomp's own nusound.c, on a
   synthetic SFX.DAT and the game's clock, stepped: a vblank is sceGsSyncV, nw_game_spend_us the EE's time between
   them, and every SIF call spends its own. Usage: test_nusound <scratch dir>.
   Local only, with your own disc: `test_nusound retail` runs InitGlobalSfx on the data image and SFX.DAT of the
   extracted disc dir NUWORLD_TEST_DATA, checks the resident bank in SPU RAM and plays the logos' audio stream,
   `test_nusound retail instant` the same off the hardware speed; exits 77 without it. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/irx.h"
#include "audio/nusound.h"
#include "audio/spu2.h"
#include "data/image.h"
#include "nusound/nusound.h"
#include "platform/paths.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/vfs.h"

#include "vag_builder.h"

/* decomp nusound.c (no header declares them) */
int SOUND_InitIOP(void);
void SetIOPCommand(short command, short count);
void SetIOPCommandEx(short command, short size, void *data);
int FlushIOPCommand(int mode);
void SOUND_CopyIOPBuffer(void);
void SOUND_GetStatus(int request);
void SOUND_AddSamples(struct nusound_filename_info_s *sfx_info);
void SOUND_SetMaxStreamLimit(int limit);
void SOUND_SetSPUWriteAddress(unsigned int address);
void SOUND_AllocateStreamBuffer(int stream, unsigned int address, unsigned int size);
void SOUND_InitStreamData(void);
void TransferAllSpotFX(int first);
void SOUND_TransferSFX(int sample, int channel);
void SOUND_StoreLocalStart(void);
void NuSoundLocalSet(NUSOUND_FILENAME_INFO *sfx_info, int unused, int first);
int SOUND_PlaySFX(int sample, unsigned int channel, int left, int right, int pitch, int flags, int priority);
int SOUND_SetChannelVolume(unsigned int channel, int left, int right);
int SOUND_SetChannelPitch(unsigned int channel, int pitch);
int SOUND_StopSound(unsigned int channel);
void SOUND_CloseStreamBuffer(int stream);
void SOUND_Pause(void);
void SOUND_Resume(void);
void SOUND_CheckAndStartStereo(int stream, int left_channel, int right_channel);
void SOUND_CancelCheckStereo(void);
int NuSoundSetChannelPitch(unsigned int channel, int pitch, int stereo);
void SOUND_EnableEffects(int core, int mode, int depth_left, int depth_right, int delay, int feedback);
void SOUND_DisableEffects(int core);
void SOUND_SetEffectMasterVolume(int core, int left, int right);
void SOUND_SetChannelEffectOn(int channel);
void SOUND_SetChannelEffectOff(int channel);
void SOUND_AllChannelEffectsOn(int core);
void SOUND_AllChannelEffectsOff(int core);
void NuSoundKillAllAudio(void);
void NuSoundPlayChan(int sfx, int left, int right, int pitch, int channel);
void InitGlobalSfx(void);
void NuFileInitEx(int deviceid, int rebootiop);
void NuPs2InitIOP(void);
int SOUND_PlayStream(int stream, int bank, unsigned int channel, int left, int right, int pitch, int start, int flags,
                     int priority);
void NuSoundUpdate(void);
int sceGsSyncV(int mode);
extern int nusound_irx_loaded;
extern int Attack, Release;
extern volatile int sbuff2[128];

static int failures;
static NwVfs vfs; /* the scratch disc with SFX.DAT */
#define BIG_BLOCKS 4096
static uint8_t a_vag[NW_VAG_HEADER + 3 * NW_ADPCM_BLOCK], bl_vag[NW_VAG_HEADER + 4 * NW_ADPCM_BLOCK];
static uint8_t big_blocks[BIG_BLOCKS * NW_ADPCM_BLOCK], big_vag[NW_VAG_HEADER + sizeof(big_blocks)];
static size_t a_size, bl_size, big_size; /* sfx\generic\a.vag, sfx\music\bl.vag, sfx\music\big.vag */

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static bool reply_is(int32_t value)
{
    for (int i = 0; i < 128; i++) {
        if (sbuff2[i] != value) {
            return false;
        }
    }
    return true;
}

static void test_transport(void)
{
    nw_nusound_init(NULL, NULL);

    /* Unbound, the call has no server and the reply stays. */
    memset((void *)sbuff2, 0x55, sizeof(sbuff2));
    SetIOPCommand(8, 0);
    CHECK(FlushIOPCommand(0) == 2);
    CHECK(reply_is(0x55555555));

    CHECK(SOUND_InitIOP() == 1);
    SetIOPCommand(8, 0);
    CHECK(FlushIOPCommand(0) == 2);
    CHECK(reply_is(0));
    CHECK(FlushIOPCommand(0) == -2); /* nothing queued */
}

static void test_batch(void)
{
    int32_t reply[4] = { -1, -1, -1, -1 };
    const uint16_t past_end[] = { 2, 8, 100 };     /* the data runs past the batch */
    const uint16_t negative[] = { 1, 8, 0xffff }; /* nwords -1 */

    nw_nusound_init(NULL, NULL);
    nw_nusound_rpc(0, past_end, sizeof(past_end), reply, 8);
    CHECK(reply[0] == 0 && reply[1] == 0 && reply[2] == -1);
    nw_nusound_rpc(0, negative, sizeof(negative), NULL, 0);
    nw_nusound_rpc(1, past_end, sizeof(past_end), reply, sizeof(reply));
    CHECK(reply[3] == 0);
}

/* The SPU on by n samples, outside the module's clock. */
static void spu_samples(unsigned n)
{
    nw_spu2_run_to(nw_spu2_cycle() + 768u * n);
}

/* GetStatus as the EE sends it (-1: no load request). */
static void status(int32_t ret[128])
{
    const uint16_t batch[] = { 1, 9, 1, 0xffff };
    nw_nusound_rpc(0, batch, sizeof(batch), ret, NW_NUSOUND_REPLY);
}

static void test_status(void)
{
    int32_t ret[128];

    nw_nusound_init(NULL, NULL);
    status(ret);
    CHECK(ret[24] == -1);
    for (int i = 0; i < 32; i++) {
        CHECK(i == 24 || ret[i] == 0);
    }

    /* Key status: channel j of a word at bits 30 - 2j; 3 is core 0 voice 3, 24 and 47 core 1 voices 0 and 23. The
       key-on takes the SPU's next sample. */
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 3);
    nw_spu2_set_switch(NW_SD_S_KON | 1, 1u << 0 | 1u << 23);
    status(ret);
    CHECK(ret[29] == 0 && ret[30] == 0 && ret[31] == 0);
    spu_samples(1);
    status(ret);
    CHECK(ret[29] == 1 << 24 && ret[30] == 1 << 14 && ret[31] == 1);

    SOUND_GetStatus(-1);
    CHECK(FlushIOPCommand(0) == 3);
    SOUND_CopyIOPBuffer();
    CHECK(NuSoundKeyStatus(3) == 1 && NuSoundKeyStatus(24) == 1 && NuSoundKeyStatus(47) == 1);
    CHECK(NuSoundKeyStatus(4) == 0);

    /* The reply only changes on a GetStatus. The release (ADSR2 0) takes two samples. */
    nw_spu2_set_switch(NW_SD_S_KOFF, 1u << 3);
    spu_samples(2);
    SetIOPCommand(8, 0);
    FlushIOPCommand(0);
    SOUND_CopyIOPBuffer();
    CHECK(NuSoundKeyStatus(3) == 1);
    SOUND_GetStatus(-1);
    FlushIOPCommand(0);
    SOUND_CopyIOPBuffer();
    CHECK(NuSoundKeyStatus(3) == 0 && NuSoundKeyStatus(24) == 1);
}

/* A string command as the EE queues it. */
static void send_string(short command, const char *s)
{
    SetIOPCommandEx(command, (short)(strlen(s) + 1), (void *)s);
    FlushIOPCommand(0);
}

static void test_init_spu(void)
{
    int32_t ret[128];

    nw_nusound_init(NULL, NULL);
    nw_spu2_set_switch(NW_SD_S_KON, 1u << 5);
    send_string(7, "cdrom0:\\");
    status(ret);
    CHECK(ret[29] == 0);
    for (int core = 0; core < 2; core++) {
        CHECK(nw_spu2_get_param((uint16_t)(core | NW_SD_P_MVOLL)) == 0x3fff);
        CHECK(nw_spu2_get_param((uint16_t)(core | NW_SD_P_MVOLR)) == 0x3fff);
    }
    CHECK(nw_spu2_get_core_attr(NW_SD_C_SPDIF_MODE) == 0x800);
    CHECK(strcmp(nw_nusound_irx()->nu_filepath, "cdrom0:\\") == 0 && strcmp(nw_nusound_irx()->nu_fileterm, ";1") == 0);
    send_string(7, "host0:");
    CHECK(nw_nusound_irx()->nu_fileterm[0] == '\0');
}

static void test_dat_path(void)
{
    const NwIrx *irx = nw_nusound_irx();

    nw_nusound_init(NULL, NULL);
    send_string(0x1e, "SFX.DAT");
    send_string(0x1f, "LOCAL.DAT");
    CHECK(strcmp(irx->new_global_dat_name, "SFX.DAT") == 0 && strcmp(irx->new_local_dat_name, "LOCAL.DAT") == 0);
    send_string(0x1f, "");
    CHECK(irx->new_local_dat_name[0] == '\0');
}

static void test_sample_info(void)
{
    const NwIrx *irx = nw_nusound_irx();
    struct nusound_filename_info_s info[14];

    memset(info, 0, sizeof(info));
    for (int i = 0; i < 13; i++) {
        snprintf(info[i].Filename, sizeof(info[i].Filename), "SFX\\GENERIC\\S%02d.VAG", i);
        info[i].Pitch = 22050;
        info[i].LoopInfo = i & 1;
        info[i].ID = 0x1000 + i;
    }
    nw_nusound_init(NULL, NULL);
    SOUND_AddSamples(info); /* 12 per batch */
    CHECK(irx->fileinfo_cnt == 13);
    CHECK(strcmp(irx->ADPCM_FileNames[12].Filename, "SFX\\GENERIC\\S12.VAG") == 0);
    CHECK(irx->ADPCM_FileNames[12].Pitch == 22050 && irx->ADPCM_FileNames[12].LoopInfo == 0);
    CHECK(irx->ADPCM_FileNames[12].ID == 0x100c && irx->ADPCM_FileNames[13].Filename[0] == '\0');

    /* At most 300; the rest are dropped. */
    uint16_t batch[1 + 30 * 24];
    batch[0] = 30;
    for (int i = 0; i < 30; i++) {
        batch[1 + 24 * i] = 0x1a;
        batch[2 + 24 * i] = 22;
        memcpy(&batch[3 + 24 * i], &info[0], sizeof(info[0]));
    }
    for (int i = 0; i < 10; i++) {
        nw_nusound_rpc(0, batch, sizeof(batch), NULL, 0);
    }
    CHECK(irx->fileinfo_cnt == 300 && irx->ADPCM_FileNames[299].ID == 0x1000);
}

static void test_max_streams(void)
{
    nw_nusound_init(NULL, NULL);
    SOUND_SetMaxStreamLimit(5);
    FlushIOPCommand(0);
    CHECK(nw_nusound_irx()->UserMaxStreams == 5);
    SOUND_SetMaxStreamLimit(48);
    FlushIOPCommand(0);
    CHECK(nw_nusound_irx()->UserMaxStreams == 5);
}

static void test_write_address(void)
{
    nw_nusound_init(NULL, NULL);
    SOUND_SetSPUWriteAddress(0x48310);
    FlushIOPCommand(0);
    CHECK(nw_nusound_irx()->StreamMemoryEnd == 0x48310);
}

static void test_stream_buffers(void)
{
    const STREAMDATA *sb = nw_nusound_irx()->StreamBuffer;

    nw_nusound_init(NULL, NULL);
    for (int i = 0; i < 5; i++) { /* init_adpcm's */
        SOUND_AllocateStreamBuffer(i, 0x5010 + i * 0xd700, 0xd700);
    }
    FlushIOPCommand(0);
    for (int i = 0; i < 5; i++) {
        CHECK(sb[i].IOPAddress && sb[i].AllocSize == 0xd700 && sb[i].AllocHalfSize == 0x6b80);
        CHECK(sb[i].AllocSPUAddress == 0x5010u + i * 0xd700u);
    }
    CHECK(!sb[5].IOPAddress);
    /* Kept while allocated, parameters too. */
    SOUND_AllocateStreamBuffer(0, 0x1000, 0x800);
    FlushIOPCommand(0);
    CHECK(sb[0].AllocSize == 0xd700 && sb[0].AllocSPUAddress == 0x5010);

    /* Closed, a buffer can be allocated anew. */
    SOUND_CloseStreamBuffer(0);
    FlushIOPCommand(0);
    CHECK(!sb[0].IOPAddress);
    SOUND_AllocateStreamBuffer(0, 0x1000, 0x800);
    FlushIOPCommand(0);
    CHECK(sb[0].IOPAddress && sb[0].AllocSize == 0x800 && sb[0].AllocHalfSize == 0x400 && sb[0].AllocSPUAddress == 0x1000);
}

static void test_init_stream_data(void)
{
    const NwIrx *irx = nw_nusound_irx();
    int32_t ret[128];
    struct nusound_filename_info_s info[] = {
        { "SFX\\GENERIC\\A.VAG", 22050, 1, 0x1000 },
        { "SFX\\MUSIC\\BL.VAG", 44100, 0, 1 },
        { "SFX\\GENERIC\\MISSING.VAG", 22050, 1, 0x1001 },
        { "SFX\\GENERIC\\README.TXT", 0, 1, 0x1002 }, /* Pitch 0: unused */
        { "", 0, 0, 0 },
    };

    nw_nusound_init(&vfs, nw_game_us);
    send_string(0x1e, "SFX.DAT");
    send_string(7, "cdrom0:\\");
    SOUND_AddSamples(info);
    SOUND_InitStreamData();
    FlushIOPCommand(0);
    CHECK(irx->global_dat && irx->AnyFiles == 1 && irx->FilesOnCD == 2);
    const ADPCMFILE *f = irx->ADPCM_FileInfo;
    CHECK(f[0].size == a_size && f[0].lsn == 1 && f[0].ID == 0x1000 && f[0].Source == 1);
    CHECK(f[0].datfile == irx->global_dat);
    CHECK(f[1].size == bl_size && f[1].lsn == 2 && f[1].ID == 1);
    CHECK(f[2].size == 0 && f[3].size == 0);
    CHECK(irx->StreamBuffer[47].Channel == 47 && irx->StreamBuffer[47].iStatus == 3);
    CHECK(irx->StreamBuffer[47].Error == 0x8000);

    /* The stream thread ticks at 60 Hz from the call on, caught up at the next one. */
    int direction = irx->Direction;
    nw_game_spend_us(3 * 16666);
    status(ret);
    CHECK(irx->Direction == (direction ^ 1));
    nw_game_spend_us(16666);
    status(ret);
    CHECK(irx->Direction == direction);

    /* Without the DAT nothing is found. */
    nw_nusound_init(&vfs, nw_game_us);
    send_string(0x1e, "NONE.DAT");
    send_string(7, "cdrom0:\\");
    SOUND_AddSamples(info);
    SOUND_InitStreamData();
    FlushIOPCommand(0);
    CHECK(!irx->global_dat && irx->AnyFiles == 0 && irx->ADPCM_FileInfo[0].size == 0);
}

/* What init_adpcm sends, on the cdrom0: path, with info as the sample table; from a vblank, so the module's ticks
   fall the same in every test. */
static void boot_module(struct nusound_filename_info_s *info)
{
    nw_nusound_init(&vfs, nw_game_us);
    sceGsSyncV(0);
    send_string(0x1e, "SFX.DAT");
    send_string(7, "cdrom0:\\");
    SOUND_AddSamples(info);
    SOUND_InitStreamData();
    for (int i = 0; i < 5; i++) {
        SOUND_AllocateStreamBuffer(i, 0x5010 + i * 0xd700, 0xd700);
    }
    SOUND_SetSPUWriteAddress(0x48310);
    SOUND_SetMaxStreamLimit(5);
    FlushIOPCommand(0);
}

static struct nusound_filename_info_s bank[] = {
    { "SFX\\GENERIC\\A.VAG", 22050, 1, 0x1000 },
    { "SFX\\GENERIC\\MISSING.VAG", 22050, 1, 0x1001 },
    { "SFX\\GENERIC\\A.VAG", 11025, 1, 0x1002 },
    { "SFX\\MUSIC\\BL.VAG", 44100, 0, 1 },
    { "", 0, 0, 0 },
};

/* TransferSFX of a resident sample: the ticks until the module reports the transfer done. */
static int transfer_ticks(int id, int slot)
{
    int32_t ret[128];
    int ticks = 0;

    SOUND_TransferSFX(id, slot);
    FlushIOPCommand(0);
    status(ret);
    CHECK(nw_nusound_irx()->ResStatus == 0);
    for (; ticks < 20 && ret[26]; ticks++) {
        nw_game_spend_us(16666);
        status(ret);
    }
    return ticks;
}

/* The drive as PCSX2 times it: the same sample again seeks back a sector from where the drive stopped (half a
   turn, done on its tick), so tick 2 reads it and tick 3 ends it; after the game read the disc the seek is a full
   one again. */
static void test_cd_time(void)
{
    boot_module(bank);
    CHECK(transfer_ticks(0x1000, 0) == 9);
    CHECK(transfer_ticks(0x1000, 1) == 3);
    nw_nusound_cd_moved();
    CHECK(transfer_ticks(0x1000, 2) == 9);

    /* Off the hardware speed the transfer is over by the module's next answer, wherever the head is. */
    nw_nusound_set_hardware_speed(false);
    boot_module(bank);
    CHECK(transfer_ticks(0x1000, 0) == 0);
    CHECK(transfer_ticks(0x1000, 1) == 0);
    nw_nusound_cd_moved();
    CHECK(transfer_ticks(0x1000, 2) == 0);
    nw_nusound_set_hardware_speed(true);
}

static void test_transfer_sfx(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const uint8_t *ram = nw_spu2_ram();
    int32_t ret[128];

    /* One transfer: tick 1 starts it with the drive's first seek, a full one (100 ms); tick 8, the first after it,
       reads the one sector the drive has read ahead by then, tick 9 sends it to SPU RAM and ends it. */
    boot_module(bank);
    CHECK(transfer_ticks(0x1000, 0) == 9);
    CHECK(memcmp(ram + 0x48310, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
    CHECK(irx->StreamMemoryEnd == 0x48310 + a_size && !irx->SPOT_IOPAddress);

    /* The game's loop: loaded, missing (2), loaded again after the first with its header's size, then the end; off
       the hardware speed all within the vblank. */
    for (int hardware = 0; hardware <= 1; hardware++) {
        nw_nusound_set_hardware_speed(hardware);
        boot_module(bank);
        uint64_t edges = nw_game_edges();
        TransferAllSpotFX(0);
        CHECK(irx->ResStatus == 1 && irx->CD_SPOT_TRANS == 0);
        CHECK(irx->ADPCM_SpotFX[0].ID == 0x1000 && irx->ADPCM_SpotFX[0].SPUAddress == 0x48310);
        CHECK(irx->ADPCM_SpotFX[2].ID == 0x1002 && irx->ADPCM_SpotFX[2].SPUAddress == 0x48310 + a_size);
        CHECK(irx->ADPCM_SpotFX[3].ID == -1);
        CHECK(memcmp(ram + 0x48310, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
        CHECK(memcmp(ram + 0x48310 + a_size, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
        CHECK(irx->StreamMemoryEnd == 0x48310 + 2 * a_size);
        CHECK(irx->StreamBuffer[4].Status == 0);
        CHECK(hardware ? nw_game_edges() > edges : nw_game_edges() == edges);
    }

    /* Without files the IRX answers 1 at once. */
    nw_nusound_init(&vfs, nw_game_us);
    SOUND_TransferSFX(0x1000, 0);
    FlushIOPCommand(0);
    status(ret);
    CHECK(ret[22] == 1 && ret[26] == 0);
}

/* NuSoundInit's end, then two levels' banks the way InitLocalSfx sets them. */
static void test_local_bank(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const uint8_t *ram = nw_spu2_ram();
    struct nusound_filename_info_s level1[] = { { "SFX\\GENERIC\\A.VAG", 22050, 1, 0x1004 }, { "", 0, 0, 0 } };
    struct nusound_filename_info_s level2[] = { { "SFX\\MUSIC\\BL.VAG", 22050, 1, 0x1004 }, { "", 0, 0, 0 } };

    boot_module(bank);
    TransferAllSpotFX(0);
    SOUND_StoreLocalStart();
    uint32_t global_end = 0x48310 + 2 * (uint32_t)a_size;
    CHECK(irx->GlobalSfxfileinfo_cnt == 4 && irx->GlobalSfxMemoryEnd == global_end);

    NuSoundLocalSet(level1, 1, 4);
    CHECK(irx->fileinfo_cnt == 5 && irx->ADPCM_FileInfo[4].size == a_size);
    CHECK(irx->ADPCM_SpotFX[4].ID == 0x1004 && irx->ADPCM_SpotFX[4].SPUAddress == global_end);
    CHECK(irx->StreamMemoryEnd == global_end + a_size);

    /* The next level's samples take the same place. */
    NuSoundLocalSet(level2, 1, 4);
    CHECK(irx->fileinfo_cnt == 5 && irx->ADPCM_FileInfo[4].size == bl_size);
    CHECK(irx->StreamMemoryEnd == global_end + bl_size && irx->ADPCM_SpotFX[4].SPUAddress == global_end);
    CHECK(memcmp(ram + global_end, bl_vag + NW_VAG_HEADER, bl_size - NW_VAG_HEADER) == 0);
    CHECK(memcmp(ram + 0x48310, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
}

/* The global bank of `bank` in SPU RAM, as after NuSoundInit. */
static void boot_bank(void)
{
    boot_module(bank);
    TransferAllSpotFX(0);
}

/* Status as the EE sees it after its next flush. */
static void ee_status(void)
{
    SOUND_GetStatus(-1);
    FlushIOPCommand(0);
    SOUND_CopyIOPBuffer();
}

static void test_play_sfx(void)
{
    boot_bank();
    SOUND_PlaySFX(0x1002, 7, 0x3fff, 0x2000, 0x1000, 3, 0x14);
    SOUND_PlaySFX(0x1000, 30, 0x1000, 0x1000, 0x800, 0, 0); /* core 1 voice 6 */
    ee_status();
    /* The key-on waits for the SPU's next sample, after the reply of its own batch. */
    CHECK(NuSoundKeyStatus(7) == 0 && NuSoundKeyStatus(30) == 0);
    CHECK(nw_spu2_get_param(7 << 1 | NW_SD_VP_VOLL) == 0x3fff && nw_spu2_get_param(7 << 1 | NW_SD_VP_VOLR) == 0x2000);
    CHECK(nw_spu2_get_param(7 << 1 | NW_SD_VP_PITCH) == 0x1000 && nw_nusound_irx()->SPU_PausePitch[7] == 0x1000);
    CHECK(nw_spu2_get_param(7 << 1 | NW_SD_VP_ADSR1) == 0x03ff && nw_spu2_get_param(7 << 1 | NW_SD_VP_ADSR2) == 0x1fd4);
    CHECK(nw_spu2_get_addr(7 << 1 | NW_SD_VA_SSA) == 0x48310 + a_size);
    CHECK(nw_spu2_get_addr(1 | 6 << 1 | NW_SD_VA_SSA) == 0x48310);

    /* 0.6 ms on (the wait and the call), both play, the one at pitch 0x1000 in the second block. */
    nw_game_spend_us(500);
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 1 && NuSoundKeyStatus(30) == 1 && NuSoundKeyStatus(8) == 0);
    uint32_t nax = nw_spu2_get_addr(7 << 1 | NW_SD_VA_NAX) - (0x48310 + (uint32_t)a_size);
    CHECK(nax > NW_ADPCM_BLOCK && nax < 2 * NW_ADPCM_BLOCK);

    /* The END block ends a.vag at sample 41 (0.85 ms) at pitch 0x1000, at 81 (1.7 ms) at 0x800. The key status
       drops, and with it the channel is free for find_free_channel again. */
    nw_game_spend_us(500);
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 0 && NuSoundKeyStatus(30) == 1);
    nw_game_spend_us(1000);
    ee_status();
    CHECK(NuSoundKeyStatus(30) == 0);

    /* An ID without a sample still keys on, from -1: the last block of sound RAM, on into the empty start. */
    SOUND_PlaySFX(0x1100, 9, 0x3fff, 0x3fff, 0x1000, 0, 0);
    FlushIOPCommand(0);
    nw_game_spend_us(500);
    ee_status();
    CHECK(NuSoundKeyStatus(9) == 1 && nw_spu2_get_addr(9 << 1 | NW_SD_VA_SSA) == 0x1ffff0);
}

static void test_channel_volume(void)
{
    nw_nusound_init(&vfs, nw_game_us);
    SOUND_SetChannelVolume(5, 0x1234, 0x0567);
    SOUND_SetChannelVolume(40, 0x7fff, 0); /* core 1 voice 16 */
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(5 << 1 | NW_SD_VP_VOLL) == 0x1234 && nw_spu2_get_param(5 << 1 | NW_SD_VP_VOLR) == 0x0567);
    CHECK(nw_spu2_get_param(1 | 16 << 1 | NW_SD_VP_VOLL) == 0x7fff);
    CHECK(nw_spu2_get_param(1 | 16 << 1 | NW_SD_VP_VOLR) == 0);
}

static void test_channel_pitch(void)
{
    nw_nusound_init(&vfs, nw_game_us);
    SOUND_SetChannelPitch(5, 0x0759);
    SOUND_SetChannelPitch(47, 0x3fff); /* core 1 voice 23 */
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(5 << 1 | NW_SD_VP_PITCH) == 0x0759 && nw_nusound_irx()->SPU_PausePitch[5] == 0x0759);
    CHECK(nw_spu2_get_param(1 | 23 << 1 | NW_SD_VP_PITCH) == 0x3fff && nw_nusound_irx()->SPU_PausePitch[47] == 0x3fff);

    /* A stereo pair: the channel and the next. */
    NuSoundSetChannelPitch(2, 0x0eb3, 1);
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(2 << 1 | NW_SD_VP_PITCH) == 0x0eb3 && nw_spu2_get_param(3 << 1 | NW_SD_VP_PITCH) == 0x0eb3);
    CHECK(nw_spu2_get_param(4 << 1 | NW_SD_VP_PITCH) == 0);
}

static void test_stop_sound(void)
{
    boot_bank();
    SOUND_PlaySFX(0x1000, 7, 0x3fff, 0x3fff, 0x1000, 0, 0);
    SOUND_PlaySFX(0x1000, 31, 0x3fff, 0x3fff, 0x1000, 0, 0);
    FlushIOPCommand(0);
    nw_game_spend_us(500);
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 1 && NuSoundKeyStatus(31) == 1);
    /* The IRX waits after KOFF and after the release: the voice is silent by the reply of the same batch. */
    SOUND_StopSound(31);
    ee_status();
    CHECK(NuSoundKeyStatus(7) == 1 && NuSoundKeyStatus(31) == 0);
    CHECK(nw_spu2_get_param(1 | 7 << 1 | NW_SD_VP_ADSR2) == 0x1fc0);
}

static struct {
    unsigned frames, loud; /* frames, and those not silent */
} ee_mix;

static void ee_mix_out(void *user, const int16_t *frames, unsigned count)
{
    (void)user;
    for (unsigned i = 0; i < count; i++, ee_mix.frames++) {
        ee_mix.loud += frames[2 * i] != 0 || frames[2 * i + 1] != 0;
    }
}

/* Every sound gets the EE's envelope (Attack 0, Release 0x10): ADSR1 0x00ff, at full level in 3 samples, and
   ADSR2 0x1fd0, whose slow release the IRX only uses past a stop's KOFF before it writes its fastest; so a stopped
   voice is silent by the reply of its batch. At pitch 0x100 a.vag plays for 14 ms, so the stop ends it. */
static void test_ee_envelope(void)
{
    boot_bank();
    CHECK(Attack == 0 && Release == 0x10);
    nw_spu2_set_output(ee_mix_out, NULL);
    SOUND_PlaySFX(0x1000, 31, 0x3fff, 0x3fff, 0x100, Attack, Release);
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(1 | 7 << 1 | NW_SD_VP_ADSR1) == 0x00ff);
    CHECK(nw_spu2_get_param(1 | 7 << 1 | NW_SD_VP_ADSR2) == 0x1fd0);
    nw_game_spend_us(500);
    ee_status();
    spu_samples(3);
    CHECK(NuSoundKeyStatus(31) == 1 && nw_spu2_get_param(1 | 7 << 1 | NW_SD_VP_ENVX) == 0x7fff && ee_mix.loud > 0);
    SOUND_StopSound(31);
    ee_status();
    ee_mix.frames = ee_mix.loud = 0;
    nw_game_spend_us(500);
    ee_status();
    CHECK(NuSoundKeyStatus(31) == 0 && ee_mix.frames > 20 && ee_mix.loud == 0);
    nw_spu2_set_output(NULL, NULL);
}

static struct nusound_filename_info_s streams[] = {
    { "SFX\\GENERIC\\A.VAG", 22050, 1, 2 }, /* a one-shot */
    { "SFX\\MUSIC\\BL.VAG", 44100, 0, 1 },  /* a loop */
    { "SFX\\MUSIC\\BIG.VAG", 44100, 1, 3 }, /* a one-shot longer than the buffer */
    { "SFX\\MUSIC\\BIG.VAG", 44100, 0, 4 }, /* the same as a loop */
    { "", 0, 0, 0 },
};

/* What one voice decodes, against the file it streams. */
static struct {
    unsigned voice;
    VagWalk walk;
} stream_tap;

static void stream_tap_word(void *user, unsigned voice, const int16_t samples[4])
{
    (void)user;
    if (voice == stream_tap.voice) {
        vag_walk_word(&stream_tap.walk, samples);
    }
}

static void stream_tap_start(unsigned voice, uint32_t count, int32_t loop)
{
    stream_tap.voice = voice;
    vag_walk_init(&stream_tap.walk, big_blocks, count, loop);
    nw_spu2_set_tap(stream_tap_word, NULL);
}

/* A game frame: a vblank of time, then NuSoundUpdate, which answers the module's load request. */
static void frame(void)
{
    sceGsSyncV(0);
    NuSoundUpdate();
}

static void test_play_stream(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const STREAMDATA *s = &irx->StreamBuffer[4];
    const uint32_t spu = 0x5010 + 4 * 0xd700;
    int frames = 0;
    bool starting = false;

    nusound_irx_loaded = 1;
    boot_module(streams);
    /* NuSoundPlayChan's stream on channel 4 (a cut's audio): bank 4, the file's rate (pitch 1), its LoopInfo (2).
       It keys on after two handshakes, the drive's first seek (a full one, 100 ms) and the read; its two blocks at
       22050 Hz are over 89 samples later, before the next reply, so the key status goes from 2 to 0. */
    SOUND_PlayStream(2, 4, 4, 0x3fff, 0x2000, 1, 2, 0, 0x10);
    for (; frames < 50 && (!starting || NuSoundKeyStatus(4) == 2); frames++) {
        frame();
        starting |= NuSoundKeyStatus(4) == 2;
    }
    CHECK(NuSoundKeyStatus(4) == 0 && starting && frames == 15);
    CHECK(s->iStatus == 0 && s->PlayingHalf == 3 && s->LoopInfo == 2);
    CHECK(s->ID == 2 && s->Channel == 4 && s->Pitch == 0x759 && s->SPUAddress == spu);
    CHECK(nw_spu2_get_addr(4 << 1 | NW_SD_VA_SSA) == spu && nw_spu2_get_param(4 << 1 | NW_SD_VP_PITCH) == 0x759);
    CHECK(nw_spu2_get_param(4 << 1 | NW_SD_VP_ADSR2) == 0x1fd0 && nw_spu2_get_param(4 << 1 | NW_SD_VP_ENVX) == 0);
    CHECK(memcmp(nw_spu2_ram() + spu, a_vag + NW_VAG_HEADER, a_size - NW_VAG_HEADER) == 0);
    /* A tick finds it played out: no volume, keyed off, the slot free. */
    frame();
    CHECK(s->Status == 0 && nw_spu2_get_param(4 << 1 | NW_SD_VP_VOLR) == 0);

    /* A stream longer than its buffer, on slot 0: the whole buffer as a ring in SPU RAM, then the rest (less than
       a half) read ahead with the end's flags set, waiting for the voice to leave the first half. Its sectors are
       near a.vag's, a short seek. The buffer's DMA to SPU RAM (18 ms) holds SYSTEMWAIT, and the key-on waits. */
    const STREAMDATA *s0 = &irx->StreamBuffer[0];
    const uint8_t *ram = nw_spu2_ram() + 0x5010;
    bool held = false;
    stream_tap_start(0, BIG_BLOCKS - 1, -1);
    SOUND_PlayStream(3, 0, 0, 0x3fff, 0, 1, 2, 0, 0x10);
    for (frames = 0; frames < 50 && NuSoundKeyStatus(0) != 1; frames++) {
        frame();
        held |= s0->iStatus == 1 && irx->SYSTEMWAIT && irx->IOPRequestLoad == -1;
    }
    CHECK(frames == 14 && s0->iStatus == 1 && held); /* keyed on, reading ahead */
    for (; frames < 50 && s0->iStatus != 0; frames++) {
        frame();
    }
    CHECK(frames == 16 && NuSoundKeyStatus(0) == 1 && s0->PlayingHalf == 0 && s0->LoopInfo == 2);
    CHECK(s0->Pitch == 0xeb3 && s0->FileSeek == 0x30 + 0xd700);
    CHECK(ram[1] == (NW_ADPCM_START | NW_ADPCM_REPEAT) && ram[0x11] == NW_ADPCM_REPEAT);
    CHECK(ram[0xd700 - 0xf] == (NW_ADPCM_END | NW_ADPCM_REPEAT));
    CHECK(memcmp(ram + 0x20, big_blocks + 0x20, 0xd700 - 0x30) == 0);
    CHECK(memcmp(s0->IOPAddress, big_blocks + 0xd700, sizeof(big_blocks) - 0xd700) == 0);

    /* The voice leaves the first half (1720 blocks at 0xeb3: 1.09 s, 55 frames), and two handshakes later the rest
       is there, from the file's block 3440 on. It plays out at the file's END block, 4095 blocks (2.6 s, 130 frames)
       after the key-on. */
    uint32_t block;
    for (; frames < 300 && s0->PlayingHalf == 0; frames++) {
        frame();
    }
    memcpy(&block, ram + 2, sizeof(block));
    CHECK(frames == 71 && s0->PlayingHalf == 1 && NuSoundKeyStatus(0) == 1 && block == 0xd700 / NW_ADPCM_BLOCK);
    for (; frames < 300 && NuSoundKeyStatus(0) == 1; frames++) {
        frame();
    }
    CHECK(frames == 144 && nw_spu2_get_param(NW_SD_VP_ENVX) == 0);
    frame();
    CHECK(s0->Status == 0);
    /* The voice decoded the file through its END block, every block once, across the refill. */
    CHECK(stream_tap.walk.words == (BIG_BLOCKS - 1) * 7 && stream_tap.walk.bad == 0);
    nw_spu2_set_tap(NULL, NULL);
    nusound_irx_loaded = 0;
}

/* test_play_stream's long stream off the hardware speed: no seek, read or DMA time, so it keys on 3 frames sooner
   and SYSTEMWAIT never holds it. */
static void test_play_stream_instant(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const STREAMDATA *s0 = &irx->StreamBuffer[0];
    int frames = 0;
    bool held = false;

    nusound_irx_loaded = 1;
    nw_nusound_set_hardware_speed(false);
    boot_module(streams);
    SOUND_PlayStream(3, 0, 0, 0x3fff, 0, 1, 2, 0, 0x10);
    for (; frames < 50 && NuSoundKeyStatus(0) != 1; frames++) {
        frame();
        held |= s0->iStatus == 1 && irx->SYSTEMWAIT && irx->IOPRequestLoad == -1;
    }
    CHECK(frames == 11 && s0->iStatus == 1 && !held);
    for (; frames < 50 && s0->iStatus != 0; frames++) {
        frame();
    }
    CHECK(frames == 13 && NuSoundKeyStatus(0) == 1 && s0->PlayingHalf == 0);
    CHECK(memcmp(s0->IOPAddress, big_blocks + 0xd700, sizeof(big_blocks) - 0xd700) == 0);
    nw_nusound_set_hardware_speed(true);
    nusound_irx_loaded = 0;
}

/* A loop longer than its buffer, on slot 0: each refill of a half carries the flags that close the ring (the second
   half's ends on END|REPEAT), so the voice goes round the buffer, 3440 blocks at 0xeb3 (109 frames) a turn. The
   file is a one-shot: at its end the read clears the END block's flags and goes on from the file's start, so the
   voice decodes the whole file (with its last two blocks) over and over, without a gap. */
static void test_stream_ring(void)
{
    const NwIrx *irx = nw_nusound_irx();
    const STREAMDATA *s = &irx->StreamBuffer[0];
    const uint8_t *ram = nw_spu2_ram() + 0x5010;
    int refills = 0, half = 0;
    bool inside = true;

    nusound_irx_loaded = 1;
    boot_module(streams);
    stream_tap_start(0, BIG_BLOCKS, 0);
    SOUND_PlayStream(4, 0, 0, 0x3fff, 0, 1, 2, 0, 0x10);
    for (int frames = 0; frames < 400; frames++) {
        frame();
        inside &= NuSoundKeyStatus(0) != 1 || nw_spu2_get_addr(NW_SD_VA_NAX) - 0x5010 < 0xd700;
        if (s->PlayingHalf != half) {
            half = s->PlayingHalf;
            if (++refills == 2) {
                CHECK(ram[0x6b80 + 1] == NW_ADPCM_REPEAT && ram[0xd700 - 0xf] == (NW_ADPCM_END | NW_ADPCM_REPEAT));
            }
        }
    }
    CHECK(inside && refills == 6 && NuSoundKeyStatus(0) == 1 && s->Status == 1 && s->LoopInfo == 0);
    CHECK(stream_tap.walk.words > 2 * BIG_BLOCKS * 7 && stream_tap.walk.bad == 0);
    nw_spu2_set_tap(NULL, NULL);
    nusound_irx_loaded = 0;
}

/* NuSoundPlayStereo's way: both halves at pitch 0 on channels 0 and 1, the first keyed on waits there, then the
   pitch once both stream. */
static void test_stereo(void)
{
    const NwIrx *irx = nw_nusound_irx();
    int frames = 0;
    bool waiting = false;

    nusound_irx_loaded = 1;
    boot_module(streams);
    SOUND_PlayStream(2, 0, 0, 0x3fff, 0, 0, 2, 0, 0x10);
    SOUND_PlayStream(3, 1, 1, 0, 0x3fff, 0, 2, 0, 0x10);
    SOUND_CheckAndStartStereo(0, 1, 0xeb3);
    do {
        frame();
        waiting |= irx->ret[28] == 1 && (NuSoundKeyStatus(0) == 1 || NuSoundKeyStatus(1) == 1) &&
                   nw_spu2_get_param(0 | NW_SD_VP_PITCH) == 0 && nw_spu2_get_param(1 << 1 | NW_SD_VP_PITCH) == 0;
    } while (++frames < 50 && irx->ret[28] != 0);
    CHECK(waiting && irx->StereoStarted == 0);
    CHECK(nw_spu2_get_param(0 | NW_SD_VP_PITCH) == 0xeb3 && nw_spu2_get_param(1 << 1 | NW_SD_VP_PITCH) == 0xeb3);
    CHECK(irx->SPU_PausePitch[0] == 0xeb3 && irx->SPU_PausePitch[1] == 0xeb3);

    /* Cancelled, nothing changes the pitch. */
    SOUND_SetChannelPitch(0, 0);
    SOUND_CheckAndStartStereo(0, 1, 0x1000);
    SOUND_CancelCheckStereo();
    frame();
    frame();
    CHECK(irx->StereoStarted == 0 && nw_spu2_get_param(0 | NW_SD_VP_PITCH) == 0);
    nusound_irx_loaded = 0;
}

static void test_reverb(void)
{
    const NwIrx *irx = nw_nusound_irx();

    nw_nusound_init(&vfs, nw_game_us);
    SOUND_EnableEffects(0, 6, 0x3fff, 0x2000, 0, 0);
    SOUND_SetEffectMasterVolume(0, 0x1000, 0x0800);
    SOUND_EnableEffects(1, 3, -0x1000, 0x1000, 0x40, 0x50); /* below core 0's work area */
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_addr(NW_SD_A_EEA) == 0xfffff && nw_spu2_get_addr(1 | NW_SD_A_EEA) == 0xdffff);
    CHECK(nw_spu2_get_param(NW_SD_P_EVOLL) == 0x1000 && nw_spu2_get_param(NW_SD_P_EVOLR) == 0x0800);
    CHECK(nw_spu2_get_param(1 | NW_SD_P_EVOLL) == 0 && nw_spu2_get_core_attr(1 | NW_SD_C_EFFECT_ENABLE) == 1);
    NwSpu2Effect e = nw_spu2_get_effect_attr(1);
    CHECK(e.mode == 0x103 && e.depth_L == -0x1000 && e.depth_R == 0x1000 && e.delay == 0x40 && e.feedback == 0x50);
    CHECK(irx->CurrentReverb[0] == 6 && irx->CurrentRevDepL[0] == 0x3fff);

    SOUND_SetChannelEffectOn(5);
    SOUND_SetChannelEffectOn(30); /* core 1 voice 6 */
    SOUND_SetChannelEffectOn(7);
    SOUND_SetChannelEffectOff(5);
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_switch(NW_SD_S_VMIXEL) == 1u << 7 && nw_spu2_get_switch(NW_SD_S_VMIXER) == 1u << 7);
    CHECK(nw_spu2_get_switch(1 | NW_SD_S_VMIXEL) == 1u << 6);
    SOUND_AllChannelEffectsOn(1);
    SOUND_AllChannelEffectsOff(0);
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_switch(1 | NW_SD_S_VMIXER) == 0xffffff && nw_spu2_get_switch(NW_SD_S_VMIXEL) == 0);

    SOUND_DisableEffects(0);
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_core_attr(NW_SD_C_EFFECT_ENABLE) == 0 && irx->CurrentReverb[0] == 0);
    CHECK(nw_spu2_get_param(NW_SD_P_EVOLL) == 0 && nw_spu2_get_effect_attr(0).mode == 0x100);
}

/* NuSoundKillAllAudio: shutoff seen raised, then cleared by the module's next tick; its vblank waits run on a
   stepped game clock. */
static void test_kill_all(void)
{
    const NwIrx *irx = nw_nusound_irx();
    int frames = 0;

    nusound_irx_loaded = 1;
    boot_module(streams);
    SOUND_PlayStream(3, 4, 4, 0x3fff, 0x3fff, 1, 2, 0, 0x10);
    SOUND_PlaySFX(0x1000, 7, 0x3fff, 0x3fff, 0x1000, 0, 0); /* no bank here: from -1, on into empty RAM */
    while (frames++ < 50 && NuSoundKeyStatus(4) != 1) {
        frame();
    }
    CHECK(NuSoundKeyStatus(4) == 1 && nw_spu2_get_param(4 << 1 | NW_SD_VP_ENVX) != 0);
    NuSoundKillAllAudio();
    CHECK(irx->StreamShutDown == 0 && irx->StreamBuffer[4].Status == 0 && irx->StreamBuffer[4].iStatus == 3);
    CHECK(nw_spu2_get_param(4 << 1 | NW_SD_VP_ENVX) == 0 && nw_spu2_get_param(4 << 1 | NW_SD_VP_PITCH) == 0);
    ee_status();
    CHECK(NuSoundKeyStatus(4) == 0 && NuSoundKeyStatus(7) == 0);
    nusound_irx_loaded = 0;
}

static void test_pause(void)
{
    nw_nusound_init(&vfs, nw_game_us);
    SOUND_SetChannelPitch(3, 0x1000);
    SOUND_SetChannelPitch(45, 0x0800); /* core 1 voice 21 */
    SOUND_Pause();
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(3 << 1 | NW_SD_VP_PITCH) == 0 && nw_spu2_get_param(1 | 21 << 1 | NW_SD_VP_PITCH) == 0);
    SOUND_Resume();
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(3 << 1 | NW_SD_VP_PITCH) == 0x1000);
    CHECK(nw_spu2_get_param(1 | 21 << 1 | NW_SD_VP_PITCH) == 0x0800);

    /* Paused twice, the resume finds the zeros. */
    SOUND_Pause();
    SOUND_Pause();
    SOUND_Resume();
    FlushIOPCommand(0);
    CHECK(nw_spu2_get_param(3 << 1 | NW_SD_VP_PITCH) == 0);
}

/* CutSceneNextFunction's way: stop the cut's stream, then wait a vblank at a time for its key status to drop. The
   stop keys off without waiting, so the reply of its own batch still finds the voice up, and the EE reads each
   reply at its next flush: two vblanks. */
static void test_stop_stream(void)
{
    const NwIrx *irx = nw_nusound_irx();
    int frames = 0;

    nusound_irx_loaded = 1;
    boot_module(streams);
    SOUND_PlayStream(3, 4, 4, 0x3fff, 0x3fff, 1, 2, 0, 0x10);
    SOUND_PlayStream(3, 0, 0, 0x3fff, 0, 1, 2, 0, 0x10);
    while (frames++ < 50 && (NuSoundKeyStatus(4) != 1 || NuSoundKeyStatus(0) != 1)) {
        frame();
    }
    NuSoundStopStream(4);
    for (frames = 0; frames < 50; frames++) {
        NuSoundUpdate();
        if (NuSoundKeyStatus(4) != 1) {
            break;
        }
        sceGsSyncV(0);
    }
    CHECK(frames == 2 && NuSoundKeyStatus(0) == 1);
    CHECK(irx->StreamBuffer[4].Status == 0 && irx->StreamBuffer[4].iStatus == 3);
    CHECK(nw_spu2_get_param(4 << 1 | NW_SD_VP_PITCH) == 0 && nw_spu2_get_param(4 << 1 | NW_SD_VP_ENVX) == 0);

    /* By slot (0x40 | slot), and a stream that is gone before the EE answers its request. */
    NuSoundStopStream(0x40);
    SOUND_PlayStream(2, 4, 4, 0x3fff, 0x3fff, 1, 2, 0, 0x10);
    FlushIOPCommand(0);
    for (frames = 0; frames < 50 && irx->IOPRequestLoad != 4; frames++) {
        nw_game_spend_us(16666);
        status(NULL);
    }
    NuSoundStopStream(4);
    FlushIOPCommand(0);
    nw_game_spend_us(16666);
    status(NULL);
    CHECK(irx->StreamBuffer[0].Status == 0 && irx->IOPRequestLoad == -1 && irx->LoadFromEE == 0xffff);
    nusound_irx_loaded = 0;
}

/* sfx\generic\a.vag: a one-shot of 3 blocks; sfx\music\bl.vag: a loop of 4; a text file; sfx\music\big.vag: a
   one-shot of BIG_BLOCKS, more than a stream buffer and a half, each block numbered in its nibbles. */
static bool setup_disc(const char *scratch)
{
    static const char text[] = "a text file in SFX.DAT, longer than a VAG header of 48 bytes";
    uint8_t blocks[4][NW_ADPCM_BLOCK];
    char dir[1024], path[1100];

    for (uint32_t b = 0; b < BIG_BLOCKS; b++) {
        uint8_t *p = big_blocks + (size_t)b * NW_ADPCM_BLOCK;
        unsigned flags = b == BIG_BLOCKS - 2 ? NW_ADPCM_END : b == BIG_BLOCKS - 1 ? 7 : 0;
        vag_block_fill(p, 0, 0, flags, 0);
        memcpy(p + 2, &b, sizeof(b));
    }
    big_size = vag_file(big_vag, 44100, big_blocks, BIG_BLOCKS);

    vag_block_fill(blocks[0], 0, 0, 0, 1);
    vag_block_fill(blocks[1], 1, 12, NW_ADPCM_END, 0);
    vag_block_fill(blocks[2], 0, 0, 7, 0);
    a_size = vag_file(a_vag, 22050, blocks[0], 3);
    vag_block_fill(blocks[1], 2, 3, NW_ADPCM_START | NW_ADPCM_REPEAT, -5);
    vag_block_fill(blocks[2], 3, 5, NW_ADPCM_REPEAT, 3);
    vag_block_fill(blocks[3], 4, 8, NW_ADPCM_END | NW_ADPCM_REPEAT, -2);
    bl_size = vag_file(bl_vag, 44100, blocks[0], 4);
    const uint8_t *const files[SFX_DAT_FILES] = { a_vag, bl_vag, (const uint8_t *)text, big_vag };
    const size_t sizes[SFX_DAT_FILES] = { a_size, bl_size, sizeof(text) - 1, big_size };
    uint8_t *arc = SDL_malloc(sfx_dat_size(sizes));
    size_t size = arc ? sfx_dat(arc, files, sizes) : 0;

    SDL_snprintf(dir, sizeof(dir), "%s/disc", scratch);
    SDL_snprintf(path, sizeof(path), "%s/SFX.DAT", dir);
    bool ok = arc && SDL_CreateDirectory(dir) && SDL_SaveFile(path, arc, size) && nw_vfs_mount(&vfs, NW_VFS_DISC, dir);
    SDL_free(arc);
    return ok;
}

/* ---- Retail (local only) ------------------------------------------------------------------------------- */

static int test_retail(bool hardware)
{
    const char *dir = getenv("NUWORLD_TEST_DATA");
    char path[1024], err[512];
    NwImageStats stats;
    size_t size = 0;

    if (!dir || !*dir) {
        printf("NUWORLD_TEST_DATA is not set: skipped\n");
        return 77;
    }
    SDL_snprintf(path, sizeof(path), "%s/SLES_503.86", dir);
    void *elf = SDL_LoadFile(path, &size);
    if (!elf || !nw_vfs_mount(&vfs, NW_VFS_DISC, dir)) {
        fprintf(stderr, "NUWORLD_TEST_DATA: %s\n", SDL_GetError());
        return 1;
    }
    nw_shim_file_init(&vfs);
    nw_newlib_init();
    bool loaded = nw_image_load(&nw_image_tables, elf, size, &stats, err, sizeof(err));
    SDL_free(elf);
    if (!loaded) {
        fprintf(stderr, "data image: %s\n", err);
        return 1;
    }

    /* The game's first load: the global table, its DAT, every resident sample into SPU RAM; 642 vblanks (12.8 s) at
       the hardware speed, 2 off it. */
    const NwIrx *irx = nw_nusound_irx();
    nw_nusound_set_hardware_speed(hardware);
    nw_nusound_init(&vfs, nw_game_us);
    NuFileInitEx(1, 0);
    NuPs2InitIOP();
    uint64_t edges = nw_game_edges();
    InitGlobalSfx();
    edges = nw_game_edges() - edges;
    printf("InitGlobalSfx: %llu vblanks\n", (unsigned long long)edges);
    CHECK(edges == (hardware ? 642 : 2));
    CHECK(irx->global_dat && irx->fileinfo_cnt == 0xc5 && irx->GlobalSfxfileinfo_cnt == 0xc5 && irx->ResStatus == 1);
    uint32_t addr = 0x48310;
    int slot = 0;
    for (; slot < NW_IRX_FILES && irx->ADPCM_SpotFX[slot].ID == 0x1000 + slot; slot++) {
        int ix = 0;
        while (ix < irx->fileinfo_cnt && irx->ADPCM_FileNames[ix].ID != 0x1000 + slot) {
            ix++;
        }
        int entry = nw_dat_find(irx->global_dat, irx->ADPCM_FileNames[ix].Filename);
        uint32_t len = entry >= 0 ? irx->global_dat->entries[entry].size : 0;
        uint8_t *data = SDL_malloc(len ? len : 1);
        CHECK(entry >= 0 && irx->ADPCM_SpotFX[slot].SPUAddress == addr && data &&
              nw_dat_read(irx->global_dat, entry, 0, data, len) &&
              memcmp(nw_spu2_ram() + addr, data + NW_VAG_HEADER, len - NW_VAG_HEADER) == 0);
        SDL_free(data);
        addr += len;
    }
    printf("SFX.DAT: %d of %d samples resident, SPU RAM 0x48310-%#x\n", slot, irx->fileinfo_cnt, addr);
    CHECK(slot == 158 && irx->StreamMemoryEnd == addr && irx->GlobalSfxMemoryEnd == addr);
    CHECK(addr == 0x187b40);

    /* StartCutMovie's logos audio: sfx 172, a stream, on channel 4, until its key status is 1; off the hardware
       speed without the drive's time. */
    int frames = 0;
    bool starting = false;
    NuSoundPlayChan(172, 0x3fff, 0x3fff, 1, 4);
    for (; frames < 100 && NuSoundKeyStatus(4) != 1; frames++) {
        frame();
        starting |= NuSoundKeyStatus(4) == 2;
    }
    printf("logos stream: id %#x, keyed on after %d frames\n", irx->StreamBuffer[4].ID, frames);
    CHECK(starting && NuSoundKeyStatus(4) == 1 && irx->StreamBuffer[4].ID == 0xe && frames == (hardware ? 19 : 13));

    /* It plays through its buffer's refills to the file's END block: 799092 samples at 22050 Hz, 1812.6 frames. On
       PCSX2 the key status of channel 4 drops 1812 vblanks after it went to 1. */
    int keyed = frames;
    for (; frames < 3000 && NuSoundKeyStatus(4) == 1; frames++) {
        frame();
    }
    printf("logos stream: played out %d frames after its key-on\n", frames - keyed);
    CHECK(frames - keyed == 1812);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (!nw_game_init(NW_CLOCK_STEPPED, 50)) {
        fprintf(stderr, "no game clock: %s\n", SDL_GetError());
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "retail") == 0) {
        return test_retail(!(argc == 3 && strcmp(argv[2], "instant") == 0));
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_nusound <scratch dir> | retail\n");
        return 2;
    }
    if (!nw_paths_remove_tree(argv[1]) || !SDL_CreateDirectory(argv[1]) || !setup_disc(argv[1])) {
        fprintf(stderr, "%s: %s\n", argv[1], SDL_GetError());
        return 1;
    }
    test_transport();
    test_batch();
    test_status();
    test_init_spu();
    test_dat_path();
    test_sample_info();
    test_max_streams();
    test_write_address();
    test_stream_buffers();
    test_init_stream_data();
    test_transfer_sfx();
    test_cd_time();
    test_local_bank();
    test_play_sfx();
    test_channel_volume();
    test_channel_pitch();
    test_stop_sound();
    test_ee_envelope();
    test_play_stream();
    test_play_stream_instant();
    test_stream_ring();
    test_stop_stream();
    test_pause();
    test_stereo();
    test_reverb();
    test_kill_all();
    nw_nusound_init(NULL, NULL);
    nw_game_shutdown();
    nw_vfs_shutdown(&vfs);
    nw_paths_remove_tree(argv[1]);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("nusound: all checks passed\n");
    return 0;
}
