/* nups2/ps2dma, nups2/asmdma and nups2/vu1man, which the port replaces (port/units.toml): the packet writers
   retail reaches. They write retail's DMA tags and VIF codes into the caller's buffer (NuLightInit keeps its
   packets to send again); nothing sends them yet (M3). A tag's ADDR is 32 bits and keeps the low half of a host
   address. */
#include <stdint.h>
#include <string.h>

static uint32_t *nw_direct_curr; /* vt_direct_curr: the open VIF DIRECT code */
static uint32_t *nw_dmatag_curr; /* vpdmatag_curr: the open DMA tag */

/* Opens a DMA tag: id in bits 28-30 (QWC 0, left for vpDmaTag_Close), ADDR, then two VIF codes. */
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

void *vpDmaTag_CntEx(void *packet, uint32_t vif0, uint32_t vif1)
{
    return nw_dmatag(packet, 1, 0, vif0, vif1);
}

void *vpDmaTag_Next(void *packet, uint32_t addr)
{
    return nw_dmatag(packet, 2, addr, 0, 0);
}

/* Callers pass the first VIF code as "qwc"; it is stored as a VIF code all the same. */
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

/* Copies a whole tag; the open tag stays as it was. */
void *vpDmaTag_Copy(void *packet, void *tag)
{
    memmove(packet, tag, 16);
    return (uint8_t *)packet + 16;
}

/* Copies a tag with a new ADDR. */
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
