/* nups2/ps2txtr, which the port replaces (port/units.toml): the textures of the TexList slots decoded for the
   renderer instead of GS upload packets. */
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/rndr.h"
#include "nu3d/nutex.h"
#include "nucore/nuerror.h"

#define ps2txtr_srcfile "..\\nu2crash.ps2\\nups2\\ps2txtr.c"
#define NW_TEX_SLOTS 0x400 /* NUTEX_MAX */

extern struct nutexslot_s *D_0062EBEC;
#define TexList D_0062EBEC

int NuPs2CreateRenderTarget(struct nutex_s *spec, struct nups2tex_s *tex);

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

/* GenTextureLoad's GS formats by bitmap type 2-5. */
static const uint8_t nw_ps2tx_psm[4] = { 0x01, 0x00, 0x14, 0x13 };

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
    tex->tw = Ps2TxValToPowerOfTwo(spec->width);
    tex->th = Ps2TxValToPowerOfTwo(spec->height);
    if (!spec->bits) {
        nw_ps2tx_set(tex, NULL);
        return 1;
    }
    NwGpuTex *gpu = SDL_calloc(1, sizeof(*gpu));
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
    nw_ps2tx_set(tex, NULL);
    return 0;
}
