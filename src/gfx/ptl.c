/* nups2/ptl, which the port replaces (port/units.toml): the particle sets and effect lists SetupDebris carves out of
   debbuffer, in retail's layout, as the debris code writes into them. A set's link to the next is an offset from it
   on the host; NuRndrParticleGroup (src/gfx/nurndr.c) follows it. */
#include <stdint.h>

#include "gamelib/debris.h"
#include "nups2/nups2types.h"

extern float NuTrigTable[];
void *vpDmaTag_RetEx(void *packet, uint32_t vif0, uint32_t vif1);
void *vpDmaTag_Close(void *packet);

/* The header to VU 0x3C, then the UV corners and a test pattern of 64 entries to 0x3D, then MSCAL 0x302. */
struct debdma_s *CreateDmaPartEffectList(struct debdma_s *list, int *size)
{
    uint8_t *base = (uint8_t *)list;
    vpDmaTag_RetEx(base, 0x11000000, 0x6C01003C);
    vpDmaTag_Close(base + 0x20);
    vpDmaTag_RetEx(base + 0x20, 0x11000000, 0x6C83003D);
    list->grav = 0.0f;
    list->u0 = 0x38;
    list->v0 = 0x48;
    list->u1 = 0xC8;
    list->v1 = 0x48;
    list->u2 = 0x38;
    list->v2 = 0xB8;
    list->u3 = 0xC8;
    list->v3 = 0xB8;
    for (int i = 0; i < 64; i++) {
        struct debtri_s *t = &list->tri[i];
        f32 a = (f32)(0x200 + i * 8), b = (f32)(-0x200 - i * 8);
        f32 c = NuTrigTable[(u16)(i * 0x200 + 0x4000)], s = NuTrigTable[(u16)(i * 0x200)];
        t->v0x.coord = (b * c - a * s) * 640.0f * 0.00390625f;
        t->v0y.coord = b * s + a * c;
        t->v2x = (a * c - a * s) * 640.0f * 0.00390625f;
        t->v2y = a * s + a * c;
        t->v1x.coord = (f32)(0x400 + i * 16) * s * 640.0f * 0.00390625f;
        t->v1y.coord = (f32)(-0x400 - i * 16) * c;
        t->v0x.colour = (u8)(-0x80 - i);
        t->v0y.colour = (u8)(0x50 - i);
        t->v1x.colour = 0;
        t->v1y.colour = i <= 0x30 ? 0x10 : (u8)(0x40 - i);
    }
    uint32_t *p = (uint32_t *)&list->tri[64];
    *p++ = 0x14000302;
    *size = (int)((uint8_t *)vpDmaTag_Close(p) - base);
    return list;
}

/* The unpack of 0x41 qwords to VU 0: the GIF tag of 32 strips, then 32 dead particles; then MSCNT. */
struct DmaTag *CreateDmaParticleSet(struct DmaTag *set, int *size)
{
    uint32_t *p = vpDmaTag_RetEx(set, 0, 0x6C418000);
    *p++ = 0x00008020;
    *p++ = 0x90024000;
    *p++ = 0x35353531;
    *p++ = 5;
    struct debpart_s *part = (struct debpart_s *)p;
    for (int i = 0; i < 32; i++, part++) {
        part->pos.x = part->pos.y = part->pos.z = 0.0f;
        part->mom.x = part->mom.y = part->mom.z = 0.0f;
        part->time = -1.0f;
        part->rate = 128.0f;
    }
    p = (uint32_t *)part;
    *p++ = 0x17000000;
    *size = (int)((uint8_t *)vpDmaTag_Close(p) - (uint8_t *)set);
    return set;
}

/* Each set's tag a NEXT to the next set, the last's a RET; the address is the next set's offset from this one, all
   of them being in debbuffer. Retail writes before sets[0] for no sets. */
void LinkDmaParticalSets(struct DmaTag **sets, int count)
{
    if (count <= 0) {
        return;
    }
    struct DmaTag *tag = sets[count - 1];
    tag->ctrl = (tag->ctrl & 0x8FFFFFFF) + 0x60000000;
    tag->addr = 0;
    for (int i = count - 2; i >= 0; i--) {
        tag = sets[i];
        tag->ctrl = (tag->ctrl & 0x8FFFFFFF) + 0x20000000;
        tag->addr = (u32)(s32)((uint8_t *)sets[i + 1] - (uint8_t *)sets[i]);
    }
}
