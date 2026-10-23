/* nu3d/nutex's NuTexCreateNative (port/units.toml override): retail's TexList slot over the record, which stays in
   place, and the record decoded for the renderer. */
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/rndr.h"
#include "nu3d/nutex.h"
#include "nucore/nuerror.h"

#define nutex_srcfile "..\\nu2crash.ps2\\nu3d\\nutex.c"
#define NUTEX_VALID 0x10000

extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC
extern int D_0062EBF0;
#define nutex_cnt D_0062EBF0
extern int D_0063305C;
#define nutex_max D_0063305C
extern int nutex_alloc_cnt;

static int NuTexFindFreeSlot(void)
{
    int start = nutex_cnt;
    while (TexList[nutex_cnt].flags & NUTEX_VALID) {
        nutex_cnt++;
        if (start == nutex_cnt) {
            return -1;
        }
        if (nutex_cnt >= nutex_max) {
            nutex_cnt = 0;
        }
    }
    nutex_alloc_cnt++;
    return nutex_cnt;
}

static int nw_rd32(const uint8_t *p)
{
    int v;
    memcpy(&v, p, 4);
    return v;
}

static int nw_rd16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static int nw_log2(int size)
{
    int shift = 0;
    for (; size >= 2; size >>= 1) {
        shift++;
    }
    return shift;
}

/* Header version 0 (old) or 1 (cur), the palette chunk of types 4-7, then a chunk per mip level; an unknown version
   takes a slot it never marks valid, as retail. */
int NuTexCreateNative(void *data)
{
    const uint8_t *hdr = data;
    int slot = NuTexFindFreeSlot();
    int version = nw_rd16(hdr + 2);
    if (slot < 0 || version > 1) {
        return 0;
    }
    struct nutexslot_s *entry = &TexList[slot];
    struct nups2tex_s *ps2tex = &entry->ps2tex;
    entry->valid = 1;
    entry->native = 1;
    entry->type = hdr[12];
    entry->width = version ? nw_rd16(hdr) : nw_rd32(hdr);
    entry->height = version ? nw_rd16(hdr + 4) : nw_rd32(hdr + 4);
    entry->bits = 0;
    entry->pal = 0;
    memset(ps2tex, 0, sizeof(*ps2tex));
    ps2tex->mipcount = version ? hdr[15] & 0x7F : hdr[14];
    ps2tex->psm = hdr[13];
    ps2tex->uppsm = version ? hdr[14] : hdr[13];
    ps2tex->tcc = version ? hdr[15] >> 7 : hdr[15];
    ps2tex->gsused = nw_rd32(hdr + 8);
    entry->mmcnt = ps2tex->mipcount - 1;
    uint8_t *chunk = (uint8_t *)data + 16;
    if (entry->type >= 4 && entry->type < 8) {
        ps2tex->paldata = chunk + 16;
        ps2tex->palsize = nw_rd32(chunk + 4);
        ps2tex->cbw = nw_rd32(chunk + 8);
        ps2tex->cpsm = nw_rd32(chunk + 12);
        ps2tex->palstreamsize = nw_rd32(chunk) - 16;
        chunk += nw_rd32(chunk);
    }
    int head = version ? 20 : 16;
    for (int level = 0; level < ps2tex->mipcount; level++) {
        int size = nw_rd32(chunk), pad = nw_rd32(chunk + head - 4);
        ps2tex->mipdata[level] = chunk + pad + head;
        ps2tex->upsize[level] = nw_rd32(chunk + 4);
        ps2tex->tbw[level] = nw_rd32(chunk + 8);
        ps2tex->dbw[level] = nw_rd32(chunk + (version ? 12 : 8));
        ps2tex->reqsize[level] = size - head;
        chunk += size + pad;
    }
    ps2tex->tw = nw_log2(entry->width);
    ps2tex->th = nw_log2(entry->height);

    NwGpuTex *gpu = SDL_calloc(1, sizeof(*gpu));
    if (!gpu || !nw_tex_decode(&gpu->tex, data, (size_t)(chunk - (uint8_t *)data))) {
        NuErrorProlog(nutex_srcfile, 0)("texture %d: %s", slot + 1, gpu ? SDL_GetError() : "out of memory");
    }
    nw_ps2tx_set(ps2tex, gpu);
    return slot + 1;
}
