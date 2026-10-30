/* The packet writers of nups2/ps2dma, asmdma and vu1man that the boot reaches (port/units.toml), unsent until M3, and
   the scratchpad allocator. */
#include <stdint.h>
#include <string.h>

#include "nucore/nuerror.h"

#define ps2dma_srcfile "..\\nu2crash.ps2\\nups2\\ps2dma.c"
#define NW_SCRATCH_SIZE 0x10000 /* the PS2's is 16 KB; the host's terrain scan blocks are twice retail's */

static uint32_t *nw_direct_curr; /* vt_direct_curr: the open VIF DIRECT code */
static uint32_t *nw_dmatag_curr; /* vpdmatag_curr: the open DMA tag */

static void *nw_dmatag(void *packet, uint32_t id, uint32_t addr, uint32_t vif0, uint32_t vif1)
{
    uint32_t *p = packet;
    nw_dmatag_curr = p;
    p[0] = id << 28;
    p[1] = addr;
    p[2] = vif0;
    p[3] = vif1;
    return p + 4;
}

/* VIF DIRECT, its quadword count left for vtCloseDirect; irq sets the interrupt bit. */
void *vtBeginDirect(void *packet, uint32_t irq)
{
    uint32_t *p = packet;
    nw_direct_curr = p;
    *p = (irq << 31) | 0x50000000u;
    return p + 1;
}

/* The count spans from the DIRECT code to the word before `packet`. */
void *vtCloseDirect(void *packet)
{
    uint32_t *last = (uint32_t *)packet - 1;
    *nw_direct_curr |= (uint32_t)(((uintptr_t)last - (uintptr_t)nw_direct_curr) >> 4) & 0xFFFF;
    return packet;
}

void *vpDmaTag_Cnt(void *packet)
{
    return nw_dmatag(packet, 1, 0, 0, 0);
}

void *vpDmaTag_CntEx(void *packet, uint32_t vif0, uint32_t vif1)
{
    return nw_dmatag(packet, 1, 0, vif0, vif1);
}

void *vpDmaTag_Next(void *packet, uint32_t addr)
{
    return nw_dmatag(packet, 2, addr, 0, 0);
}

void *vpDmaTag_RefEx(void *packet, uint32_t addr, uint32_t vif0, uint32_t vif1)
{
    return nw_dmatag(packet, 3, addr, vif0, vif1);
}

void *vpDmaTag_Call(void *packet, uint32_t addr)
{
    return nw_dmatag(packet, 5, addr, 0, 0);
}

void *vpDmaTag_CallEx(void *packet, uint32_t addr, uint32_t vif0, uint32_t vif1)
{
    return nw_dmatag(packet, 5, addr, vif0, vif1);
}

void *vpDmaTag_Ret(void *packet)
{
    return nw_dmatag(packet, 6, 0, 0, 0);
}

void *vpDmaTag_RetEx(void *packet, uint32_t vif0, uint32_t vif1)
{
    return nw_dmatag(packet, 6, 0, vif0, vif1);
}

void *vpDmaTag_Copy(void *packet, void *tag)
{
    memmove(packet, tag, 16);
    return (uint8_t *)packet + 16;
}

void *vpDmaTag_RepointCopy(void *packet, void *tag, uint32_t addr)
{
    uint32_t *p = packet;
    memmove(p, tag, 16);
    p[1] = addr;
    return p + 4;
}

/* Pads to a quadword with zero words and writes the quadwords after the open tag into its QWC. */
void *vpDmaTag_Close(void *packet)
{
    uint32_t *p = packet;
    while ((uintptr_t)p & 0xF) {
        *p++ = 0;
    }
    *(uint16_t *)nw_dmatag_curr = (uint16_t)((((uintptr_t)p - (uintptr_t)nw_dmatag_curr) >> 4) - 1);
    return p;
}

/* Resets VU1 and uploads its microcode: the renderer's (M3). */
void NuPs2InitVU(void)
{
}

static _Alignas(16) unsigned char nw_scratch[NW_SCRATCH_SIZE];
static unsigned char *nw_scratch_ptr = nw_scratch;

/* A stack, as retail's on the scratchpad: each block is followed by the cursor before it, which NuScratchRelease
   returns to. */
void *NuScratchAlloc32(uint32_t size)
{
    unsigned char *previous = nw_scratch_ptr;
    unsigned char *block = (unsigned char *)(((uintptr_t)previous + 3) & ~(uintptr_t)3);
    unsigned char *link = (unsigned char *)(((uintptr_t)block + ((size + 3) & ~3u) + 7) & ~(uintptr_t)7);
    if (link > nw_scratch + NW_SCRATCH_SIZE - sizeof(void *)) {
        NuErrorProlog(ps2dma_srcfile, 0)("NuScratchAlloc32: %u bytes overflow the scratchpad", size);
    }
    memcpy(link, &previous, sizeof(previous));
    nw_scratch_ptr = link + sizeof(void *);
    return block;
}

void NuScratchRelease(void)
{
    if (nw_scratch_ptr == nw_scratch) {
        NuErrorProlog(ps2dma_srcfile, 0)("NuScratchRelease: no block to release");
    }
    memcpy(&nw_scratch_ptr, nw_scratch_ptr - sizeof(void *), sizeof(void *));
}
