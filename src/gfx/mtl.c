#include "gfx/mtl.h"

#include <math.h>

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
    s.ate = s.abe;
    s.atst = NW_GSC_ATTRIB_ATST(attrib);
    s.aref = NW_GSC_ATTRIB_AREF(attrib);
    s.afail = NW_GSC_ATTRIB_AFAIL(attrib);
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

uint32_t nw_mtl_passes(const NwMtlState *s, NwMtlPass pass[2])
{
    pass[0] = (NwMtlPass){ false, true, true, !s->zmsk };
    if (!s->ate || s->atst == 1 || s->afail == 0) {
        return 1;
    }
    static const NwMtlPass afail[4] = {
        { true, false, false, false }, { true, true, true, false }, { true, false, false, true },
        { true, true, false, false },
    };
    pass[1] = afail[s->afail];
    pass[1].z = pass[1].z && !s->zmsk;
    return 2;
}

/* NuTrigTable[((int)index + add) & 0xFFFF], NuTrigInit's sin(i * 2pi / 65536); the int conversion saturates. */
static float nw_mtl_trig(float index, uint32_t add)
{
    int32_t i = index >= 2147483648.0f ? INT32_MAX : index <= -2147483648.0f ? INT32_MIN : (int32_t)index;
    return (float)sin((float)(((uint32_t)i + add) & 0xFFFF) * 0.0000958738f);
}

static float nw_mtl_axis(uint8_t mode, float *d, float s, float sinetime, float dt, float offset)
{
    switch (mode) {
    case 1:
        return *d;
    case 2:
        *d += s * dt;
        *d = *d - floorf(*d);
        return *d;
    case 3:
        return nw_mtl_trig(sinetime * s * 65536.0f, 0) * *d;
    case 4:
        return nw_mtl_trig(sinetime * s * 65536.0f, 0x4000) * *d;
    default:
        return offset;
    }
}

void nw_mtl_animate(NwMtlScroll *s, float sinetime, float dt)
{
    s->u = nw_mtl_axis(s->umode, &s->du, s->su, sinetime, dt, s->u);
    s->v = nw_mtl_axis(s->vmode, &s->dv, s->sv, sinetime, dt, s->v);
}
