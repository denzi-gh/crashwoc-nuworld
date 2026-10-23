#include "gfx/vif.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#define NW_VIF_MSCAL_TRI 0x300    /* the package's triangle program at VU 0x1800 */
#define NW_VIF_MSCAL_FACEON 0x304 /* its jump to faceon */
#define NW_VIF_MSCAL_FACEY 0x306  /* and to facey */
#define NW_VIF_GIFTAG 0xD2     /* the batch's strip GIF tag; its NLOOP is the vertex count */

/* The VIF unit as far as the geometry stream drives it, and the open batch. */
typedef struct NwVif {
    NwVifMesh *mesh;
    uint32_t row[4];
    uint32_t hdr[4]; /* the last V4-32 to VU 2: the compressed positions' offset in hdr[1..3] */
    unsigned cl, wl, mode, nloop;
    int uvmode; /* the STMOD of the geom's texcoord unpacks, -1 before the first */
    bool has_hdr, has_nloop, open, has_tex, has_col;
    uint32_t count; /* vertices of the open batch, from mesh->nverts */
    bool adc[256];
} NwVif;

static uint32_t nw_rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static float nw_f32(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static bool nw_vif_reserve(void **items, uint32_t *cap, uint64_t need, size_t size)
{
    if (need <= *cap) {
        return true;
    }
    if (need > UINT32_MAX / 2) {
        return SDL_SetError("vif: mesh too large");
    }
    uint32_t n = *cap ? *cap : 256;
    while (n < need) {
        n *= 2;
    }
    void *p = SDL_realloc(*items, (size_t)n * size);
    if (!p) {
        return false;
    }
    *items = p;
    *cap = n;
    return true;
}

/* Positions open a batch (STCYCL 3,1 puts vertex i at VU 3+3i, its texcoord at 4+3i, its colour at 5+3i). */
static bool nw_vif_positions(NwVif *v, unsigned vn, unsigned vl, bool usn, unsigned n, const uint8_t *d)
{
    NwVifMesh *m = v->mesh;
    if (v->open) {
        return SDL_SetError("vif: second position unpack in a batch");
    }
    if (vn != 3 || vl > 1) {
        return SDL_SetError("vif: position unpack V%u-%u", vn + 1, 32 >> vl);
    }
    if (vl == 1 && (v->mode != 1 || !v->has_hdr)) {
        return SDL_SetError("vif: V4-16 positions without STMOD 1 and a VU 2 header");
    }
    if (vl == 0 && v->mode != 0) {
        return SDL_SetError("vif: V4-32 positions under STMOD %u", v->mode);
    }
    if (!nw_vif_reserve((void **)&m->verts, &m->vcap, (uint64_t)m->nverts + n, sizeof(NwVifVertex))) {
        return false;
    }
    NwVifVertex *out = m->verts + m->nverts;
    memset(out, 0, n * sizeof(*out));
    for (unsigned i = 0; i < n; i++) {
        if (vl == 0) {
            memcpy(out[i].pos, d + i * 16, 16);
            v->adc[i] = (nw_rd32(d + i * 16 + 12) & 0x8000) != 0;
            continue;
        }
        /* STMOD 1 adds the row to the 16-bit value; the VU subtracts the header (nus_scene.py _rowadd). */
        int32_t c[4];
        for (int k = 0; k < 4; k++) {
            uint16_t h;
            memcpy(&h, d + i * 8 + k * 2, 2);
            c[k] = usn ? (int32_t)h : (int32_t)(int16_t)h;
        }
        for (int k = 0; k < 3; k++) {
            double f = nw_f32(v->row[k] + (uint32_t)c[k]);
            out[i].pos[k] = (float)(f - (double)nw_f32(v->hdr[k + 1]));
        }
        v->adc[i] = c[3] != 0;
    }
    v->open = true;
    v->has_tex = v->has_col = false;
    v->count = n;
    return true;
}

static bool nw_vif_texcoords(NwVif *v, unsigned vn, unsigned vl, bool usn, unsigned n, const uint8_t *d)
{
    if (v->has_tex || n != v->count) {
        return SDL_SetError("vif: texcoord unpack of %u for a batch of %u", n, v->count);
    }
    if ((vn != 1 && vn != 3) || vl != 1 || usn) {
        return SDL_SetError("vif: texcoord unpack V%u-%u%s", vn + 1, 32 >> vl, usn ? " unsigned" : "");
    }
    /* Under STMOD 1 the texcoords add the material's STROW row (its UV scroll), which the decode leaves out. */
    if (v->uvmode >= 0 && (unsigned)v->uvmode != v->mode) {
        return SDL_SetError("vif: texcoords under STMOD 0 and 1 in one geom");
    }
    v->uvmode = (int)v->mode;
    NwVifVertex *out = v->mesh->verts + v->mesh->nverts;
    for (unsigned i = 0; i < n; i++) {
        memcpy(out[i].tex, d + i * (vn + 1) * 2, (vn + 1) * 2);
    }
    v->has_tex = true;
    return true;
}

static bool nw_vif_colours(NwVif *v, unsigned vn, unsigned vl, unsigned n, const uint8_t *d)
{
    if (v->has_col || n != v->count) {
        return SDL_SetError("vif: colour unpack of %u for a batch of %u", n, v->count);
    }
    if (vn != 3 || vl != 2) {
        return SDL_SetError("vif: colour unpack V%u-%u", vn + 1, 32 >> vl);
    }
    NwVifVertex *out = v->mesh->verts + v->mesh->nverts;
    for (unsigned i = 0; i < n; i++) {
        memcpy(out[i].col, d + i * 4, 4);
    }
    v->has_col = true;
    return true;
}

static bool nw_vif_finite(const NwVifVertex *v)
{
    return isfinite(v->pos[0]) && isfinite(v->pos[1]) && isfinite(v->pos[2]);
}

/* The kick: the batch's tri-strips, restarting at ADC, skipping triangles on non-finite vertices. */
static bool nw_vif_close(NwVif *v, unsigned cmd, unsigned imm)
{
    NwVifMesh *m = v->mesh;
    if (cmd != 0x14 && cmd != 0x15) {
        return SDL_SetError("vif: batch closed by VIF command 0x%02x", cmd);
    }
    if (imm != NW_VIF_MSCAL_TRI) {
        return SDL_SetError("vif: batch kicked at 0x%x", imm);
    }
    if (!v->has_tex || !v->has_col) {
        return SDL_SetError("vif: batch without %s", v->has_tex ? "colours" : "texcoords");
    }
    if (!v->has_nloop || v->nloop != v->count) {
        return SDL_SetError("vif: batch of %u vertices under a GIF tag of NLOOP %u", v->count,
                            v->has_nloop ? v->nloop : 0);
    }
    if (!nw_vif_reserve((void **)&m->tris, &m->tcap, (uint64_t)m->ntris + v->count, 3 * sizeof(uint32_t))) {
        return false;
    }
    const NwVifVertex *vtx = m->verts + m->nverts;
    uint32_t *t = m->tris + 3 * (size_t)m->ntris;
    for (uint32_t i = 2; i < v->count; i++) {
        if (v->adc[i] || !nw_vif_finite(&vtx[i - 2]) || !nw_vif_finite(&vtx[i - 1]) || !nw_vif_finite(&vtx[i])) {
            continue;
        }
        uint32_t b = m->nverts + i;
        t[0] = b - 2;
        t[1] = i & 1 ? b : b - 1;
        t[2] = i & 1 ? b - 1 : b;
        t += 3;
        m->ntris++;
    }
    m->nverts += v->count;
    m->nbatches++;
    v->open = v->has_nloop = false;
    return true;
}

static bool nw_vif_unpack(NwVif *v, uint32_t w, const uint8_t *d)
{
    unsigned cmd = (w >> 24) & 0x7F, vn = (cmd >> 2) & 3, vl = cmd & 3, addr = w & 0x3FF;
    unsigned n = (w >> 16) & 0xFF ? (w >> 16) & 0xFF : 256;
    bool usn = (w & 0x4000) != 0;
    if (addr == 3 && v->cl == 3 && v->wl == 1) {
        return nw_vif_positions(v, vn, vl, usn, n, d);
    }
    if (v->open && addr == 4) {
        return nw_vif_texcoords(v, vn, vl, usn, n, d);
    }
    if (v->open && addr == 5) {
        return nw_vif_colours(v, vn, vl, n, d);
    }
    if (vn == 3 && vl == 0 && addr == 2) {
        memcpy(v->hdr, d, 16);
        v->has_hdr = true;
    } else if (vn == 3 && vl == 0 && addr == NW_VIF_GIFTAG && n == 1) {
        v->nloop = nw_rd32(d) & 0x7FFF;
        v->has_nloop = true;
    }
    return true;
}

/* The VIF stream: 4-byte codes with their data inline. */
static bool nw_vif_stream(NwVif *v, const uint8_t *s, size_t n)
{
    size_t p = 0;
    while (p < n) {
        uint32_t w = nw_rd32(s + p);
        unsigned cmd = (w >> 24) & 0x7F, imm = w & 0xFFFF;
        size_t at = p;
        p += 4;
        if (cmd >= 0x60) {
            unsigned vn = (cmd >> 2) & 3, vl = cmd & 3, num = (w >> 16) & 0xFF ? (w >> 16) & 0xFF : 256;
            if (cmd & 0x10) {
                return SDL_SetError("vif: masked unpack at 0x%zx", at);
            }
            size_t bytes = vl == 3 ? (size_t)num * 2 : (size_t)num * (vn + 1) * (32u >> vl) / 8;
            bytes = (bytes + 3) & ~(size_t)3;
            if (vl == 3 && vn != 3) {
                return SDL_SetError("vif: unpack 0x%02x at 0x%zx", cmd, at);
            }
            if (bytes > n - p) {
                return SDL_SetError("vif: unpack at 0x%zx runs past the end", at);
            }
            if (!nw_vif_unpack(v, w, s + p)) {
                return false;
            }
            p += bytes;
            continue;
        }
        size_t data = cmd == 0x20 ? 4 : cmd == 0x30 || cmd == 0x31 ? 16 : 0;
        if (data > n - p) {
            return SDL_SetError("vif: VIF command 0x%02x at 0x%zx runs past the end", cmd, at);
        }
        switch (cmd) {
        case 0x00: /* NOP */
        case 0x02: /* OFFSET */
        case 0x03: /* BASE */
        case 0x04: /* ITOP */
        case 0x06: /* MSKPATH3 */
        case 0x07: /* MARK */
        case 0x10: /* FLUSHE */
        case 0x11: /* FLUSH */
        case 0x13: /* FLUSHA */
        case 0x20: /* STMASK */
        case 0x31: /* STCOL */
            break;
        case 0x01: /* STCYCL */
            v->cl = imm & 0xFF;
            v->wl = imm >> 8;
            break;
        case 0x05: /* STMOD */
            v->mode = imm & 3;
            if (v->mode > 1) {
                return SDL_SetError("vif: STMOD %u at 0x%zx", v->mode, at);
            }
            break;
        case 0x14: /* MSCAL */
        case 0x15: /* MSCALF */
        case 0x17: /* MSCNT */
            if (v->open && !nw_vif_close(v, cmd, imm)) {
                return false;
            }
            break;
        case 0x30: /* STROW */
            memcpy(v->row, s + p, 16);
            break;
        default:
            return SDL_SetError("vif: VIF command 0x%02x at 0x%zx", cmd, at);
        }
        p += data;
    }
    if (v->open) {
        return SDL_SetError("vif: batch not kicked");
    }
    return true;
}

/* The DMA chain's VIF stream, in the order the DMA sends it: each tag's two VIF words, then its data. */
static uint8_t *nw_vif_chain(const uint8_t *g, size_t size, size_t *len)
{
    uint8_t *s = SDL_malloc(size ? size : 1);
    size_t off = 0, n = 0;
    if (!s) {
        return NULL;
    }
    for (;;) {
        if (size < 16 || off > size - 16 || (off & 15)) {
            SDL_free(s);
            SDL_SetError("vif: DMA tag at 0x%zx outside the geom", off);
            return NULL;
        }
        uint32_t lo = nw_rd32(g + off), addr = nw_rd32(g + off + 4);
        size_t qwc = lo & 0xFFFF, end = off + 16 + qwc * 16;
        unsigned id = (lo >> 28) & 7;
        if (end > size) {
            SDL_free(s);
            SDL_SetError("vif: DMA tag at 0x%zx runs past the geom", off);
            return NULL;
        }
        if (8 + qwc * 16 > size - n) { /* every tag sends at least 8 bytes, so a loop ends here */
            SDL_free(s);
            SDL_SetError("vif: DMA chain at 0x%zx sends more than the geom holds", off);
            return NULL;
        }
        memcpy(s + n, g + off + 8, 8);
        memcpy(s + n + 8, g + off + 16, qwc * 16);
        n += 8 + qwc * 16;
        if (id == 6 || id == 7) { /* RET with nothing to return to, END */
            break;
        }
        if (id == 1) { /* CNT */
            off = end;
        } else if (id == 2) { /* NEXT, relative to the geom since retail's ReadObjSet relocates it */
            off = addr;
        } else {
            SDL_free(s);
            SDL_SetError("vif: DMA tag id %u at 0x%zx", id, off);
            return NULL;
        }
    }
    *len = n;
    return s;
}

bool nw_vif_decode(NwVifMesh *mesh, const void *geom, size_t size)
{
    size_t n = 0;
    uint8_t *s = nw_vif_chain(geom, size, &n);
    if (!s) {
        return false;
    }
    NwVif v = { .mesh = mesh, .uvmode = -1 };
    uint32_t nverts = mesh->nverts, ntris = mesh->ntris, nbatches = mesh->nbatches;
    bool ok = nw_vif_stream(&v, s, n);
    SDL_free(s);
    if (!ok) {
        mesh->nverts = nverts;
        mesh->ntris = ntris;
        mesh->nbatches = nbatches;
    } else {
        mesh->uvrow = v.uvmode == 1;
    }
    return ok;
}

/* A count, then a block per triangle: a REF tag and three vertices of three V4-32 quadwords, x, y, z and the normal's
   x as floats, u, v and the normal's y, z x4096 as ints, r, g, b, a as ints. The normal saturates: only the lit
   programs read it, and the unlit batches carry ones that are not unit length. */
bool nw_vif_otbatch(NwVifMesh *mesh, const void *batch, size_t size)
{
    const uint8_t *p = batch;
    uint32_t count = size >= 16 ? nw_rd32(p) : 0;
    if (size < 16 || count > (size - 16) / 0xA0) {
        return SDL_SetError("vif: %u triangles in an OT batch of %zu bytes", count, size);
    }
    uint64_t nverts = (uint64_t)mesh->nverts + count * 3u, ntris = (uint64_t)mesh->ntris + count;
    if (!nw_vif_reserve((void **)&mesh->verts, &mesh->vcap, nverts, sizeof(*mesh->verts)) ||
        !nw_vif_reserve((void **)&mesh->tris, &mesh->tcap, ntris * 3, sizeof(*mesh->tris))) {
        return false;
    }
    for (uint32_t t = 0; t < count; t++) {
        const uint8_t *v = p + 16 + (size_t)t * 0xA0 + 16;
        for (int k = 0; k < 3; k++, v += 0x30) {
            NwVifVertex *out = &mesh->verts[mesh->nverts + t * 3 + k];
            memcpy(out->pos, v, 16);
            for (int i = 0; i < 4; i++) {
                int32_t tex = (int32_t)nw_rd32(v + 16 + i * 4), col = (int32_t)nw_rd32(v + 32 + i * 4);
                if ((i < 2 && (tex < INT16_MIN || tex > INT16_MAX)) || col < 0 || col > 255) {
                    return SDL_SetError("vif: OT triangle %u vertex %d out of range", t, k);
                }
                out->tex[i] = (int16_t)(tex < INT16_MIN ? INT16_MIN : tex > INT16_MAX ? INT16_MAX : tex);
                out->col[i] = (uint8_t)col;
            }
            mesh->tris[(mesh->ntris + t) * 3 + k] = mesh->nverts + t * 3 + k;
        }
    }
    mesh->nverts = (uint32_t)nverts;
    mesh->ntris = (uint32_t)ntris;
    mesh->nbatches++;
    return true;
}

void nw_vif_free(NwVifMesh *mesh)
{
    SDL_free(mesh->verts);
    SDL_free(mesh->tris);
    memset(mesh, 0, sizeof(*mesh));
}

/* A face-on batch as NuCvtDmaFaceOn and NuCvtDmaFaceY write it: the count x3 at VU 1, then under STCYCL 3,1 each
   sprite's point (V3-32) at VU 4 + 3i, size (V2-32) at 5 + 3i and colour (V4-8) at 6 + 3i, kicked at MSCAL 0x304
   or 0x306. The GIF tags they also upload are the programs' own. */
static bool nw_vif_sprite_batch(NwVifSprites *s, const uint8_t *st, size_t n)
{
    uint32_t count = 0, filled = 0;
    unsigned cl = 1, wl = 1;
    bool kicked = false;
    NwVifSprite *b = NULL;
    for (size_t p = 0; p < n;) {
        uint32_t w = nw_rd32(st + p);
        unsigned cmd = (w >> 24) & 0x7F, imm = w & 0xFFFF, addr = w & 0x3FF;
        size_t at = p;
        p += 4;
        if (cmd < 0x60) {
            if (cmd == 0x01) {
                cl = imm & 0xFF;
                wl = imm >> 8;
            } else if (cmd == 0x14) {
                if (imm != NW_VIF_MSCAL_FACEON && imm != NW_VIF_MSCAL_FACEY) {
                    return SDL_SetError("vif: face-on batch kicked at 0x%x", imm);
                }
                if (kicked && s->facey != (imm == NW_VIF_MSCAL_FACEY)) {
                    return SDL_SetError("vif: faceon and facey batches in one geom");
                }
                if (!b || filled != 7) {
                    return SDL_SetError("vif: face-on batch kicked before its points, sizes and colours");
                }
                s->facey = imm == NW_VIF_MSCAL_FACEY;
                s->count += count;
                kicked = true;
                b = NULL;
                filled = 0;
            } else if (cmd != 0x00) {
                return SDL_SetError("vif: VIF command 0x%02x at 0x%zx in a face-on geom", cmd, at);
            }
            continue;
        }
        unsigned vn = (cmd >> 2) & 3, vl = cmd & 3, num = (w >> 16) & 0xFF ? (w >> 16) & 0xFF : 256;
        size_t bytes = ((size_t)num * (vn + 1) * (32u >> vl) / 8 + 3) & ~(size_t)3;
        if (bytes > n - p) {
            return SDL_SetError("vif: unpack at 0x%zx runs past the end", at);
        }
        const uint8_t *d = st + p;
        p += bytes;
        if (addr == 1 && vn == 3 && vl == 1) {
            uint16_t x3;
            memcpy(&x3, d, 2);
            count = x3 / 3u;
            if (!nw_vif_reserve((void **)&s->sprites, &s->cap, (uint64_t)s->count + count, sizeof(*s->sprites))) {
                return false;
            }
            b = s->sprites + s->count;
            filled = 0;
            continue;
        }
        if (addr < 4 || addr > 6) {
            continue; /* the GIF tags at VU 0 and 0x56 */
        }
        if (!b || cl != 3 || wl != 1 || num != count) {
            return SDL_SetError("vif: face-on unpack at 0x%zx outside a batch of %u", at, count);
        }
        for (uint32_t i = 0; i < count; i++) {
            if (addr == 4 && vn == 2 && vl == 0) {
                memcpy(b[i].pos, d + i * 12, 12);
            } else if (addr == 5 && vn == 1 && vl == 0) {
                memcpy(b[i].size, d + i * 8, 8);
            } else if (addr == 6 && vn == 3 && vl == 2) {
                memcpy(b[i].col, d + i * 4, 4);
            } else {
                return SDL_SetError("vif: face-on unpack 0x%02x to VU %u", cmd, addr);
            }
        }
        filled |= 1u << (addr - 4);
    }
    if (b) {
        return SDL_SetError("vif: face-on batch not kicked");
    }
    return true;
}

bool nw_vif_sprites(NwVifSprites *s, const void *geom, size_t size)
{
    memset(s, 0, sizeof(*s));
    size_t n = 0;
    uint8_t *st = nw_vif_chain(geom, size, &n);
    bool ok = st && nw_vif_sprite_batch(s, st, n);
    SDL_free(st);
    if (!ok) {
        nw_vif_sprites_free(s);
    }
    return ok;
}

void nw_vif_sprites_free(NwVifSprites *s)
{
    SDL_free(s->sprites);
    memset(s, 0, sizeof(*s));
}

#define NW_VIF_MSCAL_SKIN 0x302   /* the package's jump to skin (tri) or skinxuv (xformuv) */
#define NW_VIF_MSCAL_SKINMOD 0x0C /* the resident blend-shape pass */
#define NW_VIF_SKIN_TOP 0xD3      /* a batch's input up to its strip GIF tag at TOP + 0xD2 */
#define NW_VIF_SKIN_MOD 0x38      /* skinmod's header (shape, end), its deltas from 0x39 */
#define NW_VIF_SKIN_MODEND 0x63

typedef struct NwSkinDelta {
    float d[3];
    uint32_t shape, slot;
} NwSkinDelta;

/* VU1 as a skin list drives it: the open batch's TOP-relative input, skinmod's block, the group's matrix slots. */
typedef struct NwSkinVu {
    uint32_t top[NW_VIF_SKIN_TOP][4];
    uint32_t mod[NW_VIF_SKIN_MODEND - NW_VIF_SKIN_MOD][4];
    bool topset[NW_VIF_SKIN_TOP], modset[NW_VIF_SKIN_MODEND - NW_VIF_SKIN_MOD];
    unsigned cl, wl;
    uint8_t joints[8];
    unsigned nslots;
    bool kicked;   /* a batch of the list ran: MSCNT may continue the program */
    bool skinmod;  /* MSCAL 0x0C ran: the MSCNT after it only ends skinmod */
    bool blended;  /* the open batch went through skinmod, so it must be kicked at 0x302 */
    NwSkinDelta *pending;
    uint32_t npending, pendcap;
} NwSkinVu;

/* An unpack under STCYCL CL >= WL, FLG ones into the batch, absolute ones into skinmod's block. */
static bool nw_skin_unpack(NwSkinVu *vu, uint32_t w, const uint8_t *d, size_t at)
{
    unsigned cmd = (w >> 24) & 0x7F, vn = (cmd >> 2) & 3, vl = cmd & 3, addr = w & 0x3FF;
    unsigned n = (w >> 16) & 0xFF ? (w >> 16) & 0xFF : 256, bits = 32u >> vl;
    bool usn = (w & 0x4000) != 0, top = (w & 0x8000) != 0;
    if (vl == 3 || vu->wl == 0 || vu->wl > vu->cl) {
        return SDL_SetError("vif: skin unpack 0x%02x under STCYCL %u,%u at 0x%zx", cmd, vu->cl, vu->wl, at);
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned q = addr + (i / vu->wl) * vu->cl + i % vu->wl;
        uint32_t v[4] = { 0, 0, 0, 0 };
        for (unsigned k = 0; k <= vn; k++) {
            const uint8_t *e = d + ((size_t)i * (vn + 1) + k) * bits / 8;
            if (bits == 32) {
                v[k] = nw_rd32(e);
            } else if (bits == 16) {
                uint16_t h;
                memcpy(&h, e, 2);
                v[k] = usn ? h : (uint32_t)(int32_t)(int16_t)h;
            } else {
                v[k] = usn ? *e : (uint32_t)(int32_t)(int8_t)*e;
            }
        }
        if (vn == 0) {
            v[1] = v[2] = v[3] = v[0];
        }
        uint32_t *dst;
        if (top && q < NW_VIF_SKIN_TOP) {
            dst = vu->top[q];
            vu->topset[q] = true;
        } else if (!top && q >= NW_VIF_SKIN_MOD && q < NW_VIF_SKIN_MODEND) {
            dst = vu->mod[q - NW_VIF_SKIN_MOD];
            vu->modset[q - NW_VIF_SKIN_MOD] = true;
        } else {
            return SDL_SetError("vif: skin unpack to VU %s0x%x at 0x%zx", top ? "TOP + " : "", q, at);
        }
        memcpy(dst, v, (vn + 1) * 4);
        if (vn == 0) {
            memcpy(dst, v, 16);
        }
    }
    return true;
}

/* skinmod's block: one shape's deltas, each aimed at a vertex's position (TOP + 3 + 4i), kept until the kick. */
static bool nw_skin_mod(NwSkinVu *vu)
{
    int32_t shape = (int32_t)vu->mod[0][0], end = (int32_t)vu->mod[0][1], count = end - 0x3A;
    if (!vu->modset[0] || shape < 0 || count < 1 || count > NW_VIF_SKIN_MODEND - 0x39) {
        return SDL_SetError("vif: skinmod header (%d, %d)", shape, end);
    }
    if (!nw_vif_reserve((void **)&vu->pending, &vu->pendcap, (uint64_t)vu->npending + count, sizeof(NwSkinDelta))) {
        return false;
    }
    for (int32_t e = 0; e < count; e++) {
        const uint32_t *q = vu->mod[1 + e];
        if (!vu->modset[1 + e] || q[3] < 3 || (q[3] - 3) % 4) {
            return SDL_SetError("vif: skinmod delta %d aimed at TOP + 0x%x", e, q[3]);
        }
        NwSkinDelta *p = &vu->pending[vu->npending++];
        p->d[0] = nw_f32(q[0]);
        p->d[1] = nw_f32(q[1]);
        p->d[2] = nw_f32(q[2]);
        p->shape = (uint32_t)shape;
        p->slot = (q[3] - 3) / 4;
    }
    memset(vu->modset, 0, sizeof(vu->modset));
    return true;
}

/* The kick: skin.vsm's vertex loop. A dupe (its first word's low byte d) is output vertex d - 1 again; ADC (the low
   bits of the second word's w) skips a strip triangle; each weight's low 3 bits name its matrix slot. */
static bool nw_skin_batch(NwVifSkin *s, NwSkinVu *vu)
{
    int32_t hdr = (int32_t)vu->top[2][0];
    uint32_t n = hdr > 0 && hdr % 4 == 0 ? (uint32_t)hdr / 4 : 0, nloop = vu->top[0xD2][0] & 0x7FFF;
    if (!vu->topset[2] || n == 0 || n > 42) {
        return SDL_SetError("vif: skin batch header %d", hdr);
    }
    if (!vu->topset[0xD2] || nloop != n) {
        return SDL_SetError("vif: skin batch of %u vertices under a GIF tag of NLOOP %u", n, nloop);
    }
    if (!nw_vif_reserve((void **)&s->verts, &s->vcap, (uint64_t)s->nverts + n, sizeof(*s->verts)) ||
        !nw_vif_reserve((void **)&s->tris, &s->tcap, (uint64_t)s->ntris + n, 3 * sizeof(uint32_t)) ||
        !nw_vif_reserve((void **)&s->deltas, &s->dcap, (uint64_t)s->ndeltas + vu->npending, sizeof(*s->deltas))) {
        return false;
    }
    uint32_t map[42];
    bool adc[42], dupe[42];
    uint32_t nverts = s->nverts;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t *q0 = vu->top[3 + 4 * i], *q1 = vu->top[4 + 4 * i];
        uint32_t d = q0[3] & 0xFF, c0 = q1[1] & 7, c1 = q1[2] & 7;
        if (!vu->topset[3 + 4 * i] || !vu->topset[4 + 4 * i]) {
            return SDL_SetError("vif: skin vertex %u not uploaded", i);
        }
        adc[i] = (q1[3] & 7) != 0;
        dupe[i] = d != 0;
        if (d) {
            if (d - 1 >= i) {
                return SDL_SetError("vif: skin vertex %u a dupe of vertex %u", i, d - 1);
            }
            map[i] = map[d - 1];
            s->ndupes++;
            continue;
        }
        if (c0 == 0 || c0 > 4 || c1 > 4 || (c1 && c0 != 4)) {
            return SDL_SetError("vif: skin vertex %u with %u + %u weights", i, c0, c1);
        }
        if (!vu->topset[5 + 4 * i] || (c1 && !vu->topset[6 + 4 * i]) || (q1[0] & 7) >= vu->nslots) {
            return SDL_SetError("vif: skin vertex %u without its weights or normal matrix", i);
        }
        NwVifSkinVertex *out = &s->verts[nverts];
        memset(out, 0, sizeof(*out));
        for (int k = 0; k < 3; k++) {
            out->pos[k] = nw_f32(q0[k]);
        }
        out->nrm[0] = nw_f32(q0[3]);
        out->nrm[1] = nw_f32(q1[2]);
        out->nrm[2] = nw_f32(q1[3]);
        out->uv[0] = nw_f32(q1[0]);
        out->uv[1] = nw_f32(q1[1]);
        out->nrmjoint = vu->joints[q1[0] & 7];
        for (uint32_t k = 0; k < c0 + c1; k++) {
            uint32_t bits = vu->top[5 + 4 * i + k / 4][k % 4], slot = bits & 7;
            double fixed = trunc((double)nw_f32(bits) * 32768.0);
            if (slot >= vu->nslots || !(fixed >= 0.0 && fixed <= 65535.0)) {
                return SDL_SetError("vif: skin vertex %u weight %u (slot %u of %u)", i, k, slot, vu->nslots);
            }
            out->weight[k] = (uint16_t)fixed;
            out->joint[k] = vu->joints[slot];
        }
        map[i] = nverts++;
    }
    for (uint32_t k = 0; k < vu->npending; k++) {
        if (vu->pending[k].slot >= n || dupe[vu->pending[k].slot]) {
            return SDL_SetError("vif: skinmod delta for vertex %u of a batch of %u", vu->pending[k].slot, n);
        }
    }
    /* Each vertex's deltas together, in shape order as skinmod adds them. */
    for (uint32_t i = 0; i < n; i++) {
        if (dupe[i]) {
            continue;
        }
        NwVifSkinVertex *v = &s->verts[map[i]];
        v->delta = s->ndeltas;
        for (uint32_t k = 0; k < vu->npending; k++) {
            const NwSkinDelta *p = &vu->pending[k];
            if (p->slot == i) {
                if (v->ndeltas == 0xFF) {
                    return SDL_SetError("vif: skin vertex %u with too many deltas", i);
                }
                memcpy(s->deltas[s->ndeltas], p->d, sizeof(p->d));
                s->deltas[s->ndeltas++][3] = (float)p->shape;
                v->ndeltas++;
            }
        }
    }
    uint32_t *t = s->tris + 3 * (size_t)s->ntris;
    for (uint32_t i = 2; i < n; i++) {
        if (adc[i]) {
            continue;
        }
        t[0] = map[i - 2];
        t[1] = i & 1 ? map[i] : map[i - 1];
        t[2] = i & 1 ? map[i - 1] : map[i];
        t += 3;
        s->ntris++;
    }
    for (uint32_t i = s->nverts; i < nverts; i++) {
        for (int k = 0; k < 8; k++) {
            s->maxjoint = s->verts[i].joint[k] > s->maxjoint ? s->verts[i].joint[k] : s->maxjoint;
        }
        s->maxjoint = s->verts[i].nrmjoint > s->maxjoint ? s->verts[i].nrmjoint : s->maxjoint;
    }
    s->nverts = nverts;
    s->nbatches++;
    vu->npending = 0;
    vu->blended = false;
    memset(vu->topset, 0, sizeof(vu->topset));
    return true;
}

