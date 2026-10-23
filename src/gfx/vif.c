#include "gfx/vif.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#define NW_VIF_MSCAL_TRI 0x300 /* the package's triangle program at VU 0x1800 */
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

void nw_vif_free(NwVifMesh *mesh)
{
    SDL_free(mesh->verts);
    SDL_free(mesh->tris);
    memset(mesh, 0, sizeof(*mesh));
}
