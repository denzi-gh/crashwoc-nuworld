#include "audio/spu2.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "audio/vag.h"

#define NW_SPU2_ENV_MAX 0x7fff
#define NW_SPU2_TICK 768         /* IOP cycles a sample */
#define NW_SPU2_WORD_MASK 0xfffffu /* sound RAM in 16-bit words */
#define NW_SPU2_BLOCK_MASK 0xffff8u
#define NW_SPU2_ALL_VOICES 0xffffffu
#define NW_SPU2_FIFO 32
#define NW_SPU2_BATCH 256 /* frames handed to the output at once */

static const int16_t nw_spu2_gauss[256][4] = {
#include "audio/spu2_gauss.inc"
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
} NwSpu2Core;

static struct {
    NwSpu2Core core[2];
} spu;

/* How far the SPU ran and the cycle of its last sample. */
static struct {
    uint64_t cycle;
    uint64_t sample;
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

void nw_spu2_set_output(NwSpu2Output out, void *user)
{
    nw_spu2_io.out = out;
    nw_spu2_io.out_user = user;
    nw_spu2_io.count = 0;
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

void nw_spu2_set_param(uint16_t entry, uint16_t value)
{
    uint16_t *p = nw_spu2_param(entry);
    if (p) {
        *p = value;
    }
}

/* ENVX reads the envelope. */
uint16_t nw_spu2_get_param(uint16_t entry)
{
    if (!(entry & 0x80) && nw_spu2_reg(entry) == nw_spu2_reg(NW_SD_VP_ENVX)) {
        const NwSpu2Voice *v = nw_spu2_voice(entry);
        return v ? (uint16_t)v->level : 0;
    }
    const uint16_t *p = nw_spu2_param(entry);
    return p ? *p : 0;
}

/* KON and KOFF wait for the next sample. PCSX2 stores them, so there a second write before it replaces the first;
   here they add up. Writing ENDX clears it. */
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

void nw_spu2_set_effect_attr(int core, const NwSpu2Effect *attr)
{
    spu.core[core & 1].effect = *attr;
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

/* The decoder (PCSX2 DecodeSamples): it reads the flags of the block at NAX every sample (START latches the loop
   address) and stays up to 16 samples ahead, a word of 4 at a time. A playing voice decodes the block when it reads
   its first word, the ADPCM history going on across blocks and loops (GetNextDataBuffered). At the end of a block
   END sets ENDX and jumps to the loop address; without REPEAT it ends the voice, whose address goes on moving. */
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

/* A fixed volume register (bit 15 clear; the IRX never sweeps) as V_VolumeSlide::RegSet scales it. */
static int32_t nw_spu2_volume(uint16_t reg)
{
    return (int16_t)(uint16_t)(reg << 1);
}

static int32_t nw_spu2_clamp(int32_t x)
{
    return x < -0x8000 ? -0x8000 : x > 0x7fff ? 0x7fff : x;
}

/* One sample of every voice (PCSX2 MixVoice): decode, the interpolated sample, the envelope, the volumes, then the
   pitch moves it on. dry gets each core's dry voices. */
static void nw_spu2_voices(int32_t dry[2][2], bool mix)
{
    for (int c = 0; c < 2; c++) {
        NwSpu2Core *core = &spu.core[c];
        for (unsigned i = 0; i < NW_SPU2_VOICES; i++) {
            NwSpu2Voice *v = &core->voice[i];
            nw_spu2_decode(core, i);
            if (v->phase != NW_ADSR_STOPPED) {
                int32_t value = mix ? nw_spu2_interpolate(v) : 0;
                if (!nw_spu2_envelope(v)) {
                    nw_spu2_stop(v);
                }
                if (core->sw[nw_spu2_reg(NW_SD_S_VMIXL)] >> i & 1) {
                    dry[c][0] += (value * nw_spu2_volume(v->param[nw_spu2_reg(NW_SD_VP_VOLL)])) >> 15;
                }
                if (core->sw[nw_spu2_reg(NW_SD_S_VMIXR)] >> i & 1) {
                    dry[c][1] += (value * nw_spu2_volume(v->param[nw_spu2_reg(NW_SD_VP_VOLR)])) >> 15;
                }
            }
            uint32_t pitch = v->param[nw_spu2_reg(NW_SD_VP_PITCH)];
            v->sp += pitch < 0x3fff ? pitch : 0x3fff;
            v->played += v->sp >> 12;
            v->sp &= 0xfff;
        }
    }
}

/* spu2Mix without the input, reverb and noise: the clamped dry voices go through each core's MMIX gates, core 0's
   output through its MVOL and core 1's AVOL into core 1, core 1's through its MVOL out. */
static void nw_spu2_mix(int32_t dry[2][2], int16_t out[2])
{
    int32_t ext[2] = { 0, 0 };
    for (int c = 0; c < 2; c++) {
        const uint16_t *p = spu.core[c].param;
        unsigned mmix = p[nw_spu2_reg(NW_SD_P_MMIX)] & (c ? 0xfff : 0xff0);
        for (int s = 0; s < 2; s++) {
            int32_t td = mmix >> (11 - s) & 1 ? nw_spu2_clamp(dry[c][s]) : 0;
            if (mmix >> (3 - s) & 1) {
                td += ext[s];
            }
            int32_t master = nw_spu2_volume(p[nw_spu2_reg(s ? NW_SD_P_MVOLR : NW_SD_P_MVOLL)]);
            int32_t v = (nw_spu2_clamp(td) * master) >> 15;
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
    int32_t dry[2][2] = { { 0, 0 }, { 0, 0 } };
    bool mix = nw_spu2_io.out != NULL;

    for (int c = 0; c < 2; c++) {
        nw_spu2_keys(&spu.core[c]);
    }
    nw_spu2_voices(dry, mix);
    if (mix) {
        nw_spu2_mix(dry, nw_spu2_io.frames + nw_spu2_io.count * 2);
        if (++nw_spu2_io.count == NW_SPU2_BATCH) {
            nw_spu2_flush();
        }
    }
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
