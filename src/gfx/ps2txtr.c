/* nups2/ps2txtr, which the port replaces (port/units.toml): the textures of the TexList slots decoded for the
   renderer instead of GS upload packets. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "gfx/rndr.h"
#include "nu3d/nutex.h"
#include "nucore/nuerror.h"
#include "port/heap.h"

#define ps2txtr_srcfile "..\\nu2crash.ps2\\nups2\\ps2txtr.c"
#define NW_TEX_SLOTS 0x400 /* NUTEX_MAX */

extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC
extern int gs_botfree;
extern int PS2_REZ_W;
extern int PS2_REZ_H;
extern int numtl_force_mipmap;

int NuTexReserve(int size);
void *NuMemAllocFn(int size, char *file, int line);
void NuMemFreeFn(void *ptr, char *file, int line);

static NwGpuTex *nw_ps2tx[NW_TEX_SLOTS];

void nw_ps2tx_set(struct nups2tex_s *tex, NwGpuTex *gpu)
{
    ptrdiff_t slot = ((char *)tex - (char *)&TexList[0].ps2tex) / (ptrdiff_t)sizeof(struct nutexslot_s);
    if (nw_ps2tx[slot]) {
        nw_gpu_queue(NW_GPU_FREE_TEX, nw_ps2tx[slot]);
    }
    nw_ps2tx[slot] = gpu;
    if (gpu) {
        nw_gpu_queue(NW_GPU_UPLOAD_TEX, gpu);
    }
}

const NwGpuTex *nw_rndr_texture(int tid)
{
    return tid > 0 && tid <= NW_TEX_SLOTS ? nw_ps2tx[tid - 1] : NULL;
}

void nw_rndr_texture_free(int tid)
{
    if (tid > 0 && tid <= NW_TEX_SLOTS && nw_ps2tx[tid - 1]) {
        nw_gpu_queue(NW_GPU_FREE_TEX, nw_ps2tx[tid - 1]);
        nw_ps2tx[tid - 1] = NULL;
    }
}

int Ps2TxValToPowerOfTwo(int value)
{
    int shift = 0, orig = value;
    while (value >= 2) {
        value >>= 1;
        shift++;
    }
    if (orig & ((1 << shift) - 1)) {
        shift++;
    }
    return shift;
}

/* Texel n of a bitmap as GenTextureLoad writes it to the GS: type 2 is B, G, R; type 3 A, B, G, R with the alpha
   halved (nutex_force_overbrighten, which keeps it, is never set); type 4 has the even texel in the high nibble. */
static void nw_ps2tx_texel(const struct nutex_s *spec, size_t n, uint8_t *out)
{
    const uint8_t *bits = spec->bits;
    const uint8_t *s;
    switch (spec->type) {
    case 2:
        s = bits + n * 3;
        out[0] = s[2];
        out[1] = s[1];
        out[2] = s[0];
        out[3] = 0x80;
        break;
    case 3:
        s = bits + n * 4;
        out[0] = s[3];
        out[1] = s[2];
        out[2] = s[1];
        out[3] = s[0] >> 1;
        break;
    case 4:
        out[0] = (bits[n >> 1] >> (n & 1 ? 0 : 4)) & 15;
        break;
    default:
        out[0] = bits[n];
        break;
    }
}

/* GenTextureLoad's levels: mmcnt of them one after the other in bits, otherwise one that takes every 2^skip-th
   texel of a bitmap over 512, stopping before a level under 8 texels. nutex_force_quantisation and
   numtl_force_mipmap are never set. */
