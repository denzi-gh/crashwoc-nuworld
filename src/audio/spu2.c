#include "audio/spu2.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/vag.h"

#define NW_SPU2_ENV_MAX 0x7fff
#define NW_SPU2_TICK 768         /* IOP cycles a sample */
#define NW_SPU2_WORD_MASK 0xfffffu /* sound RAM in 16-bit words */
#define NW_SPU2_BLOCK_MASK 0xffff8u
#define NW_SPU2_ALL_VOICES 0xffffffu
#define NW_SPU2_FIFO 32
#define NW_SPU2_BATCH 256 /* frames handed to the output at once */
#define NW_SPU2_FIR_TAPS 39
#define NW_SPU2_FIR_RING 64

static const int16_t nw_spu2_gauss[256][4] = {
#include "audio/spu2_gauss.inc"
};

/* PCSX2's half-band filter around the reverb's 24 kHz (ReverbResample.cpp, its reference version); the way up
   takes it doubled. */
static const int16_t nw_spu2_fir[NW_SPU2_FIR_TAPS] = {
    -1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0, 10246, 16384,
    10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1,
};

/* The reverb's address registers (in words) and volumes, each in register order. */
enum {
    NW_REVB_APF1_SIZE, NW_REVB_APF2_SIZE, NW_REVB_SAME_L_DST, NW_REVB_SAME_R_DST, NW_REVB_COMB1_L_SRC,
    NW_REVB_COMB1_R_SRC, NW_REVB_COMB2_L_SRC, NW_REVB_COMB2_R_SRC, NW_REVB_SAME_L_SRC, NW_REVB_SAME_R_SRC,
    NW_REVB_DIFF_L_DST, NW_REVB_DIFF_R_DST, NW_REVB_COMB3_L_SRC, NW_REVB_COMB3_R_SRC, NW_REVB_COMB4_L_SRC,
    NW_REVB_COMB4_R_SRC, NW_REVB_DIFF_L_SRC, NW_REVB_DIFF_R_SRC, NW_REVB_APF1_L_DST, NW_REVB_APF1_R_DST,
    NW_REVB_APF2_L_DST, NW_REVB_APF2_R_DST, NW_REVB_ADDRS
};
enum {
    NW_REVB_IIR_VOL, NW_REVB_COMB1_VOL, NW_REVB_COMB2_VOL, NW_REVB_COMB3_VOL, NW_REVB_COMB4_VOL, NW_REVB_WALL_VOL,
    NW_REVB_APF1_VOL, NW_REVB_APF2_VOL, NW_REVB_IN_COEF_L, NW_REVB_IN_COEF_R, NW_REVB_VOLS
};

/* The envelope's phases (PCSX2 V_ADSR::PHASE_*). */
enum { NW_ADSR_STOPPED, NW_ADSR_ATTACK, NW_ADSR_DECAY, NW_ADSR_SUSTAIN, NW_ADSR_RELEASE };

typedef struct NwSpu2Voice {
    uint16_t param[16]; /* by register (entry >> 8) */
    uint32_t start;     /* SSA, LSAX and NAX, in words; NAX is the word the decoder reads next */
    uint32_t loop;
    uint32_t next;
    bool loop_set;      /* LSAX written since the key-on: START flags leave it */
    uint8_t flags;      /* the loop flags of the block at NAX */
    uint32_t sp;        /* the pitch counter, 12 fraction bits */
    uint32_t decoded;   /* samples decoded and played since the key-on */
    uint32_t played;
    uint8_t phase;
    uint32_t counter;
    int32_t level; /* ENVX */
    int32_t vol[2]; /* VOLL and VOLR as they apply, and their sweeps' counters */
    uint32_t vol_counter[2];
    NwAdpcm adpcm;
    bool block_ready; /* the block at NAX is in pcm */
    int16_t pcm[NW_ADPCM_SAMPLES];
    int16_t fifo[NW_SPU2_FIFO]; /* decoded samples by count */
} NwSpu2Voice;

typedef struct NwSpu2Core {
    NwSpu2Voice voice[NW_SPU2_VOICES];
    uint16_t param[16]; /* core registers by register */
    uint32_t sw[32];    /* switches by register */
    uint32_t key_on;    /* KON and KOFF for the next sample */
    uint32_t key_off;
    uint32_t addr[4]; /* ESA, EEA, TSA, IRQA */
    uint16_t attr[8]; /* core attributes by (entry >> 1) */
    NwSpu2Effect effect;
    uint32_t revb_addr[NW_REVB_ADDRS];
    int16_t revb_vol[NW_REVB_VOLS];
    int16_t revb_down[2][2 * NW_SPU2_FIR_RING]; /* each ring twice over, so the filter reads it in one piece */
    int16_t revb_up[2][2 * NW_SPU2_FIR_RING];
    unsigned revb_pos;
} NwSpu2Core;

