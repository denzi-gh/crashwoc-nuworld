#include "gfx/gsc.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/vif.h"
#include "vfs/sha256.h"

#define NW_GSC_VERSION 6 /* scene_version */

/* The blocks NuGScnLoadFix reads; DbgBlkType calls anything else "unknown". */
static const uint32_t nw_gsc_known[] = {
    NW_GSC_ID('T', 'S', 'T', '2'), NW_GSC_ID('T', 'S', 'T', '0'), NW_GSC_ID('M', 'S', '0', '2'),
    NW_GSC_ID('M', 'S', '0', '0'), NW_GSC_ID('O', 'B', 'J', '0'), NW_GSC_ID('I', 'N', 'S', 'T'),
    NW_GSC_ID('N', 'T', 'B', 'L'), NW_GSC_ID('S', 'P', 'E', 'C'), NW_GSC_ID('S', 'S', 'T', '0'),
    NW_GSC_ID('I', 'A', 'B', 'L'), NW_GSC_ID('A', 'L', 'I', 'B'), NW_GSC_ID('I', 'N', 'I', 'D'),
    NW_GSC_ID('T', 'A', 'S', '0'),
};

static uint32_t nw_rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* A bounded reader over one block; reading past its end clears ok. */
typedef struct NwGscCur {
    const uint8_t *base;
    uint32_t pos, end;
    bool ok;
} NwGscCur;

static uint32_t nw_cur_u32(NwGscCur *c)
{
    if (c->end - c->pos < 4) {
        c->ok = false;
        c->pos = c->end;
        return 0;
    }
    c->pos += 4;
    return nw_rd32(c->base + c->pos - 4);
}

static void nw_cur_skip(NwGscCur *c, uint32_t n)
{
    if (c->end - c->pos < n) {
        c->ok = false;
        c->pos = c->end;
    } else {
        c->pos += n;
    }
}

static const NwGscChunk *nw_gsc_last(const NwGsc *g, uint32_t a, uint32_t b)
{
    for (uint32_t i = g->nchunks; i-- > 0;) {
        if (g->chunks[i].id == a || g->chunks[i].id == b) {
            return &g->chunks[i];
        }
    }
    return NULL;
}

/* {u32 count, pad, count records} */
static bool nw_gsc_table(const NwGsc *g, const NwGscChunk *ch, uint32_t record, uint32_t *offset, uint32_t *count)
{
    if (!ch) {
        return true;
    }
    uint32_t n = ch->size >= 8 ? nw_rd32(g->data + ch->offset) : UINT32_MAX;
    if (n > (ch->size - 8) / record) {
        return SDL_SetError("gsc: block %.4s of %u bytes has %u records", (const char *)&ch->id, ch->size, n);
    }
    *offset = ch->offset + 8;
    *count = n;
    return true;
}

static bool nw_gsc_blob(NwGsc *g, uint32_t *cap, NwGscCur *c, uint32_t mtl, uint32_t size, bool faceon)
{
    if (mtl >= g->nmtls) {
        return SDL_SetError("gsc: OBJ0 geom of material %u of %u", mtl, g->nmtls);
    }
    if (g->nblobs == *cap) {
        uint32_t n = *cap ? *cap * 2 : 256;
        NwGscBlob *b = SDL_realloc(g->blobs, n * sizeof(*b));
        if (!b) {
            return false;
        }
        g->blobs = b;
        *cap = n;
    }
    uint32_t attrib[2];
    memcpy(attrib, g->data + g->mtls + mtl * NW_GSC_MTL_SIZE + 0x16C, 8);
    g->blobs[g->nblobs++] = (NwGscBlob){ c->pos, size, mtl, faceon, !faceon && NW_GSC_ATTRIB_OT(attrib) };
    nw_cur_skip(c, size);
    return true;
}