static bool nw_ps2tx_bitmap(NwTex *t, const struct nutex_s *spec)
{
    if (spec->type < 2 || spec->type > 5) {
        return SDL_SetError("bitmap type %u", spec->type);
    }
    int w = spec->width, h = spec->height, skip = 0;
    while (w > 0x200 || h > 0x200) {
        w >>= 1;
        h >>= 1;
        skip++;
    }
    t->type = spec->type;
    t->tcc = spec->type != 2;
    if (spec->type >= 4) {
        t->ncolours = spec->type == 4 ? 16 : 256;
        memcpy(t->clut, spec->pal, t->ncolours * 4);
    }
    bool direct = spec->type < 4;
    int levels = spec->mmcnt ? spec->mmcnt : 1, step = spec->mmcnt ? 1 : 1 << skip;
    size_t offset = 0;
    for (int i = 0; i < levels && i < NW_TEX_LEVELS && (w >> i) >= 8 && (h >> i) >= 8; i++) {
        NwTexLevel *l = &t->levels[t->nlevels++];
        int stride = spec->mmcnt ? w >> i : spec->width;
        l->width = w >> i;
        l->height = h >> i;
        l->texels = SDL_malloc((size_t)l->width * l->height * (direct ? 4 : 1));
        if (!l->texels) {
            return false;
        }
        for (uint32_t y = 0; y < l->height; y++) {
            for (uint32_t x = 0; x < l->width; x++) {
                nw_ps2tx_texel(spec, offset + ((size_t)y * stride + x) * step,
                               l->texels + ((size_t)y * l->width + x) * (direct ? 4 : 1));
            }
        }
        offset += (size_t)(spec->width >> i) * (spec->height >> i);
    }
    return true;
}

/* Once per kind: what of GS memory the port does not draw. */
static void nw_ps2tx_unsupported(int kind, const char *what)
{
    static unsigned reported;
    if (!(reported & 1u << kind)) {
        reported |= 1u << kind;
        fprintf(stderr, NW_APP_NAME ": %s is not drawn\n", what);
    }
}

NwSurf nw_ps2tx_surf(int tid)
{
    NwSurf s = { 0, 0, 0, false };
    if (tid == NW_SURF_BACK || tid == NW_SURF_FRONT) {
        s.addr = tid;
        s.tw = (uint16_t)(1 << Ps2TxValToPowerOfTwo(PS2_REZ_W));
        s.th = (uint16_t)(1 << Ps2TxValToPowerOfTwo(PS2_REZ_H));
        s.tcc = true; /* the frame buffer is PSMCT32 */
    } else if (tid == -2) {
        nw_ps2tx_unsupported(0, "the Z buffer as a texture");
    } else if (tid > 0 && TexList[tid - 1].valid && TexList[tid - 1].type >= 8) {
        const struct nutexslot_s *e = &TexList[tid - 1];
        if (e->type <= 10) {
            nw_ps2tx_unsupported(1, "a palette render target (types 8-10)");
        } else if (e->type >= 17) {
            nw_ps2tx_unsupported(2, "a Z render target (types 17-19)");
        } else {
            s.addr = e->ps2tex.tbp[0] ? e->ps2tex.tbp[0] : e->ps2tex.gsaddr ? e->ps2tex.gsaddr : gs_botfree;
            s.tw = (uint16_t)(1 << e->ps2tex.tw);
            s.th = (uint16_t)(1 << e->ps2tex.th);
            s.tcc = e->ps2tex.tcc;
        }
    }
    return s;
}

/* A render target's GS format and place: types 11-13 reserve their memory (NuTexReserve), the others take the
   address NuTexAssignAddr gives them, else gs_botfree. 8-10 are read back through a ramp CLUT and 17-19 hold Z,
   neither of which the port draws. */
int NuPs2CreateRenderTarget(struct nutex_s *spec, struct nups2tex_s *tex)
{
    static const uint8_t psm[12] = { 0x13, 0x13, 0x1B, 0x00, 0x01, 0x02, 0x00, 0x01, 0x02, 0x30, 0x31, 0x32 };
    static const uint8_t bpp[12] = { 2, 2, 2, 4, 4, 2, 4, 4, 2, 4, 4, 2 };
    int type = spec->type;
    memset(tex, 0, sizeof(*tex));
    tex->tw = Ps2TxValToPowerOfTwo(spec->width);
    tex->th = Ps2TxValToPowerOfTwo(spec->height);
    tex->psm = psm[type - 8];
    tex->tcc = type <= 10 || type % 3 != 0; /* PSMCT24 (12, 15, 18) has no alpha */
    int align = type <= 9 ? 0x80 : 0x40;
    int size = (spec->width * spec->height * bpp[type - 8] + 15) & ~15;
    tex->tbp[0] = type >= 11 && type <= 13 ? NuTexReserve(size) : 0;
    tex->tbw[0] = ((spec->width + align - 1) & -align) / 64;
    tex->reqsize[0] = (size + 255) / 256;
    nw_ps2tx_set(tex, NULL);
    return 1;
}

/* GenTextureLoad's GS formats by bitmap type 2-5. */
static const uint8_t nw_ps2tx_psm[4] = { 0x01, 0x00, 0x14, 0x13 };

