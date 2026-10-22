#include "gfx/mtl.h"

#include <SDL3/SDL.h>

#include "gfx/gsc.h"

/* CreateStateBlock's ALPHA_1 by attrib.alpha: 1 and the default (Cs - Cd) * As + Cd, 2 add, 3 subtract, ... */
static const uint8_t nw_mtl_alpha[16] = {
    0x44, 0x44, 0x48, 0x42, 0x49, 0x46, 0x89, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
};

/* NuTexWriteMaterialSettings' tables by attrib.filter and attrib.utc / vtc (retail 0x2D3F20, 0x2D3F30, 0x2D3F40). */
static const uint8_t nw_mtl_wrap[4] = { 0, 1, 0, 0 };
static const uint8_t nw_mtl_mmag[4] = { 0, 1, 1, 0 };
static const uint8_t nw_mtl_mmin[4] = { 0, 1, 5, 0 };

NwMtlState nw_mtl_state(const uint32_t attrib[2], int16_t k, uint8_t l, uint32_t levels)
{
    NwMtlState s;
    s.abe = NW_GSC_ATTRIB_ALPHA(attrib) != 0;
    s.alpha = nw_mtl_alpha[NW_GSC_ATTRIB_ALPHA(attrib)];
    s.ztst = NW_GSC_ATTRIB_ZMODE(attrib) >= 2 ? 1 : 2;
    s.zmsk = NW_GSC_ATTRIB_ZMODE(attrib) & 1;
    s.mmag = nw_mtl_mmag[NW_GSC_ATTRIB_FILTER(attrib)];
    s.mmin = nw_mtl_mmin[NW_GSC_ATTRIB_FILTER(attrib)];
    s.mxl = NW_GSC_ATTRIB_FILTER(attrib) == 2 && levels > 0 ? (uint8_t)(levels - 1) : 0;
    s.l = l & 3;
    s.k = k;
    s.wms = nw_mtl_wrap[NW_GSC_ATTRIB_UTC(attrib)];
    s.wmt = nw_mtl_wrap[NW_GSC_ATTRIB_VTC(attrib)];
    return s;
}

/* A colour's factor k + m * As as a GL factor and its sign; 1 + As has none. */
static bool nw_mtl_factor(int k, int m, NwBlendFactor *f, int *sign)
{
    *sign = 1;
    if (m == 0) {
        *f = k ? NW_BLEND_ONE : NW_BLEND_ZERO;
    } else if (k == 0) {
        *f = NW_BLEND_SRC_ALPHA;
        *sign = m;
    } else if (m < 0) {
        *f = NW_BLEND_ONE_MINUS_SRC_ALPHA;
    } else {
        return false;
    }
    return true;
}

bool nw_mtl_blend(uint8_t alpha, NwMtlBlend *b)
{
    unsigned a = alpha & 3, bb = (alpha >> 2) & 3, c = (alpha >> 4) & 3, d = (alpha >> 6) & 3;
    if (c != 0 || a == 3 || bb == 3 || d == 3) {
        return SDL_SetError("mtl: ALPHA 0x%02x", alpha);
    }
    /* Cs and Cd each get k + m * As: k from D, m from A - B (0 Cs, 1 Cd, 2 zero). */
    int ks = d == 0, kd = d == 1, ms = (a == 0) - (bb == 0), md = (a == 1) - (bb == 1), ss, sd;
    if (ks == 0 && ms == 0 && kd == 1 && md == 1) {
        *b = (NwMtlBlend){ NW_BLEND_ADD, NW_BLEND_DST_COLOUR, NW_BLEND_ONE, true };
        return true;
    }
    *b = (NwMtlBlend){ NW_BLEND_ADD, NW_BLEND_ZERO, NW_BLEND_ZERO, false };
    if (!nw_mtl_factor(ks, ms, &b->src, &ss) || !nw_mtl_factor(kd, md, &b->dst, &sd) || (ss < 0 && sd < 0)) {
        return SDL_SetError("mtl: ALPHA 0x%02x has no GL blend", alpha);
    }
    b->op = ss < 0 ? NW_BLEND_REVERSE_SUBTRACT : sd < 0 ? NW_BLEND_SUBTRACT : NW_BLEND_ADD;
    return true;
}
