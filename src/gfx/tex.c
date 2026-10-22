#include "gfx/tex.h"

#include <string.h>

#include <SDL3/SDL.h>

#define NW_PSMCT32 0x00
#define NW_PSMCT24 0x01
#define NW_PSMT8 0x13
#define NW_PSMT4 0x14

/* GS local memory: the block order in a page and the pixel order in a block, as the GS manual tabulates them. */
static const uint8_t nw_gs_block32[4][8] = {
    { 0, 1, 4, 5, 16, 17, 20, 21 },
    { 2, 3, 6, 7, 18, 19, 22, 23 },
    { 8, 9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 },
};

static const uint8_t nw_gs_block4[8][4] = {
    { 0, 2, 8, 10 },    { 1, 3, 9, 11 },    { 4, 6, 12, 14 },   { 5, 7, 13, 15 },
    { 16, 18, 24, 26 }, { 17, 19, 25, 27 }, { 20, 22, 28, 30 }, { 21, 23, 29, 31 },
};

static const uint8_t nw_gs_column32[8][8] = {
    { 0, 1, 4, 5, 8, 9, 12, 13 },         { 2, 3, 6, 7, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 24, 25, 28, 29 },   { 18, 19, 22, 23, 26, 27, 30, 31 },
    { 32, 33, 36, 37, 40, 41, 44, 45 },   { 34, 35, 38, 39, 42, 43, 46, 47 },
    { 48, 49, 52, 53, 56, 57, 60, 61 },   { 50, 51, 54, 55, 58, 59, 62, 63 },
};

static const uint8_t nw_gs_column8[16][16] = {
    { 0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54 },
    { 8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62 },
    { 33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23 },
    { 41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31 },
    { 96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86 },
    { 104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94 },
    { 65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119 },
    { 73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127 },
    { 128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182 },
    { 136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190 },
    { 161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151 },
    { 169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159 },
    { 224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214 },
    { 232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222 },
    { 193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247 },
    { 201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255 },
};

static const uint16_t nw_gs_column4[16][32] = {
    { 0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
      4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110 },
    { 16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
      20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126 },
    { 65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43,
      69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47 },
    { 81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
      85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63 },
    { 192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
      196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174 },
    { 208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
      212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190 },
    { 129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
      133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239 },
    { 145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
      149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255 },
    { 256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362,
      260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366 },
    { 272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378,
      276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382 },
    { 321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299,
      325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303 },
    { 337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315,
      341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319 },
    { 448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426,
      452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430 },
    { 464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442,
      468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446 },
    { 385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491,
      389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495 },
    { 401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507,
      405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511 },
};

/* Byte address of a PSMCT32 pixel at block 0; bw is the buffer width in 64-pixel units. */
static size_t nw_gs_addr32(uint32_t x, uint32_t y, uint32_t bw)
{
    size_t page = (size_t)(y >> 5) * bw + (x >> 6);
    return (page * 2048 + nw_gs_block32[(y & 31) >> 3][(x & 63) >> 3] * 64u + nw_gs_column32[y & 7][x & 7]) * 4;
}

/* Byte address of a PSMT8 pixel: 128x64 pages. */
static size_t nw_gs_addr8(uint32_t x, uint32_t y, uint32_t bw)
{
    size_t page = (size_t)(y >> 6) * (bw >> 1) + (x >> 7);
    return page * 8192 + nw_gs_block32[(y & 63) >> 4][(x & 127) >> 4] * 256u + nw_gs_column8[y & 15][x & 15];
}

/* Nibble address of a PSMT4 pixel: 128x128 pages. */
static size_t nw_gs_addr4(uint32_t x, uint32_t y, uint32_t bw)
{
    size_t page = (size_t)(y >> 7) * (bw >> 1) + (x >> 7);
    return page * 16384 + nw_gs_block4[(y & 127) >> 4][(x & 127) >> 5] * 512u + nw_gs_column4[y & 15][x & 31];
}