/* ReadTextureSet's records: size, skip, then skip bytes (8 when 0) before the native record. */
static bool nw_gsc_texset(NwGsc *g, const NwGscChunk *ch)
{
    NwGscCur c = { g->data, ch->offset, ch->offset + ch->size, true };
    uint32_t n = nw_cur_u32(&c);
    nw_cur_skip(&c, 4);
    if (!c.ok || n > ch->size / 16) {
        return SDL_SetError("gsc: %.4s of %u bytes has %u textures", (const char *)&ch->id, ch->size, n);
    }
    g->textures = SDL_calloc(n ? n : 1, sizeof(*g->textures));
    if (!g->textures) {
        return false;
    }
    for (uint32_t i = 0; i < n && c.ok; i++) {
        uint32_t size = nw_cur_u32(&c), skip = nw_cur_u32(&c);
        nw_cur_skip(&c, skip ? skip : 8);
        g->textures[i] = (NwGscTexture){ c.pos, size };
        nw_cur_skip(&c, size);
    }
    if (!c.ok) {
        return SDL_SetError("gsc: %.4s runs past its block", (const char *)&ch->id);
    }
    g->ntextures = n;
    return true;
}

/* ReadObjSet's descriptor stream, every NuFileReadInt and NuMemFileSeek in its order. */
static bool nw_gsc_objset(NwGsc *g, const NwGscChunk *ch)
{
    NwGscCur c = { g->data, ch->offset, ch->offset + ch->size, true };
    uint32_t ngobjs = nw_cur_u32(&c), colourref = nw_cur_u32(&c), cap = 0;
    if (!c.ok || ngobjs > ch->size / 16) {
        return SDL_SetError("gsc: OBJ0 of %u bytes has %u objects", ch->size, ngobjs);
    }
    g->objects = SDL_calloc(ngobjs ? ngobjs : 1, sizeof(*g->objects));
    if (!g->objects) {
        return false;
    }
    g->nobjects = ngobjs;
    for (uint32_t i = 0; i < ngobjs && c.ok; i++) {
        uint32_t nsub = nw_cur_u32(&c);
        nw_cur_skip(&c, 12);
        g->objects[i].first = g->nblobs;
        for (uint32_t j = 0; j < nsub && c.ok; j++) {
            uint32_t type = nw_cur_u32(&c), count = nw_cur_u32(&c);
            if (type > 1) {
                return SDL_SetError("gsc: OBJ0 object %u has a sub-object of type %u", i, type);
            }
            nw_cur_skip(&c, type == 0 ? 24 : 8); /* origin, tricount, 2 ints | 2 ints */
            for (uint32_t k = 0; k < count && c.ok; k++) {
                uint32_t mtl, size;
                if (type == 0) {
                    nw_cur_skip(&c, 4);
                    uint32_t skip = nw_cur_u32(&c);
                    mtl = nw_cur_u32(&c);
                    nw_cur_skip(&c, skip);
                    size = nw_cur_u32(&c);
                    if (size & 15) {
                        return SDL_SetError("gsc: OBJ0 object %u has a geom of %u bytes", i, size);
                    }
                } else {
                    mtl = nw_cur_u32(&c);
                    nw_cur_skip(&c, 8); /* faceon_type, faceon_radius */
                    size = nw_cur_u32(&c);
                }
                if (c.ok && !nw_gsc_blob(g, &cap, &c, mtl, size, type == 1)) {
                    return false;
                }
            }
            nw_cur_skip(&c, 32); /* min, max, 2 ints */
        }
        if (colourref == 1) {
            uint32_t blend = nw_cur_u32(&c);
            nw_cur_skip(&c, 12);
            nw_cur_skip(&c, blend);
        }
        g->objects[i].count = g->nblobs - g->objects[i].first;
    }
    if (!c.ok) {
        return SDL_SetError("gsc: OBJ0 runs past its block");
    }
    return true;
}