/* A group's batches: the VIF stream of the RET tag a CALL reaches. */
static bool nw_skin_stream(NwVifSkin *s, NwSkinVu *vu, const uint8_t *st, size_t n)
{
    for (size_t p = 0; p < n;) {
        uint32_t w = nw_rd32(st + p);
        unsigned cmd = (w >> 24) & 0x7F, imm = w & 0xFFFF;
        size_t at = p;
        p += 4;
        if (cmd >= 0x60) {
            unsigned vn = (cmd >> 2) & 3, vl = cmd & 3, num = (w >> 16) & 0xFF ? (w >> 16) & 0xFF : 256;
            size_t bytes = ((size_t)num * (vn + 1) * (32u >> (vl & 3)) / 8 + 3) & ~(size_t)3;
            if (cmd & 0x10) {
                return SDL_SetError("vif: masked unpack at 0x%zx", at);
            }
            if (bytes > n - p) {
                return SDL_SetError("vif: unpack at 0x%zx runs past the end", at);
            }
            if (!nw_skin_unpack(vu, w, st + p, at)) {
                return false;
            }
            p += bytes;
            continue;
        }
        if (vu->skinmod && cmd != 0x17) {
            return SDL_SetError("vif: skinmod not ended by MSCNT at 0x%zx", at);
        }
        switch (cmd) {
        case 0x00: /* NOP */
        case 0x10: /* FLUSHE */
        case 0x11: /* FLUSH */
        case 0x13: /* FLUSHA */
            break;
        case 0x01: /* STCYCL: 1,1 or 4,2; 5,3 (vertex colours) has no skin program */
            vu->cl = imm & 0xFF;
            vu->wl = imm >> 8;
            if ((vu->cl != 1 || vu->wl != 1) && (vu->cl != 4 || vu->wl != 2)) {
                return SDL_SetError("vif: skin STCYCL %u,%u at 0x%zx", vu->cl, vu->wl, at);
            }
            break;
        case 0x14: /* MSCAL */
            if (imm == NW_VIF_MSCAL_SKINMOD) {
                if (!nw_skin_mod(vu)) {
                    return false;
                }
                vu->skinmod = vu->blended = true;
                break;
            }
            if (imm != NW_VIF_MSCAL_SKIN) {
                return SDL_SetError("vif: skin batch kicked at 0x%x", imm);
            }
            if (!nw_skin_batch(s, vu)) {
                return false;
            }
            vu->kicked = true;
            break;
        case 0x17: /* MSCNT */
            if (vu->skinmod) {
                vu->skinmod = false;
                break;
            }
            if (!vu->kicked || vu->blended) {
                return SDL_SetError("vif: %s skin batch continued by MSCNT", vu->blended ? "blended" : "first");
            }
            if (!nw_skin_batch(s, vu)) {
                return false;
            }
            break;
        default:
            return SDL_SetError("vif: VIF command 0x%02x at 0x%zx in a skin stream", cmd, at);
        }
    }
    if (vu->skinmod || vu->npending) {
        return SDL_SetError("vif: skin batch not kicked");
    }
    return true;
}