static struct {
    NwSpu2Core core[2];
} spu;

static NwSpu2Presets nw_spu2_presets;
static SDL_AtomicInt nw_spu2_cut[2 * NW_SPU2_VOICES]; /* NW_SPU2_UNITY less the port's gain */

/* How far the SPU ran, the cycle of its last sample and the samples so far (PCSX2's Cycles). */
static struct {
    uint64_t cycle;
    uint64_t sample;
    uint32_t ticks;
} nw_spu2_clock;

static uint8_t nw_spu2_mem[NW_SPU2_RAM];

static struct {
    NwSpu2Output out;
    void *out_user;
    NwSpu2Tap tap;
    void *tap_user;
    int16_t frames[NW_SPU2_BATCH * 2];
    unsigned count;
} nw_spu2_io;

static unsigned nw_spu2_reg(uint16_t entry)
{
    return (entry >> 8) & 0x1f;
}

/* libsd's InitVoices and InitCoreVolume: every voice dry and wet, core 0 into core 1 at full volume. */
void nw_spu2_init(void)
{
    memset(&spu, 0, sizeof(spu));
    for (int c = 0; c < 2; c++) {
        uint32_t *sw = spu.core[c].sw;
        sw[nw_spu2_reg(NW_SD_S_VMIXL)] = sw[nw_spu2_reg(NW_SD_S_VMIXR)] = NW_SPU2_ALL_VOICES;
        sw[nw_spu2_reg(NW_SD_S_VMIXEL)] = sw[nw_spu2_reg(NW_SD_S_VMIXER)] = NW_SPU2_ALL_VOICES;
    }
    spu.core[0].param[nw_spu2_reg(NW_SD_P_MMIX)] = 0xff0;
    spu.core[1].param[nw_spu2_reg(NW_SD_P_MMIX)] = 0xffc;
    spu.core[1].param[nw_spu2_reg(NW_SD_P_AVOLL)] = spu.core[1].param[nw_spu2_reg(NW_SD_P_AVOLR)] = 0x7fff;
}

void nw_spu2_set_presets(const NwSpu2Presets *presets)
{
    if (presets) {
        nw_spu2_presets = *presets;
    } else {
        memset(&nw_spu2_presets, 0, sizeof(nw_spu2_presets));
    }
}

void nw_spu2_set_output(NwSpu2Output out, void *user)
{
    nw_spu2_io.out = out;
    nw_spu2_io.out_user = user;
    nw_spu2_io.count = 0;
}

void nw_spu2_set_gain(unsigned voice, int gain)
{
    if (voice < 2 * NW_SPU2_VOICES) {
        SDL_SetAtomicInt(&nw_spu2_cut[voice], NW_SPU2_UNITY - SDL_clamp(gain, 0, NW_SPU2_UNITY));
    }
}

void nw_spu2_set_tap(NwSpu2Tap tap, void *user)
{
    nw_spu2_io.tap = tap;
    nw_spu2_io.tap_user = user;
}

static NwSpu2Voice *nw_spu2_voice(uint16_t entry)
{
    unsigned voice = (entry >> 1) & 0x1f;
    return voice < NW_SPU2_VOICES ? &spu.core[entry & 1].voice[voice] : NULL;
}

static uint16_t *nw_spu2_param(uint16_t entry)
{
    if (entry & 0x80) {
        return &spu.core[entry & 1].param[nw_spu2_reg(entry) & 0xf];
    }
    NwSpu2Voice *v = nw_spu2_voice(entry);
    return v ? &v->param[nw_spu2_reg(entry) & 0xf] : NULL;
}

/* A voice's VOLL or VOLR applies at once when fixed (V_VolumeSlide::RegSet); a sweep starts from where it is. */
void nw_spu2_set_param(uint16_t entry, uint16_t value)
{
    uint16_t *p = nw_spu2_param(entry);
    if (p) {
        *p = value;
    }
    unsigned reg = nw_spu2_reg(entry);
    NwSpu2Voice *v = entry & 0x80 ? NULL : nw_spu2_voice(entry);
    if (v && reg <= nw_spu2_reg(NW_SD_VP_VOLR) && !(value & 0x8000)) {
        v->vol[reg] = (int16_t)(uint16_t)(value << 1);
    }
}