static bool nw_gsc_read(NwGsc *g)
{
    if (g->size < 16 || memcmp(g->data, "NU20", 4) != 0) {
        return SDL_SetError("gsc: not an NU20 scene");
    }
    if (nw_rd32(g->data + 8) != NW_GSC_VERSION) {
        return SDL_SetError("gsc: scene version %u", nw_rd32(g->data + 8));
    }
    uint32_t cap = 0;
    for (size_t off = 16; off < g->size;) {
        if (g->size - off < 8) {
            g->truncated = true;
            break;
        }
        uint32_t id = nw_rd32(g->data + off);
        int32_t s = (int32_t)nw_rd32(g->data + off + 4);
        uint32_t size = s < 0 ? 0u - (uint32_t)s : (uint32_t)s;
        if (size < 8) {
            return SDL_SetError("gsc: block at 0x%zx of %d bytes", off, s);
        }
        if (size > g->size - off) {
            g->truncated = true;
            break;
        }
        if (g->nchunks == cap) {
            cap = cap ? cap * 2 : 16;
            NwGscChunk *c = SDL_realloc(g->chunks, cap * sizeof(*c));
            if (!c) {
                return false;
            }
            g->chunks = c;
        }
        g->chunks[g->nchunks++] = (NwGscChunk){ id, (uint32_t)off + 8, size - 8 };
        bool known = false;
        for (size_t i = 0; i < SDL_arraysize(nw_gsc_known); i++) {
            known = known || id == nw_gsc_known[i];
        }
        g->unknown += !known;
        off += size;
    }
    const NwGscChunk *objset = nw_gsc_last(g, NW_GSC_ID('O', 'B', 'J', '0'), 0);
    const NwGscChunk *texset = nw_gsc_last(g, NW_GSC_ID('T', 'S', 'T', '0'), NW_GSC_ID('T', 'S', 'T', '2'));
    return (!texset || nw_gsc_texset(g, texset)) &&
           nw_gsc_table(g, nw_gsc_last(g, NW_GSC_ID('M', 'S', '0', '0'), NW_GSC_ID('M', 'S', '0', '2')),
                        NW_GSC_MTL_SIZE, &g->mtls, &g->nmtls) &&
           nw_gsc_table(g, nw_gsc_last(g, NW_GSC_ID('I', 'N', 'S', 'T'), 0), NW_GSC_INST_SIZE, &g->insts,
                        &g->ninsts) &&
           (!objset || nw_gsc_objset(g, objset));
}

bool nw_gsc_open(NwGsc *g, const void *data, size_t size)
{
    memset(g, 0, sizeof(*g));
    g->data = data;
    g->size = size;
    if (size > UINT32_MAX) {
        return SDL_SetError("gsc: %zu bytes", size);
    }
    if (!nw_gsc_read(g)) {
        nw_gsc_close(g);
        return false;
    }
    return true;
}

void nw_gsc_close(NwGsc *g)
{
    SDL_free(g->chunks);
    SDL_free(g->objects);
    SDL_free(g->blobs);
    SDL_free(g->textures);
    memset(g, 0, sizeof(*g));
}

NwGscMtl nw_gsc_mtl(const NwGsc *g, uint32_t index)
{
    const uint8_t *p = g->data + g->mtls + index * NW_GSC_MTL_SIZE;
    NwGscMtl m;
    memcpy(m.attrib, p + 0x16C, 8);
    memcpy(m.diffuse, p + 0x180, 12);
    memcpy(&m.alpha, p + 0x1A0, 4);
    memcpy(&m.tid, p + 0x1A4, 4);
    return m;
}

NwGscInst nw_gsc_inst(const NwGsc *g, uint32_t index)
{
    const uint8_t *p = g->data + g->insts + index * NW_GSC_INST_SIZE;
    NwGscInst inst;
    memcpy(inst.mtx, p, 64);
    memcpy(&inst.objid, p + 0x40, 4);
    memcpy(&inst.flags, p + 0x44, 4);
    return inst;
}

bool nw_gsc_texture(const NwGsc *g, uint32_t index, NwTex *t)
{
    if (index >= g->ntextures) {
        memset(t, 0, sizeof(*t));
        return SDL_SetError("gsc: texture %u of %u", index, g->ntextures);
    }
    return nw_tex_decode(t, g->data + g->textures[index].offset, g->textures[index].size);
}

const char *nw_gsc_mtl_package(const NwGscMtl *m)
{
    if (NW_GSC_ATTRIB_FX(m->attrib) == 1) {
        return "vu_load_xformuv_mpg";
    }
    switch (NW_GSC_ATTRIB_LIGHTING(m->attrib)) {
    case 3:
        return "vu_load_trica_mpg";
    case 2:
        return "vu_load_tricu_mpg";
    case 1:
        return "vu_load_xformuv_mpg";
    default:
        return NW_GSC_ATTRIB_COLOUR(m->attrib) ? "vu_load_tric_mpg" : "vu_load_tri_mpg";
    }
}