static uint32_t nw_rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint16_t nw_rd16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

/* One upload packet's IMAGE bytes: DMA CNT/RET tags with DIRECT GIF data, A+D TRXREG = w x h, then IMAGE. */
static bool nw_tex_upload(const uint8_t *p, const uint8_t *end, uint32_t w, uint32_t h, size_t bytes, uint8_t *out)
{
    size_t got = 0;
    bool trxreg = false;
    while (got < bytes) {
        if (end - p < 16) {
            return SDL_SetError("tex: upload of %ux%u runs past its block", w, h);
        }
        uint32_t tag = nw_rd32(p), id = (tag >> 28) & 7, qwc = tag & 0xFFFF;
        if (id != 1 && id != 6 && id != 7) {
            return SDL_SetError("tex: DMA tag id %u", id);
        }
        if ((nw_rd32(p + 12) >> 24) != 0x50) {
            return SDL_SetError("tex: DMA tag without DIRECT");
        }
        const uint8_t *q = p + 16;
        /* The CLUT packets claim more quadwords than their block holds; the transfer is what TRXREG says. */
        const uint8_t *qend = (size_t)(end - q) / 16 < qwc ? end : q + (size_t)qwc * 16;
        while (end - q >= 16 && q < qend && got < bytes) {
            uint32_t nloop = nw_rd32(q) & 0x7FFF, flg = (nw_rd32(q + 4) >> 26) & 3, nreg = nw_rd32(q + 4) >> 28;
            q += 16;
            if (flg == 0 && nreg == 1 && nw_rd32(q - 8) == 0xE) {
                for (uint32_t k = 0; k < nloop; k++, q += 16) {
                    if (qend - q < 16) {
                        return SDL_SetError("tex: A+D data runs past its DMA tag");
                    }
                    if (q[8] == 0x52) {
                        trxreg = (nw_rd32(q) & 0xFFF) == w && (nw_rd32(q + 4) & 0xFFF) == h;
                        if (!trxreg) {
                            return SDL_SetError("tex: TRXREG %ux%u for an upload of %ux%u", nw_rd32(q) & 0xFFF,
                                                nw_rd32(q + 4) & 0xFFF, w, h);
                        }
                    }
                }
            } else if (flg == 2) {
                if (!trxreg) {
                    return SDL_SetError("tex: IMAGE data before TRXREG");
                }
                size_t n = SDL_min(SDL_min((size_t)nloop * 16, (size_t)(qend - q)), bytes - got);
                memcpy(out + got, q, n);
                got += n;
                q += SDL_min((size_t)nloop * 16, (size_t)(qend - q));
            } else {
                return SDL_SetError("tex: GIF tag FLG %u NREG %u", flg, nreg);
            }
        }
        if (id != 1 || qend == end) {
            break;
        }
        p = qend;
    }
    if (got < bytes) {
        return SDL_SetError("tex: upload of %ux%u has %zu of %zu bytes", w, h, got, bytes);
    }
    return true;
}

/* A PSMCT32 upload of uw x uh at buffer width dbw read back as a w x h PSMT8 or PSMT4 image at tbw. */
static bool nw_tex_unswizzle(const uint8_t *up, uint32_t uw, uint32_t uh, uint32_t dbw, uint32_t w, uint32_t h,
                             uint32_t tbw, bool psmt4, uint8_t *out)
{
    if (dbw == 0 || dbw * 64 < uw || tbw < 2 || tbw * 64 < w) {
        return SDL_SetError("tex: buffer widths %u/%u for %ux%u", dbw, tbw, w, h);
    }
    size_t wpages = (size_t)((uh - 1) >> 5) * dbw + ((uw - 1) >> 6) + 1;
    size_t rpages = (size_t)((h - 1) >> (psmt4 ? 7 : 6)) * (tbw >> 1) + ((w - 1) >> 7) + 1;
    uint8_t *mem = SDL_calloc(SDL_max(wpages, rpages), 8192);
    if (!mem) {
        return false;
    }
    for (uint32_t y = 0; y < uh; y++) {
        for (uint32_t x = 0; x < uw; x++) {
            memcpy(mem + nw_gs_addr32(x, y, dbw), up + ((size_t)y * uw + x) * 4, 4);
        }
    }
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            size_t n = psmt4 ? nw_gs_addr4(x, y, tbw) : 0;
            out[(size_t)y * w + x] = psmt4 ? (mem[n >> 1] >> ((n & 1) * 4)) & 15 : mem[nw_gs_addr8(x, y, tbw)];
        }
    }
    SDL_free(mem);
    return true;
}