uint16_t nw_spu2_get_param(uint16_t entry)
{
    if (!(entry & 0x80) && nw_spu2_reg(entry) == nw_spu2_reg(NW_SD_VP_ENVX)) {
        const NwSpu2Voice *v = nw_spu2_voice(entry);
        return v ? (uint16_t)v->level : 0;
    }
    const uint16_t *p = nw_spu2_param(entry);
    return p ? *p : 0;
}

/* KON and KOFF wait for the next sample; writes before then add up (PCSX2 keeps the last). Writing ENDX clears it. */
void nw_spu2_set_switch(uint16_t entry, uint32_t value)
{
    NwSpu2Core *c = &spu.core[entry & 1];
    unsigned reg = nw_spu2_reg(entry);
    value &= (1u << NW_SPU2_VOICES) - 1;
    if (reg == nw_spu2_reg(NW_SD_S_KON)) {
        c->key_on |= value;
    } else if (reg == nw_spu2_reg(NW_SD_S_KOFF)) {
        c->key_off |= value;
    } else {
        c->sw[reg] = reg == nw_spu2_reg(NW_SD_S_ENDX) ? 0 : value;
    }
}

uint32_t nw_spu2_get_switch(uint16_t entry)
{
    return spu.core[entry & 1].sw[nw_spu2_reg(entry)];
}

/* Voice addresses are kept in words, block aligned; an LSAX write keeps the START flags from moving it until the
   next key-on. */
void nw_spu2_set_addr(uint16_t entry, uint32_t addr)
{
    unsigned reg = (entry >> 8) & 0x3f;
    if (!(entry & 0x40)) {
        if (reg >= 0x1c && reg < 0x20) {
            spu.core[entry & 1].addr[reg - 0x1c] = addr;
        }
        return;
    }
    NwSpu2Voice *v = nw_spu2_voice(entry);
    uint32_t words = (addr >> 1) & NW_SPU2_BLOCK_MASK;
    if (!v) {
        return;
    }
    if (reg == NW_SD_VA_SSA >> 8) {
        v->start = words;
    } else if (reg == NW_SD_VA_LSAX >> 8) {
        v->loop = words;
        v->loop_set = true;
    } else if (reg == NW_SD_VA_NAX >> 8) {
        v->next = words | 1;
    }
}

uint32_t nw_spu2_get_addr(uint16_t entry)
{
    unsigned reg = (entry >> 8) & 0x3f;
    if (!(entry & 0x40)) {
        return reg >= 0x1c && reg < 0x20 ? spu.core[entry & 1].addr[reg - 0x1c] : 0;
    }
    const NwSpu2Voice *v = nw_spu2_voice(entry);
    if (!v) {
        return 0;
    }
    if (reg == NW_SD_VA_SSA >> 8) {
        return v->start << 1;
    }
    if (reg == NW_SD_VA_LSAX >> 8) {
        return v->loop << 1;
    }
    return reg == NW_SD_VA_NAX >> 8 ? v->next << 1 : 0;
}

void nw_spu2_set_core_attr(uint16_t entry, uint16_t value)
{
    spu.core[entry & 1].attr[(entry >> 1) & 7] = value;
}

uint16_t nw_spu2_get_core_attr(uint16_t entry)
{
    return spu.core[entry & 1].attr[(entry >> 1) & 7];
}

/* The work area in words: from ESA to the end of EEA's 64K words. */
static uint32_t nw_spu2_esa(const NwSpu2Core *c)
{
    return (c->addr[0] >> 1) & NW_SPU2_WORD_MASK;
}

static uint32_t nw_spu2_eea(const NwSpu2Core *c)
{
    return ((c->addr[1] >> 17) << 16 | 0xffff) & NW_SPU2_WORD_MASK;
}

/* libsd clears a mode's area below EEA by DMA; here at once. */
static void nw_spu2_clear_work(const NwSpu2Core *c, int mode)
{
    uint32_t end = nw_spu2_eea(c) + 1, words = nw_spu2_presets.size[mode] * 4;
    if (mode != 0 && words <= end) {
        memset(nw_spu2_mem + (end - words) * 2, 0, words * 2);
    }
}

/* ECHO and DELAY move six addresses by the delay and set the wall volume from the feedback (other modes: 0). The old
   mode's area is cleared before the register writes if effects were on, the new one's after. */
