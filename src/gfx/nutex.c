/* NuTexCreateNative override (port/units.toml): retail's TexList slot over the record, which stays in place. */
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "gfx/rndr.h"
#include "nu3d/nutex.h"
#include "nucore/nuerror.h"

#define nutex_srcfile "..\\nu2crash.ps2\\nu3d\\nutex.c"
#define NUTEX_VALID 0x10000

extern struct nutexslot_s *tinfo;
#define TexList tinfo
extern int tpid;
#define nutex_cnt tpid
extern int tex_allocmax;
#define nutex_max tex_allocmax
extern int nutex_alloc_cnt;

static int NuTexFindFreeSlot(void)
{
    int start = nutex_cnt;
    while (TexList[nutex_cnt].flags & NUTEX_VALID) {
        nutex_cnt++;
        if (nutex_cnt >= nutex_max) {
            nutex_cnt = 0;
        }
        if (start == nutex_cnt) {
            return -1;
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

/* An unknown header version takes a slot it never marks valid, as retail. */
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
    if (ps2tex->mipcount > (int)(sizeof(ps2tex->mipdata) / sizeof(ps2tex->mipdata[0]))) {
        NuErrorProlog(nutex_srcfile, 0)("texture %d: %d mip levels", slot + 1, ps2tex->mipcount);
    }
    for (int level = 0; level < ps2tex->mipcount; level++) {
        int size = nw_rd32(chunk), pad = nw_rd32(chunk + head - 4);
        if (size < head || pad < 0 || size > INT32_MAX - pad) {
            NuErrorProlog(nutex_srcfile, 0)("texture %d: mip level %d of %d bytes", slot + 1, level, size);
        }
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

struct NwTexture { unsigned refs; int tid; NwGpuTex *gpu; bool mips; };
static uint32_t png_word(const uint8_t *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

NwTexture *nw_texture_png(const void *bytes, size_t size)
{
    if (!TexList || nutex_max <= 0 || nutex_max > 0x400 || nutex_cnt < 0 || nutex_cnt >= nutex_max) {
        SDL_SetError("texture system unavailable or capacity exceeds renderer slots"); return NULL;
    }
    const uint8_t *p = bytes;
    if (!p || size < 45 || size > 64u*1024*1024 || memcmp(p,"\x89PNG\r\n\x1a\n",8) ||
        memcmp(p+8,"\0\0\0\rIHDR",8) || memcmp(p+size-12,"\0\0\0\0IEND\xae\x42\x60\x82",12) ||
        !p[24] || p[24] > 8) {
        SDL_SetError("invalid PNG header, size or bit depth (RGBA8 only)"); return NULL;
    }
    uint32_t w = png_word(p+16), h = png_word(p+20);
    int limit = nw_tex_max_side();
    if (!limit || !w || !h || w > (uint32_t)limit || h > (uint32_t)limit || (uint64_t)w*h*4 > 64u*1024*1024) {
        SDL_SetError("PNG dimensions exceed GPU or RGBA budget, or GPU limit unavailable"); return NULL;
    }
    SDL_Surface *image = SDL_LoadPNG_IO(SDL_IOFromConstMem(bytes,size),true);
    SDL_Surface *rgba = image ? SDL_ConvertSurface(image,SDL_PIXELFORMAT_ABGR8888) : NULL;
    SDL_DestroySurface(image);
    if (!rgba) return NULL;
    if (rgba->w != (int)w || rgba->h != (int)h) {
        SDL_DestroySurface(rgba); SDL_SetError("decoded PNG dimensions differ from its header"); return NULL;
    }
    NwTexture *texture = NULL;
    NwGpuTex *gpu = SDL_calloc(1,sizeof(*gpu));
    uint8_t *pixels = SDL_malloc((size_t)w*h*4);
    if (!gpu || !pixels) goto failed;
    for (uint32_t y = 0; y < h; y++) memcpy(pixels+(size_t)y*w*4,(uint8_t *)rgba->pixels+(size_t)y*rgba->pitch,(size_t)w*4);
    texture = SDL_malloc(sizeof(*texture));
    if (!texture) goto failed;
    int slot = NuTexFindFreeSlot();
    if (slot < 0) { SDL_SetError("no free texture slot for PNG"); goto failed; }
    struct nutexslot_s *entry = &TexList[slot];
    memset(entry,0,sizeof(*entry));
    entry->valid = entry->native = 1; entry->type = 3;
    entry->width = w; entry->height = h;
    entry->ps2tex.mipcount = entry->ps2tex.tcc = 1;
    entry->ps2tex.tw = nw_log2(w); entry->ps2tex.th = nw_log2(h);
    gpu->tex = (NwTex){.type=3,.tcc=true,.rgba8=true,.nlevels=1,.levels={{w,h,pixels}}};
    *texture = (NwTexture){.refs=1,.tid=slot+1,.gpu=gpu};
    nw_ps2tx_set(&entry->ps2tex,gpu);
    SDL_DestroySurface(rgba);
    return texture;
failed:
    SDL_free(texture); SDL_free(gpu); SDL_free(pixels); SDL_DestroySurface(rgba);
    return NULL;
}

bool nw_texture_retain(NwTexture *texture)
{
    if (!texture || texture->refs == UINT_MAX) return SDL_SetError("invalid texture reference count");
    texture->refs++; return true;
}

bool nw_texture_array(const short *tids, int count, NwTexture *reuse, NwTexture **output)
{
    return nw_texture_images(tids,count,false,reuse,output);
}
bool nw_texture_images(const short *tids, int count, bool mips, NwTexture *reuse, NwTexture **output)
{
    if (!output) return SDL_SetError("missing array output");
    *output = NULL;
    if (!tids || count <= 0) return SDL_SetError("invalid array texture list");
    const NwGpuTex *first = nw_rndr_texture(tids[0]);
    if (!first || !first->tex.nlevels || !first->tex.levels[0].texels)
        return SDL_SetError("array source is not a decoded model texture");
    const NwTex *base = &first->tex;
    uint32_t w = base->levels[0].width, h = base->levels[0].height;
    bool compatible = (count > 1 || mips) && (mips || base->nlevels == 1) && !base->layers;
    for (int n = 0; n < count; n++) {
        const NwGpuTex *s = nw_rndr_texture(tids[n]);
        if (!s || !s->tex.nlevels || !s->tex.levels[0].texels)
            return SDL_SetError("invalid array source at frame %d",n);
        const NwTex *t = &s->tex;
        compatible &= (mips || t->nlevels == 1) && !t->layers && t->levels[0].width == w &&
                      t->levels[0].height == h && t->tcc == base->tcc && t->rgba8 == base->rgba8;
    }
    if (!compatible) return !mips || SDL_SetError("generated mipmaps require compatible image dimensions and alpha conventions");
    if (!w || !h || w > (uint32_t)nw_tex_max_side() || h > (uint32_t)nw_tex_max_side() ||
        (count > 1 && count > nw_tex_max_layers()) || (uint64_t)w*h > (64u*1024*1024/4)/(unsigned)count)
        return SDL_SetError("array dimensions, layers or RGBA budget exceed limits");
    size_t pixels = (size_t)w*h, bytes = pixels*4*(unsigned)count;
    const NwTex *old = reuse ? &reuse->gpu->tex : NULL;
    bool same = old && reuse->mips == mips && old->layers == (count>1 ? (uint32_t)count : 0) && old->levels[0].width == w &&
                old->levels[0].height == h && old->tcc == base->tcc && old->rgba8 == base->rgba8;
    /* Compare immutable colours at binding time: recycled model IDs/pointers cannot reuse stale images. */
    for (int n = 0; same && n < count; n++) {
        const NwTex *t = &nw_rndr_texture(tids[n])->tex;
        const uint8_t *dst = old->levels[0].texels+(size_t)n*pixels*4;
        if (t->ncolours) {
            for (size_t p = 0; same && p < pixels; p++) same = !memcmp(dst+p*4,t->clut[t->levels[0].texels[p]],4);
        } else same = !memcmp(dst,t->levels[0].texels,pixels*4);
    }
    if (same) { if (!nw_texture_retain(reuse)) return false; *output = reuse; return true; }
    NwTexture *image = SDL_malloc(sizeof(*image));
    NwGpuTex *gpu = SDL_calloc(1,sizeof(*gpu));
    uint8_t *rgba = SDL_malloc(bytes);
    if (!image || !gpu || !rgba) { SDL_free(image); SDL_free(gpu); SDL_free(rgba); return false; }
    *image = (NwTexture){.refs=1,.gpu=gpu,.mips=mips};
    for (int n = 0; n < count; n++) {
        const NwGpuTex *s = nw_rndr_texture(tids[n]);
        uint8_t *dst = rgba+(size_t)n*pixels*4;
        if (s->tex.ncolours) {
            for (size_t p = 0; p < pixels; p++) memcpy(dst+p*4,s->tex.clut[s->tex.levels[0].texels[p]],4);
        } else memcpy(dst,s->tex.levels[0].texels,pixels*4);
    }
    gpu->tex = (NwTex){.type=3,.tcc=base->tcc,.rgba8=base->rgba8,.nlevels=1,
                       .layers=count>1 ? (uint32_t)count : 0,.levels={{w,h,rgba}}};
    if (mips && !nw_tex_mipmaps(&gpu->tex,64u<<20)) { nw_tex_free(&gpu->tex); SDL_free(gpu); SDL_free(image); return false; }
    nw_gpu_queue(NW_GPU_UPLOAD_TEX,gpu);
    *output = image;
    return true;
}

void nw_texture_release(NwTexture *texture)
{
    if (texture && !--texture->refs) {
        if (texture->tid) {
            nw_ps2tx_set(&TexList[texture->tid-1].ps2tex,NULL);
            NuTexDestroy(texture->tid); /* Native slot: the GPU queue owns its pixels. */
        } else nw_gpu_queue(NW_GPU_FREE_TEX,texture->gpu);
        SDL_free(texture);
    }
}

int nw_texture_tid(const NwTexture *texture) { return texture ? texture->tid : 0; }
const NwGpuTex *nw_texture_gpu(const NwTexture *texture) { return texture ? texture->gpu : NULL; }
