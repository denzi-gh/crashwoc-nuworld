#include "audio/spu2.h"

#include <string.h>

#define NW_SPU2_ENV_MAX 0x7fff

typedef struct NwSpu2Voice {
    uint16_t param[16]; /* by register (entry >> 8) */
} NwSpu2Voice;

typedef struct NwSpu2Core {
    NwSpu2Voice voice[NW_SPU2_VOICES];
    uint32_t sw[32]; /* switches by register */
} NwSpu2Core;

static struct {
    NwSpu2Core core[2];
} spu;

static unsigned nw_spu2_reg(uint16_t entry)
{
    return (entry >> 8) & 0x1f;
}

void nw_spu2_init(void)
{
    memset(&spu, 0, sizeof(spu));
}

void nw_spu2_set_param(uint16_t entry, uint16_t value)
{
    unsigned voice = (entry >> 1) & 0x1f;
    if (voice < NW_SPU2_VOICES) {
        spu.core[entry & 1].voice[voice].param[nw_spu2_reg(entry) & 0xf] = value;
    }
}

uint16_t nw_spu2_get_param(uint16_t entry)
{
    unsigned voice = (entry >> 1) & 0x1f;
    return voice < NW_SPU2_VOICES ? spu.core[entry & 1].voice[voice].param[nw_spu2_reg(entry) & 0xf] : 0;
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