void nw_spu2_set_effect_attr(int core, const NwSpu2Effect *attr)
{
    NwSpu2Core *c = &spu.core[core & 1];
    unsigned mode = (unsigned)attr->mode & ~0x100u, old = (unsigned)c->effect.mode;
    bool clear = attr->mode & 0x100;

    if (mode >= NW_SPU2_REVERB_MODES) {
        return;
    }
    NwSpu2Preset p = nw_spu2_presets.mode[mode];
    bool echo = mode == 7 || mode == 8;
    c->effect = (NwSpu2Effect){ (int)mode, attr->depth_L, attr->depth_R, echo ? attr->delay : 0,
                                echo ? attr->feedback : 0 };
    if (echo) {
        uint16_t d = (uint16_t)((attr->delay + 1) << 5);
        p.reg[10] = (uint16_t)(2 * d - p.reg[0]);
        p.reg[11] = (uint16_t)(d - p.reg[1]);
        p.reg[16] = (uint16_t)(p.reg[17] + d);
        p.reg[12] = (uint16_t)(p.reg[13] + d);
        p.reg[27] = (uint16_t)(p.reg[29] + d);
        p.reg[26] = (uint16_t)(p.reg[28] + d);
        p.reg[7] = (uint16_t)(attr->feedback * 258);
    }
    if (clear && nw_spu2_get_core_attr((uint16_t)(core | NW_SD_C_EFFECT_ENABLE))) {
        nw_spu2_clear_work(c, (int)old);
    }
    c->param[nw_spu2_reg(NW_SD_P_EVOLL)] = (uint16_t)attr->depth_L;
    c->param[nw_spu2_reg(NW_SD_P_EVOLR)] = (uint16_t)attr->depth_R;
    for (int i = 0; i < NW_SPU2_REVERB_REGS; i++) {
        if (p.mask && !(p.mask >> i & 1)) {
            continue;
        }
        if (i < 2 || (i >= 10 && i < 30)) {
            c->revb_addr[i < 2 ? i : i - 8] = (uint32_t)p.reg[i] << 2;
        } else {
            c->revb_vol[i < 10 ? i - 2 : i - 22] = (int16_t)p.reg[i];
        }
    }
    uint32_t esa = nw_spu2_eea(c) + 1 - nw_spu2_presets.size[mode] * 4;
    c->addr[0] = (esa & NW_SPU2_WORD_MASK) << 1;
    if (clear) {
        nw_spu2_clear_work(c, (int)mode);
    }
}

NwSpu2Effect nw_spu2_get_effect_attr(int core)
{
    return spu.core[core & 1].effect;
}

void nw_spu2_voice_trans(uint32_t spu_addr, const void *src, uint32_t size)
{
    if (spu_addr < NW_SPU2_RAM) {
        memcpy(nw_spu2_mem + spu_addr, src, size < NW_SPU2_RAM - spu_addr ? size : NW_SPU2_RAM - spu_addr);
    }
}

const uint8_t *nw_spu2_ram(void)
{
    return nw_spu2_mem;
}

static void nw_spu2_stop(NwSpu2Voice *v)
{
    v->level = 0;
    v->phase = NW_ADSR_STOPPED;
}

/* At the start of a sample, KOFF releases, then KON starts from SSA with the envelope at 0. */
static void nw_spu2_keys(NwSpu2Core *c)
{
    for (unsigned i = 0; i < NW_SPU2_VOICES; i++) {
        NwSpu2Voice *v = &c->voice[i];
        if ((c->key_off >> i & 1) && v->phase != NW_ADSR_STOPPED) {
            v->phase = NW_ADSR_RELEASE;
            v->counter = 0;
        }
        if (c->key_on >> i & 1) {
            v->phase = NW_ADSR_ATTACK;
            v->counter = 0;
            v->level = 0;
            v->loop_set = false;
            v->sp = 0;
            v->flags = 0;
            v->next = v->start | 1;
            v->decoded = v->played = 0;
            v->adpcm = (NwAdpcm){ 0, 0 };
            v->block_ready = false;
        }
    }
    c->sw[nw_spu2_reg(NW_SD_S_ENDX)] &= ~c->key_on;
    c->key_on = c->key_off = 0;
}

/* PCSX2 DecodeSamples: reads the block flags at NAX every sample (START latches the loop address) and stays up to 16
   samples ahead, 4 per word; the ADPCM history carries across blocks and loops. At a block's end END sets ENDX and
   jumps to the loop address; without REPEAT the voice ends, its address still moving. */
