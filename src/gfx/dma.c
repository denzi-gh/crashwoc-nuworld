/* nups2/ps2dma, nups2/asmdma and nups2/vu1man, which the port replaces (port/units.toml): the packet writers the
   boot reaches. They write retail's DMA tags and VIF codes into the caller's buffer (NuLightInit keeps its
   packets to send again); nothing sends them yet (M3). */
#include <stdint.h>

static uint32_t *nw_direct_curr; /* vt_direct_curr: the open VIF DIRECT code */
static uint32_t *nw_dmatag_curr; /* vpdmatag_curr: the open DMA tag */

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

/* A DMA ret tag (id 6) carrying two VIF codes; its QWC is left for vpDmaTag_Close. */
void *vpDmaTag_RetEx(void *packet, uint32_t vif0, uint32_t vif1)
{
    uint32_t *p = packet;
    nw_dmatag_curr = p;
    p[0] = 0x60000000u;
    p[1] = 0;
    p[2] = vif0;
    p[3] = vif1;
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
