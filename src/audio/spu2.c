#include "audio/spu2.h"

#include <stddef.h>
#include <string.h>

#define NW_SPU2_ENV_MAX 0x7fff

typedef struct NwSpu2Voice {
    uint16_t param[16]; /* by register (entry >> 8) */
    uint32_t addr[3];   /* SSA, LSAX, NAX */
} NwSpu2Voice;

typedef struct NwSpu2Core {
    NwSpu2Voice voice[NW_SPU2_VOICES];
    uint16_t param[16]; /* core registers by register */
    uint32_t sw[32];    /* switches by register */
    uint16_t attr[8];   /* core attributes by (entry >> 1) */
} NwSpu2Core;

static struct {
    NwSpu2Core core[2];
} spu;

static uint8_t nw_spu2_mem[NW_SPU2_RAM];

static unsigned nw_spu2_reg(uint16_t entry)
{
    return (entry >> 8) & 0x1f;
}

void nw_spu2_init(void)
{
    memset(&spu, 0, sizeof(spu));
}

static uint16_t *nw_spu2_param(uint16_t entry)
{
    NwSpu2Core *c = &spu.core[entry & 1];
    unsigned voice = (entry >> 1) & 0x1f;
    if (entry & 0x80) {
        return &c->param[nw_spu2_reg(entry) & 0xf];
    }
    return voice < NW_SPU2_VOICES ? &c->voice[voice].param[nw_spu2_reg(entry) & 0xf] : NULL;
}

void nw_spu2_set_param(uint16_t entry, uint16_t value)
{
    uint16_t *p = nw_spu2_param(entry);
    if (p) {
        *p = value;
    }
}

uint16_t nw_spu2_get_param(uint16_t entry)
{
    const uint16_t *p = nw_spu2_param(entry);
    return p ? *p : 0;
}

/* KON starts the envelope and clears ENDX, KOFF ends it at once. */
void nw_spu2_set_switch(uint16_t entry, uint32_t value)
{
    NwSpu2Core *c = &spu.core[entry & 1];
    unsigned reg = nw_spu2_reg(entry);
    for (unsigned v = 0; v < NW_SPU2_VOICES; v++) {
        if (!(value & 1u << v)) {
            continue;
        }
        if (reg == nw_spu2_reg(NW_SD_S_KON)) {
            c->voice[v].param[nw_spu2_reg(NW_SD_VP_ENVX)] = NW_SPU2_ENV_MAX;
            c->voice[v].addr[2] = c->voice[v].addr[0];
            c->sw[nw_spu2_reg(NW_SD_S_ENDX)] &= ~(1u << v);
        } else if (reg == nw_spu2_reg(NW_SD_S_KOFF)) {
            c->voice[v].param[nw_spu2_reg(NW_SD_VP_ENVX)] = 0;
        }
    }
    if (reg != nw_spu2_reg(NW_SD_S_KON) && reg != nw_spu2_reg(NW_SD_S_KOFF)) {
        c->sw[reg] = value & ((1u << NW_SPU2_VOICES) - 1);
    }
}

uint32_t nw_spu2_get_switch(uint16_t entry)
{
    return spu.core[entry & 1].sw[nw_spu2_reg(entry)];
}

uint32_t nw_spu2_get_addr(uint16_t entry)
{
    unsigned voice = (entry >> 1) & 0x1f, reg = nw_spu2_reg(entry) - nw_spu2_reg(NW_SD_VA_SSA);
    return voice < NW_SPU2_VOICES && reg < 3 ? spu.core[entry & 1].voice[voice].addr[reg] : 0;
}

void nw_spu2_set_core_attr(uint16_t entry, uint16_t value)
{
    spu.core[entry & 1].attr[(entry >> 1) & 7] = value;
}

uint16_t nw_spu2_get_core_attr(uint16_t entry)
{
    return spu.core[entry & 1].attr[(entry >> 1) & 7];
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