static void nw_spu2_decode(NwSpu2Core *c, unsigned i)
{
    NwSpu2Voice *v = &c->voice[i];
    uint32_t block = v->next & NW_SPU2_BLOCK_MASK;
    v->flags = nw_spu2_mem[block * 2 + 1];
    if ((v->flags & NW_ADPCM_START) && !v->loop_set) {
        v->loop = block;
    }
    if ((int32_t)(v->decoded - v->played) > 12) {
        return;
    }
    if (v->phase != NW_ADSR_STOPPED) {
        if (!v->block_ready) {
            nw_adpcm_block(&v->adpcm, nw_spu2_mem + block * 2, v->pcm);
            v->block_ready = true;
        }
        const int16_t *s = v->pcm + ((v->next & 7) - 1) * 4;
        for (unsigned k = 0; k < 4; k++) {
            v->fifo[(v->decoded + k) % NW_SPU2_FIFO] = s[k];
        }
        if (nw_spu2_io.tap) {
            nw_spu2_io.tap(nw_spu2_io.tap_user, (unsigned)(c - spu.core) * NW_SPU2_VOICES + i, s);
        }
    }
    v->decoded += 4;
    v->next = (v->next + 1) & NW_SPU2_WORD_MASK;
    if ((v->next & 7) == 0) {
        if (v->flags & NW_ADPCM_END) {
            c->sw[nw_spu2_reg(NW_SD_S_ENDX)] |= 1u << i;
            v->next = v->loop;
            if (!(v->flags & NW_ADPCM_REPEAT)) {
                nw_spu2_stop(v);
            }
        }
        v->next = (v->next + 1) & NW_SPU2_WORD_MASK; /* past the header */
        v->block_ready = false;
    }
}

/* One envelope step (PCSX2 V_ADSR::Calculate); false once the release is done or the sustain reached 0. */
static bool nw_spu2_envelope(NwSpu2Voice *v)
{
    unsigned a1 = v->param[nw_spu2_reg(NW_SD_VP_ADSR1)], a2 = v->param[nw_spu2_reg(NW_SD_VP_ADSR2)];
    bool decr = true, exp = true;
    int shift, step = -8;
    int32_t target = 0;

    switch (v->phase) {
    case NW_ADSR_ATTACK:
        decr = false;
        exp = a1 >> 15;
        shift = a1 >> 10 & 0x1f;
        step = 7 - (int)(a1 >> 8 & 3);
        target = NW_SPU2_ENV_MAX;
        break;
    case NW_ADSR_DECAY:
        shift = a1 >> 4 & 0xf;
        target = (int32_t)((a1 & 0xf) + 1) << 11;
        break;
    case NW_ADSR_SUSTAIN:
        decr = a2 >> 14 & 1;
        exp = a2 >> 15;
        shift = a2 >> 8 & 0x1f;
        step = 7 - (int)(a2 >> 6 & 3);
        if (decr) {
            step = ~step;
        }
        break;
    default:
        exp = a2 >> 5 & 1;
        shift = a2 & 0x1f;
        break;
    }
    uint32_t counter_inc = 0x8000u >> (shift > 11 ? shift - 11 : 0);
    int32_t level_inc = step * (1 << (shift < 11 ? 11 - shift : 0));
    if (exp && !decr && v->level > 0x6000) {
        counter_inc >>= 2;
    }
    if (exp && decr) {
        level_inc = (int16_t)((level_inc * v->level) >> 15);
    }
    v->counter += counter_inc ? counter_inc : 1;
    if (v->counter >= 0x8000) {
        v->counter = 0;
        v->level += level_inc;
        v->level = v->level < 0 ? 0 : v->level > NW_SPU2_ENV_MAX ? NW_SPU2_ENV_MAX : v->level;
    }
    if (v->phase == NW_ADSR_SUSTAIN) {
        return v->level != 0;
    }
    if (decr ? v->level <= target : v->level >= target) {
        v->phase++;
    }
    return v->phase <= NW_ADSR_RELEASE;
}

/* The 4 samples from the read position, weighted by the pitch counter's phase (GetVoiceValues). */
static int32_t nw_spu2_interpolate(const NwSpu2Voice *v)
{
    const int16_t *g = nw_spu2_gauss[(v->sp & 0xff0) >> 4];
    int32_t out = 0;
    for (unsigned k = 0; k < 4; k++) {
        out += (g[k] * v->fifo[(v->played + k) % NW_SPU2_FIFO]) >> 15;
    }
    return out;
}

/* A fixed volume register (bit 15 clear; the IRX's master volumes never sweep) as V_VolumeSlide::RegSet scales it. */
static int32_t nw_spu2_volume(uint16_t reg)
{
    return (int16_t)(uint16_t)(reg << 1);
}

static int32_t nw_spu2_clamp(int32_t x)
{
    return x < -0x8000 ? -0x8000 : x > 0x7fff ? 0x7fff : x;
}

/* Bit 15 set: a sweep, a step a sample (PCSX2 V_VolumeSlide::Update); bits 0-6 rate, 12 phase, 13 down, 14
   exponential. The EE sends one when surround negates a looping sound's right volume (one-shots mask it to 15 bits). */