/* The sampled and the upload psm of each type (GenTextureLoad); 0xFF for none. */
static const uint8_t nw_tex_psm[8][2] = {
    { 0xFF, 0xFF }, { 0xFF, 0xFF }, { NW_PSMCT24, NW_PSMCT24 }, { NW_PSMCT32, NW_PSMCT32 },
    { NW_PSMT4, NW_PSMT4 }, { NW_PSMT8, NW_PSMT8 }, { NW_PSMT4, NW_PSMCT32 }, { NW_PSMT8, NW_PSMCT32 },
};

/* One mip level: the upload in uppsm, as CLUT indices in the sampled layout or as r, g, b, a. */
static bool nw_tex_level(NwTex *t, NwTexLevel *l, const uint8_t *up, const uint8_t *end, uint32_t tbw, uint32_t dbw)
{
    uint32_t w = l->width, h = l->height;
    bool psmt4 = t->type == 4 || t->type == 6;
    bool swizzled = t->type >= 6, direct = t->type < 4;
    uint32_t uw = swizzled ? w / 2 : w, uh = swizzled ? (psmt4 ? h / 4 : h / 2) : h;
    size_t n = (size_t)w * h;
    size_t bytes = swizzled ? (size_t)uw * uh * 4 : psmt4 ? n / 2 : direct ? n * (t->type == 2 ? 3 : 4) : n;
    if (uw == 0 || uh == 0 || (swizzled && (uw * 2 != w || uh * (psmt4 ? 4 : 2) != h)) || (psmt4 && (w & 1))) {
        return SDL_SetError("tex: type %u level of %ux%u", t->type, w, h);
    }
    uint8_t *raw = SDL_malloc(bytes);
    l->texels = SDL_malloc(direct ? n * 4 : n);
    bool ok = raw && l->texels && nw_tex_upload(up, end, uw, uh, bytes, raw);
    if (ok && swizzled) {
        ok = nw_tex_unswizzle(raw, uw, uh, dbw, w, h, tbw, psmt4, l->texels);
    } else if (ok && psmt4) {
        for (size_t i = 0; i < n; i++) {
            l->texels[i] = (raw[i >> 1] >> ((i & 1) * 4)) & 15;
        }
    } else if (ok && t->type == 2) {
        for (size_t i = 0; i < n; i++) {
            memcpy(l->texels + i * 4, raw + i * 3, 3);
            l->texels[i * 4 + 3] = 0x80;
        }
    } else if (ok) {
        memcpy(l->texels, raw, bytes);
    }
    SDL_free(raw);
    return ok;
}