void nw_gsc_obj_line(uint32_t id, const NwVifMesh *m, char *line, size_t size)
{
    NwSha256 sha;
    uint8_t vtx[NW_SHA256_SIZE], idx[NW_SHA256_SIZE];
    char vhex[NW_SHA256_HEX], ihex[NW_SHA256_HEX];
    nw_sha256_init(&sha);
    for (uint32_t i = 0; i < m->nverts; i++) {
        const NwVifVertex *v = &m->verts[i];
        float f[5] = { v->pos[0], v->pos[1], v->pos[2], (float)v->tex[0] / 4096.0f, (float)v->tex[1] / 4096.0f };
        nw_sha256_update(&sha, f, sizeof(f));
        nw_sha256_update(&sha, v->col, 4);
    }
    nw_sha256_final(&sha, vtx);
    nw_sha256(m->tris, (size_t)m->ntris * 12, idx);
    nw_sha256_hex(vtx, vhex);
    nw_sha256_hex(idx, ihex);
    SDL_snprintf(line, size, "OBJ %u batches=%u verts=%u tris=%u vtx=%s idx=%s", id, m->nbatches, m->nverts, m->ntris,
                 vhex, ihex);
}

void nw_gsc_tex_line(uint32_t id, const NwTex *t, char *line, size_t size)
{
    const NwTexLevel *l = &t->levels[0];
    uint8_t row[1024 * 4], sum[NW_SHA256_SIZE];
    char hex[NW_SHA256_HEX];
    NwSha256 sha;
    nw_sha256_init(&sha);
    for (uint32_t y = 0; y < l->height; y++) {
        nw_tex_rgba(t, y, row);
        nw_sha256_update(&sha, row, (size_t)l->width * 4);
    }
    nw_sha256_final(&sha, sum);
    nw_sha256_hex(sum, hex);
    SDL_snprintf(line, size, "TEX %u %ux%u type=%u version=%u levels=%u rgba=%s", id, l->width, l->height, t->type,
                 t->version, t->nlevels, hex);
}

bool nw_gsc_report(const NwGsc *g, const char *name, FILE *out, NwGscTotal *total)
{
    NwVifMesh mesh = { 0 };
    uint32_t batches = 0, verts = 0, tris = 0;
    bool ok = true;
    for (uint32_t o = 0; o < g->nobjects && ok; o++) {
        const NwGscObject *obj = &g->objects[o];
        mesh.nverts = mesh.ntris = mesh.nbatches = 0;
        for (uint32_t i = obj->first; i < obj->first + obj->count && ok; i++) {
            const NwGscBlob *b = &g->blobs[i];
            if (!b->ot && !b->faceon && !nw_vif_decode(&mesh, g->data + b->offset, b->size)) {
                char why[256];
                SDL_strlcpy(why, SDL_GetError(), sizeof(why));
                ok = SDL_SetError("object %u geom at 0x%x: %s", o, b->offset, why);
            }
        }
        char line[256];
        nw_gsc_obj_line(o, &mesh, line, sizeof(line));
        fprintf(out, "%s\n", line);
        batches += mesh.nbatches;
        verts += mesh.nverts;
        tris += mesh.ntris;
    }
    for (uint32_t i = 0; i < g->ntextures && ok; i++) {
        NwTex t;
        if (!nw_gsc_texture(g, i, &t)) {
            char why[256];
            SDL_strlcpy(why, SDL_GetError(), sizeof(why));
            ok = SDL_SetError("texture %u at 0x%x: %s", i, g->textures[i].offset, why);
            break;
        }
        char line[256];
        nw_gsc_tex_line(i, &t, line, sizeof(line));
        fprintf(out, "%s\n", line);
        nw_tex_free(&t);
    }
    total->files++;
    total->failed += !ok;
    total->unknown += g->unknown;
    total->truncated += g->truncated;
    total->objects += g->nobjects;
    total->batches += batches;
    total->verts += verts;
    total->tris += tris;
    total->textures += g->ntextures;
    fprintf(out, "GSC %s chunks=%u unknown=%u truncated=%d objects=%u batches=%u verts=%u tris=%u textures=%u\n",
            name, g->nchunks, g->unknown, g->truncated, g->nobjects, batches, verts, tris, g->ntextures);
    nw_vif_free(&mesh);
    return ok;
}

void nw_gsc_report_total(const NwGscTotal *t, FILE *out)
{
    fprintf(out,
            "TOTAL files=%u failed=%u unknown=%u truncated=%u objects=%llu batches=%llu verts=%llu tris=%llu "
            "textures=%llu\n",
            t->files, t->failed, t->unknown, t->truncated, (unsigned long long)t->objects,
            (unsigned long long)t->batches, (unsigned long long)t->verts, (unsigned long long)t->tris,
            (unsigned long long)t->textures);
}