static void nw_spu2_sweep(uint16_t reg, int32_t *value, uint32_t *counter)
{
    if (!(reg & 0x8000)) {
        return;
    }
    int rate = reg & 3, shift = reg >> 2 & 0x1f, step = 7 - rate;
    bool phase = reg >> 12 & 1, down = reg >> 13 & 1, exp = reg >> 14 & 1;
    if (down) {
        step = ~step;
    }
    uint32_t counter_inc = 0x8000u >> (shift > 11 ? shift - 11 : 0);
    int32_t level_inc = step * (1 << (shift < 11 ? 11 - shift : 0));
    if (exp && !down && *value > 0x6000) {
        counter_inc >>= 2;
    }
    if (exp && down) {
        level_inc = (int16_t)((level_inc * *value) >> 15);
    }
    if (rate != 3 && shift != 0x1f && counter_inc == 0) {
        counter_inc = 1;
    }
    *counter += counter_inc;
    if (!(exp && down) && phase) {
        level_inc = -level_inc;
    }
    if (*counter >= 0x8000) {
        bool negative = down && phase && !exp;
        int32_t lo = !down || negative ? -0x8000 : 0, hi = negative ? 0 : 0x7fff;
        *counter = 0;
        *value = SDL_clamp(*value + level_inc, lo, hi);
    }
}

/* One sample of every voice (PCSX2 MixVoice); dry and wet get each core's voices by their VMIX gates. */
static void nw_spu2_voices(int32_t dry[2][2], int32_t wet[2][2], bool mix)
{
    for (int c = 0; c < 2; c++) {
        NwSpu2Core *core = &spu.core[c];
        const uint32_t *sw = core->sw;
        for (unsigned i = 0; i < NW_SPU2_VOICES; i++) {
            NwSpu2Voice *v = &core->voice[i];
            for (int s = 0; s < 2; s++) {
                nw_spu2_sweep(v->param[s], &v->vol[s], &v->vol_counter[s]);
            }
            nw_spu2_decode(core, i);
            if (v->phase != NW_ADSR_STOPPED) {
                int32_t value = mix ? nw_spu2_interpolate(v) : 0;
                if (!nw_spu2_envelope(v)) {
                    nw_spu2_stop(v);
                }
                value = (value * v->level) >> 15;
                int cut = SDL_GetAtomicInt(&nw_spu2_cut[c * NW_SPU2_VOICES + i]);
                if (cut) {
                    value = (value * (NW_SPU2_UNITY - cut)) >> 15;
                }
                int32_t l = (value * v->vol[0]) >> 15, r = (value * v->vol[1]) >> 15;
                dry[c][0] += sw[nw_spu2_reg(NW_SD_S_VMIXL)] >> i & 1 ? l : 0;
                dry[c][1] += sw[nw_spu2_reg(NW_SD_S_VMIXR)] >> i & 1 ? r : 0;
                wet[c][0] += sw[nw_spu2_reg(NW_SD_S_VMIXEL)] >> i & 1 ? l : 0;
                wet[c][1] += sw[nw_spu2_reg(NW_SD_S_VMIXER)] >> i & 1 ? r : 0;
            }
            uint32_t pitch = v->param[nw_spu2_reg(NW_SD_VP_PITCH)];
            v->sp += pitch < 0x3fff ? pitch : 0x3fff;
            v->played += v->sp >> 12;
            v->sp &= 0xfff;
        }
    }
}

static int16_t nw_spu2_word(uint32_t w)
{
    return (int16_t)(nw_spu2_mem[w * 2] | nw_spu2_mem[w * 2 + 1] << 8);
}

static void nw_spu2_put_word(uint32_t w, int32_t x)
{
    x = nw_spu2_clamp(x);
    nw_spu2_mem[w * 2] = (uint8_t)x;
    nw_spu2_mem[w * 2 + 1] = (uint8_t)(x >> 8);
}

/* PCSX2's MUL, its 32-bit product wrapping as there. */
static int32_t nw_spu2_mul(int32_t x, int32_t y)
{
    return (int32_t)((uint32_t)x * (uint32_t)y) >> 15;
}

/* A reverb address: offset words past the area's start, which moves a word every other sample (RevbGetIndexer). */
static uint32_t nw_spu2_revb_at(const NwSpu2Core *c, uint32_t offset)
{
    uint32_t start = nw_spu2_esa(c), end = nw_spu2_eea(c);
    return (start + ((nw_spu2_clock.ticks >> 1) + offset) % (end - start + 1)) & NW_SPU2_WORD_MASK;
}