/* Retail's upload packets, never filled here: GenTextureLoad's per mip level, GenPaletteLoad's for types 4-7. Inside
   NuMemSetExternal they come off the caller's buffer (the time tunnel's texture off the superbuffer). */
static void nw_ps2tx_charge(const struct nutex_s *spec, struct nups2tex_s *tex)
{
    int w = spec->width, h = spec->height;
    while (w > 0x200 || h > 0x200) {
        w >>= 1;
        h >>= 1;
    }
    int levels = !spec->bits ? 0 : spec->mmcnt ? spec->mmcnt : numtl_force_mipmap ? 7 : 1;
    for (int i = 0; i < levels && i < 7 && w >> i >= 8 && h >> i >= 8; i++) {
        int texels = (w >> i) * (h >> i);
        int size = spec->type == 4 ? texels / 2 : spec->type == 5 ? texels : spec->type == 2 ? texels * 3
                 : spec->type == 3 || spec->type >= 6 ? texels * 4 : texels * 2;
        int alloc = ((size + 15) & ~15) + 0x90;
        alloc += (alloc + 0x7CFFF) / 0x7D000 * 32;
        tex->mipdata[i] = NuMemAllocFn(alloc, ps2txtr_srcfile, 0x250);
        tex->reqsize[i] = alloc;
        nw_heap_hole(tex->mipdata[i], (size_t)alloc, "texture packet");
    }
    if (spec->type >= 4 && spec->type <= 7) {
        int size = (spec->type == 4 || spec->type == 6 ? 0x40 : 0x400) + 0x80;
        tex->paldata = NuMemAllocFn(size, ps2txtr_srcfile, 0x3ED);
        tex->palstreamsize = tex->paldata ? size : 0;
        nw_heap_hole(tex->paldata, (size_t)size, "palette packet");
    }
}

int NuPs2TxCreate(struct nutex_s *spec, struct nups2tex_s *tex)
{
    if (spec->type >= 8) {
        if (spec->type < 0x14) {
            return NuPs2CreateRenderTarget(spec, tex);
        }
        NuErrorProlog(ps2txtr_srcfile, 0x640)("texture type %u", spec->type);
        return 0;
    }
    memset(tex, 0, sizeof(*tex));
    nw_ps2tx_charge(spec, tex);
    tex->tw = Ps2TxValToPowerOfTwo(spec->width);
    tex->th = Ps2TxValToPowerOfTwo(spec->height);
    /* Without bits retail uploads a palette type's CLUT alone (ShadComInit's palettes). */
    bool clut = !spec->bits && spec->pal && (spec->type == 4 || spec->type == 5);
    if (!spec->bits && !clut) {
        nw_ps2tx_set(tex, NULL);
        return 1;
    }
    NwGpuTex *gpu = SDL_calloc(1, sizeof(*gpu));
    if (gpu && clut) {
        gpu->tex.type = spec->type;
        gpu->tex.tcc = true;
        gpu->tex.ncolours = spec->type == 4 ? 16 : 256;
        memcpy(gpu->tex.clut, spec->pal, gpu->tex.ncolours * 4);
        tex->psm = tex->uppsm = nw_ps2tx_psm[spec->type - 2];
        tex->tcc = true;
        nw_ps2tx_set(tex, gpu);
        return 1;
    }
    if (!gpu || !nw_ps2tx_bitmap(&gpu->tex, spec)) {
        NuErrorProlog(ps2txtr_srcfile, 0)("%s", gpu ? SDL_GetError() : "out of memory");
        return 0;
    }
    tex->mipcount = gpu->tex.nlevels;
    tex->psm = tex->uppsm = nw_ps2tx_psm[spec->type - 2];
    tex->tcc = gpu->tex.tcc;
    nw_ps2tx_set(tex, gpu);
    return 1;
}

int NuPs2TxDestroy(struct nups2tex_s *tex)
{
    for (int i = 0; i < 7; i++) {
        if (tex->mipdata[i]) {
            NuMemFreeFn(tex->mipdata[i], ps2txtr_srcfile, 0x136);
        }
    }
    if (tex->paldata) {
        NuMemFreeFn(tex->paldata, ps2txtr_srcfile, 0x139);
    }
    nw_ps2tx_set(tex, NULL);
    return 0;
}