/* The list: per group of at most 8 matrices a CNT (STCYCL 8,1), a REF per slot (joint << 6 into skin_mtx_buffer,
   unpacked to VU 0x18 + slot) and a CALL of the group's batches; RET ends it. */
static bool nw_skin_list(NwVifSkin *s, NwSkinVu *vu, const uint8_t *l, size_t size)
{
    for (size_t off = 0;; off += 16) {
        if (size < 16 || off > size - 16) {
            return SDL_SetError("vif: skin DMA tag at 0x%zx outside the geom", off);
        }
        uint32_t lo = nw_rd32(l + off), addr = nw_rd32(l + off + 4), vif0 = nw_rd32(l + off + 8);
        uint32_t vif1 = nw_rd32(l + off + 12), qwc = lo & 0xFFFF, id = (lo >> 28) & 7;
        if (id == 6) {
            return true;
        }
        if (id == 1) {
            if (qwc != 0 || vif0 != 0x01000108) {
                return SDL_SetError("vif: skin group tag at 0x%zx", off);
            }
            vu->nslots = 0;
        } else if (id == 3) {
            unsigned slot = (vif1 & 0x3FF) - 0x18;
            if (qwc != 4 || (vif1 & 0xFFFFFC00u) != 0x6C040000u || slot != vu->nslots || slot >= 8 || (addr & 63) ||
                addr >> 6 > 0xFF) {
                return SDL_SetError("vif: skin matrix tag at 0x%zx", off);
            }
            vu->joints[vu->nslots++] = (uint8_t)(addr >> 6);
        } else if (id == 5) {
            if (vu->nslots == 0 || (addr & 15) || size < 16 || addr > size - 16) {
                return SDL_SetError("vif: skin CALL at 0x%zx to 0x%x", off, addr);
            }
            uint32_t rlo = nw_rd32(l + addr);
            size_t rqwc = rlo & 0xFFFF;
            if (((rlo >> 28) & 7) != 6 || rqwc * 16 > size - addr - 16) {
                return SDL_SetError("vif: skin CALL at 0x%zx reaches no RET block", off);
            }
            uint8_t *st = SDL_malloc(8 + rqwc * 16);
            if (!st) {
                return false;
            }
            memcpy(st, l + addr + 8, 8);
            memcpy(st + 8, l + addr + 16, rqwc * 16);
            bool ok = nw_skin_stream(s, vu, st, 8 + rqwc * 16);
            SDL_free(st);
            if (!ok) {
                return false;
            }
        } else {
            return SDL_SetError("vif: skin DMA tag id %u at 0x%zx", id, off);
        }
    }
}

bool nw_vif_skin(NwVifSkin *skin, const void *list, size_t size)
{
    memset(skin, 0, sizeof(*skin));
    NwSkinVu *vu = SDL_calloc(1, sizeof(*vu));
    bool ok = vu && nw_skin_list(skin, vu, list, size);
    if (vu) {
        SDL_free(vu->pending);
    }
    SDL_free(vu);
    if (!ok) {
        nw_vif_skin_free(skin);
    }
    return ok;
}

void nw_vif_skin_free(NwVifSkin *skin)
{
    SDL_free(skin->verts);
    SDL_free(skin->tris);
    SDL_free(skin->deltas);
    memset(skin, 0, sizeof(*skin));
}