static int32_t nw_spu2_filter(const int16_t *ring, unsigned pos, int scale)
{
    const int16_t *x = ring + ((pos - NW_SPU2_FIR_TAPS) & (NW_SPU2_FIR_RING - 1));
    int32_t out = 0;
    for (unsigned i = 0; i < NW_SPU2_FIR_TAPS; i++) {
        int32_t k = nw_spu2_fir[i] * scale;
        out += x[i] * (k > 0x7fff ? 0x7fff : k);
    }
    return nw_spu2_clamp(out >> 15);
}

/* One sample of a core's reverb (PCSX2 DoReverb): the input filtered down to 24 kHz, its left and right on
   alternate samples through the work area, written back only with effects on, the output filtered back up. */
static void nw_spu2_reverb(NwSpu2Core *c, const int32_t in[2], int32_t out[2])
{
    out[0] = out[1] = 0;
    if (nw_spu2_esa(c) >= nw_spu2_eea(c)) {
        return;
    }
    unsigned pos = c->revb_pos;
    for (int s = 0; s < 2; s++) {
        c->revb_down[s][pos] = c->revb_down[s][pos | NW_SPU2_FIR_RING] = (int16_t)nw_spu2_clamp(in[s]);
    }

    const uint32_t *a = c->revb_addr;
    const int16_t *vol = c->revb_vol;
    int r = nw_spu2_clock.ticks & 1;
    uint32_t same_src = nw_spu2_revb_at(c, a[NW_REVB_SAME_L_SRC + r]);
    uint32_t same_dst = nw_spu2_revb_at(c, a[NW_REVB_SAME_L_DST + r]);
    uint32_t same_prv = nw_spu2_revb_at(c, a[NW_REVB_SAME_L_DST + r] - 1);
    uint32_t diff_src = nw_spu2_revb_at(c, a[NW_REVB_DIFF_R_SRC - r]); /* the other side's */
    uint32_t diff_dst = nw_spu2_revb_at(c, a[NW_REVB_DIFF_L_DST + r]);
    uint32_t diff_prv = nw_spu2_revb_at(c, a[NW_REVB_DIFF_L_DST + r] - 1);
    uint32_t apf1_dst = nw_spu2_revb_at(c, a[NW_REVB_APF1_L_DST + r]);
    uint32_t apf1_src = nw_spu2_revb_at(c, a[NW_REVB_APF1_L_DST + r] - a[NW_REVB_APF1_SIZE]);
    uint32_t apf2_dst = nw_spu2_revb_at(c, a[NW_REVB_APF2_L_DST + r]);
    uint32_t apf2_src = nw_spu2_revb_at(c, a[NW_REVB_APF2_L_DST + r] - a[NW_REVB_APF2_SIZE]);

    int32_t x = nw_spu2_mul(vol[NW_REVB_IN_COEF_L + r], nw_spu2_filter(c->revb_down[r], pos, 1));
    int32_t same = nw_spu2_mul(vol[NW_REVB_IIR_VOL], x + nw_spu2_mul(vol[NW_REVB_WALL_VOL], nw_spu2_word(same_src)) -
                                                         nw_spu2_word(same_prv)) +
                   nw_spu2_word(same_prv);
    int32_t diff = nw_spu2_mul(vol[NW_REVB_IIR_VOL], x + nw_spu2_mul(vol[NW_REVB_WALL_VOL], nw_spu2_word(diff_src)) -
                                                         nw_spu2_word(diff_prv)) +
                   nw_spu2_word(diff_prv);
    int32_t y = nw_spu2_mul(vol[NW_REVB_COMB1_VOL], nw_spu2_word(nw_spu2_revb_at(c, a[NW_REVB_COMB1_L_SRC + r]))) +
                nw_spu2_mul(vol[NW_REVB_COMB2_VOL], nw_spu2_word(nw_spu2_revb_at(c, a[NW_REVB_COMB2_L_SRC + r]))) +
                nw_spu2_mul(vol[NW_REVB_COMB3_VOL], nw_spu2_word(nw_spu2_revb_at(c, a[NW_REVB_COMB3_L_SRC + r]))) +
                nw_spu2_mul(vol[NW_REVB_COMB4_VOL], nw_spu2_word(nw_spu2_revb_at(c, a[NW_REVB_COMB4_L_SRC + r])));
    int32_t apf1 = y - nw_spu2_mul(vol[NW_REVB_APF1_VOL], nw_spu2_word(apf1_src));
    y = nw_spu2_word(apf1_src) + nw_spu2_mul(vol[NW_REVB_APF1_VOL], apf1);
    int32_t apf2 = y - nw_spu2_mul(vol[NW_REVB_APF2_VOL], nw_spu2_word(apf2_src));
    y = nw_spu2_word(apf2_src) + nw_spu2_mul(vol[NW_REVB_APF2_VOL], apf2);
    if (c->attr[NW_SD_C_EFFECT_ENABLE >> 1]) {
        nw_spu2_put_word(same_dst, same);
        nw_spu2_put_word(diff_dst, diff);
        nw_spu2_put_word(apf1_dst, apf1);
        nw_spu2_put_word(apf2_dst, apf2);
    }

    c->revb_up[r][pos] = c->revb_up[r][pos | NW_SPU2_FIR_RING] = (int16_t)nw_spu2_clamp(y);
    c->revb_up[!r][pos] = c->revb_up[!r][pos | NW_SPU2_FIR_RING] = 0;
    c->revb_pos = pos = (pos + 1) & (NW_SPU2_FIR_RING - 1);
    out[0] = nw_spu2_filter(c->revb_up[0], pos, 2);
    out[1] = nw_spu2_filter(c->revb_up[1], pos, 2);
}