static bool nw_tex_read(NwTex *t, const uint8_t *p, size_t size)
{
    if (size < 16) {
        return SDL_SetError("tex: record of %zu bytes", size);
    }
    const uint8_t *end = p + size;
    uint32_t w, h, psm, uppsm, mips;
    t->version = nw_rd16(p + 2);
    if (t->version == 1) {
        w = nw_rd16(p);
        h = nw_rd16(p + 4);
        uppsm = p[14];
        mips = p[15] & 0x7F;
        t->tcc = p[15] >> 7;
    } else if (t->version == 0) {
        w = nw_rd32(p);
        h = nw_rd32(p + 4);
        uppsm = p[13];
        mips = p[14];
        t->tcc = p[15] != 0;
    } else {
        return SDL_SetError("tex: header version %u", t->version);
    }
    t->type = p[12];
    psm = p[13];
    bool psmt4 = t->type == 4 || t->type == 6;
    if (t->type < 2 || t->type > 7) {
        return SDL_SetError("tex: type %u", t->type);
    }
    if (psm != nw_tex_psm[t->type][0] || uppsm != nw_tex_psm[t->type][1]) {
        return SDL_SetError("tex: type %u with psm 0x%x uploaded as 0x%x", t->type, psm, uppsm);
    }
    if (mips == 0 || mips > NW_TEX_LEVELS || w == 0 || h == 0 || w > 1024 || h > 1024) {
        return SDL_SetError("tex: %ux%u with %u levels", w, h, mips);
    }
    const uint8_t *c = p + 16;
    if (t->type >= 4) {
        if (end - c < 16 || nw_rd32(c) < 16 || nw_rd32(c) > (size_t)(end - c)) {
            return SDL_SetError("tex: CLUT block runs past the record");
        }
        if (nw_rd32(c + 12) != NW_PSMCT32) {
            return SDL_SetError("tex: CLUT psm 0x%x", nw_rd32(c + 12));
        }
        t->ncolours = psmt4 ? 16 : 256;
        uint8_t raw[256][4];
        if (!nw_tex_upload(c + 16, c + nw_rd32(c), psmt4 ? 8 : 16, psmt4 ? 2 : 16, t->ncolours * 4, raw[0])) {
            return false;
        }
        for (uint32_t i = 0; i < t->ncolours; i++) {
            uint32_t csm1 = psmt4 ? i : (i & 0xE7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1);
            memcpy(t->clut[i], raw[csm1], 4);
        }
        c += nw_rd32(c);
    }
    uint32_t head = t->version == 1 ? 0x14 : 0x10;
    for (uint32_t i = 0; i < mips; i++) {
        if ((size_t)(end - c) < head) {
            return SDL_SetError("tex: level %u runs past the record", i);
        }
        uint32_t msize = nw_rd32(c), pad = nw_rd32(c + (t->version == 1 ? 16 : 12));
        uint32_t tbw = nw_rd32(c + 8), dbw = t->version == 1 ? nw_rd32(c + 12) : tbw;
        if (msize < head || pad > (size_t)(end - c) || msize > (size_t)(end - c) - pad) {
            return SDL_SetError("tex: level %u of %u bytes runs past the record", i, msize);
        }
        NwTexLevel *l = &t->levels[t->nlevels++];
        l->width = SDL_max(w >> i, 1u);
        l->height = SDL_max(h >> i, 1u);
        if (!nw_tex_level(t, l, c + head + pad, c + msize + pad, tbw, dbw)) {
            return false;
        }
        c += msize + pad;
    }
    return true;
}

bool nw_tex_decode(NwTex *t, const void *data, size_t size)
{
    memset(t, 0, sizeof(*t));
    if (!nw_tex_read(t, data, size)) {
        nw_tex_free(t);
        return false;
    }
    return true;
}

void nw_tex_free(NwTex *t)
{
    for (uint32_t i = 0; i < NW_TEX_LEVELS; i++) {
        SDL_free(t->levels[i].texels);
    }
    memset(t, 0, sizeof(*t));
}

void nw_tex_rgba(const NwTex *t, uint32_t y, uint8_t *out)
{
    const NwTexLevel *l = &t->levels[0];
    for (size_t i = (size_t)y * l->width; i < (size_t)(y + 1) * l->width; i++, out += 4) {
        const uint8_t *c = t->ncolours ? t->clut[l->texels[i]] : l->texels + i * 4;
        out[0] = c[0];
        out[1] = c[1];
        out[2] = c[2];
        out[3] = (uint8_t)SDL_min(255, c[3] * 2);
    }
}