/* PCSX2 spu2Mix without input and noise: core 0's output goes through its MVOL and core 1's AVOL into core 1, core
   1's through its MVOL out. */
static void nw_spu2_mix(int32_t dry[2][2], int32_t wet[2][2], int16_t out[2])
{
    int32_t ext[2] = { 0, 0 };
    for (int c = 0; c < 2; c++) {
        const uint16_t *p = spu.core[c].param;
        unsigned mmix = p[nw_spu2_reg(NW_SD_P_MMIX)] & (c ? 0xfff : 0xff0);
        int32_t td[2], tw[2], rv[2];
        for (int s = 0; s < 2; s++) {
            td[s] = (mmix >> (11 - s) & 1 ? nw_spu2_clamp(dry[c][s]) : 0) + (mmix >> (3 - s) & 1 ? ext[s] : 0);
            tw[s] = (mmix >> (9 - s) & 1 ? nw_spu2_clamp(wet[c][s]) : 0) + (mmix >> (1 - s) & 1 ? ext[s] : 0);
        }
        nw_spu2_reverb(&spu.core[c], tw, rv);
        for (int s = 0; s < 2; s++) {
            int32_t fx = (rv[s] * (int16_t)p[nw_spu2_reg(s ? NW_SD_P_EVOLR : NW_SD_P_EVOLL)]) >> 15;
            int32_t master = nw_spu2_volume(p[nw_spu2_reg(s ? NW_SD_P_MVOLR : NW_SD_P_MVOLL)]);
            int32_t v = (nw_spu2_clamp(td[s] + fx) * master) >> 15;
            if (c == 0) {
                ext[s] = (v * (int16_t)spu.core[1].param[nw_spu2_reg(s ? NW_SD_P_AVOLR : NW_SD_P_AVOLL)]) >> 15;
            } else {
                out[s] = (int16_t)nw_spu2_clamp(v);
            }
        }
    }
}

static void nw_spu2_flush(void)
{
    if (nw_spu2_io.count) {
        nw_spu2_io.out(nw_spu2_io.out_user, nw_spu2_io.frames, nw_spu2_io.count);
        nw_spu2_io.count = 0;
    }
}

static void nw_spu2_sample(void)
{
    int32_t dry[2][2] = { { 0, 0 }, { 0, 0 } }, wet[2][2] = { { 0, 0 }, { 0, 0 } };
    bool mix = nw_spu2_io.out != NULL;

    for (int c = 0; c < 2; c++) {
        nw_spu2_keys(&spu.core[c]);
    }
    nw_spu2_voices(dry, wet, mix);
    if (mix) {
        nw_spu2_mix(dry, wet, nw_spu2_io.frames + nw_spu2_io.count * 2);
        if (++nw_spu2_io.count == NW_SPU2_BATCH) {
            nw_spu2_flush();
        }
    }
    nw_spu2_clock.ticks++;
}

void nw_spu2_run_to(uint64_t cycle)
{
    while (cycle >= nw_spu2_clock.sample + NW_SPU2_TICK) {
        nw_spu2_clock.sample += NW_SPU2_TICK;
        nw_spu2_sample();
    }
    if (nw_spu2_io.out) {
        nw_spu2_flush();
    }
    if (cycle > nw_spu2_clock.cycle) {
        nw_spu2_clock.cycle = cycle;
    }
}

uint64_t nw_spu2_cycle(void)
{
    return nw_spu2_clock.cycle;
}
