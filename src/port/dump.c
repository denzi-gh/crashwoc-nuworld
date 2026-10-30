#include "port/dump.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "data/convert.h"
#include "data/image.h"
#include "game/shadcom.h"
#include "gamelib/gcutscn.h"
#include "nucore/nucoretypes.h"
#include "nw_convert.h"
#include "port/heap.h"
#include "port/trace.h"

#define NW_DUMP_MAX 512
#define NW_POKE_MAX 64
#define NW_DUMP_MARK 0xF0000000u   /* a pointer slot that a reloc fills */
#define NW_DUMP_OPAQUE 0x80000000u /* a reloc target: a host pointer to nothing the dump knows */
#define NW_DUMP_NONE 0xFFFFFFFFu
#define NW_DUMP_MAGIC "NWPS2V1"

/* NwDumpBlock.kind: at a PS2 address, at an offset into another block, or where PCSX2's pointers to it say. */
enum { NW_BLOCK_ABS, NW_BLOCK_IN, NW_BLOCK_FREE };

extern union variptr_u superbuffer_base;
extern union variptr_u superbuffer_end;

/* Records whose last member is a one-element array that runs on past the type (the struct hack). */
typedef struct NwDumpHack {
    const NwType *type, *elem;
    uint32_t ps2_off, host_off; /* of the array */
} NwDumpHack;

static const NwDumpHack nw_dump_hacks[] = {
    { &nw_type_shaddata_s, &nw_type_shadslot_u, offsetof(nw_ps2_shaddata_s, table),
      offsetof(struct shaddata_s, table) },
    { &nw_type_NUGCUTCAMSYS_s, &nw_type_s8, offsetof(nw_ps2_NUGCUTCAMSYS_s, remap),
      offsetof(struct NUGCUTCAMSYS_s, remap) },
};

typedef struct NwDumpBlock {
    uint32_t kind, parent, base;
    const char *name; /* a root's; NULL for the heap's */
    char *path;       /* the first field that reached it */
    const NwType *type; /* NULL: plain bytes */
    uint32_t count;
    const unsigned char *host;
    size_t host_size;
    const NwDumpHack *hack; /* count 1, then ntail elements of its array */
    uint32_t ntail;
    unsigned char *out; /* typed: the PS2 bytes */
    size_t out_size;
    size_t reloc, nrelocs;
    bool done;
    bool hole; /* count bytes the port does not model */
} NwDumpBlock;

typedef struct NwDumpReloc {
    uint32_t off, target;
    size_t host_off; /* into the target, mapped to its PS2 offset on writing */
} NwDumpReloc;

typedef struct NwPtrMap {
    uintptr_t *keys;
    uint32_t *vals;
    size_t cap, n;
} NwPtrMap;

typedef struct NwDump {
    NwXCtx ctx;
    NwDumpBlock *blocks;
    size_t nblocks, cap;
    NwDumpReloc *relocs;
    size_t nrelocs, rcap;
    struct {
        uint32_t target;
        size_t host_off;
    } *marks;
    uint32_t nmarks, mcap;
    NwPtrMap recs, inheap, opaque;
    const void **opaque_host;
    size_t nopaque, ocap;
    uint32_t sb;
    const char *sb_lo, *sb_hi;
    jmp_buf fail;
    char *msg;
    unsigned failed, unplaced, conflicts, unmapped;
} NwDump;

static NwDump nw_d; /* not a local: a block's conversion may longjmp out of it */

static _Noreturn void nw_dump_oom(void)
{
    snprintf(nw_d.msg, 256, "out of memory");
    longjmp(nw_d.fail, 2);
}

static void *nw_dump_grow(void *p, size_t *cap, size_t need, size_t size)
{
    if (need <= *cap) {
        return p;
    }
    size_t n = *cap ? *cap * 2 : 256;
    n = n < need ? need : n;
    void *grown = realloc(p, n * size);
    if (!grown) {
        nw_dump_oom();
    }
    *cap = n;
    return grown;
}

/* The value of key, inserting fresh (NW_DUMP_NONE) when missing. */
static uint32_t *nw_map_slot(NwPtrMap *m, uintptr_t key)
{
    if (2 * (m->n + 1) > m->cap) {
        NwPtrMap g = { calloc(m->cap ? m->cap * 2 : 1024, sizeof(uintptr_t)), NULL, m->cap ? m->cap * 2 : 1024, 0 };
        g.vals = malloc(g.cap * sizeof(uint32_t));
        if (!g.keys || !g.vals) {
            nw_dump_oom();
        }
        for (size_t i = 0; i < m->cap; i++) {
            if (m->keys[i]) {
                *nw_map_slot(&g, m->keys[i]) = m->vals[i];
            }
        }
        free(m->keys);
        free(m->vals);
        *m = g;
    }
    size_t i = (size_t)((key >> 3) * 0x9E3779B97F4A7C15ull) & (m->cap - 1);
    while (m->keys[i] && m->keys[i] != key) {
        i = (i + 1) & (m->cap - 1);
    }
    if (!m->keys[i]) {
        m->keys[i] = key;
        m->vals[i] = NW_DUMP_NONE;
        m->n++;
    }
    return &m->vals[i];
}

static void nw_map_free(NwPtrMap *m)
{
    free(m->keys);
    free(m->vals);
    memset(m, 0, sizeof(*m));
}

/* A pointee converted element by element. */
static bool nw_dump_typed(const NwType *type)
{
    return type && type->ps2_size && type->xr && !(type->flags & (NW_TYPE_POD | NW_TYPE_OPAQUE));
}

static bool nw_dump_plain(const NwType *type)
{
    return type && type->ps2_size && (type->flags & NW_TYPE_POD);
}

static uint32_t nw_dump_block(uint32_t kind, uint32_t parent, uint32_t base, const char *name, const NwType *type,
                              uint32_t count, const void *host, size_t host_size)
{
    NwDump *d = &nw_d;
    d->blocks = nw_dump_grow(d->blocks, &d->cap, d->nblocks + 1, sizeof(*d->blocks));
    d->blocks[d->nblocks] = (NwDumpBlock){ .kind = kind, .parent = parent, .base = base, .name = name, .type = type,
                                           .count = count, .host = host, .host_size = host_size };
    return (uint32_t)d->nblocks++;
}

static void nw_dump_name(NwDump *d, uint32_t b, const char *site)
{
    char path[256];
    nw_xpath(&d->ctx, site, path, sizeof(path));
    d->blocks[b].path = SDL_strdup(path);
}

/* A heap record's block, its type set (or improved while it waits) from the pointer that reached it. */
static uint32_t nw_dump_rec(NwDump *d, const NwHeapRec *rec, const NwType *type, const char *site)
{
    uint32_t *slot = nw_map_slot(&d->recs, (uintptr_t)rec->host);
    if (*slot == NW_DUMP_NONE) {
        const char *at = rec->at;
        bool in = d->sb != NW_DUMP_NONE && at >= d->sb_lo && at < d->sb_hi;
        *slot = nw_dump_block(in ? NW_BLOCK_IN : NW_BLOCK_FREE, in ? d->sb : NW_DUMP_NONE,
                              in ? (uint32_t)(at - d->sb_lo) : 0, NULL, NULL, 0, rec->host, 0);
        nw_dump_name(d, *slot, site);
    }
    NwDumpBlock *b = &d->blocks[*slot];
    if (nw_dump_typed(type) && rec->size >= type->host_size && b->type != type) {
        if (b->done) {
            d->conflicts += b->type != NULL;
        } else if (!b->type || !b->host_size) {
            const NwDumpHack *h = NULL;
            for (size_t i = 0; i < sizeof(nw_dump_hacks) / sizeof(nw_dump_hacks[0]); i++) {
                h = nw_dump_hacks[i].type == type && rec->size > type->host_size ? &nw_dump_hacks[i] : h;
            }
            b->type = type;
            b->hack = h;
            b->count = h ? 1 : (uint32_t)(rec->size / type->host_size);
            b->ntail = h ? (uint32_t)((rec->size - h->host_off) / h->elem->host_size) : 0;
            b->host_size = h ? rec->size : (size_t)b->count * type->host_size;
        }
    } else if (nw_dump_plain(type) && !b->type && !b->host_size && !b->done) {
        b->host_size = rec->size;
        b->count = 1;
    }
    return *slot;
}

/* A typed record the decomp carved inside the superbuffer in host layout: its own block over those bytes. */
static uint32_t nw_dump_inheap(NwDump *d, const char *p, const NwType *type, const char *site, size_t *off)
{
    if (!nw_dump_typed(type) || p + type->host_size > d->sb_hi) {
        *off = (size_t)(p - d->sb_lo);
        return d->sb;
    }
    *off = 0;
    uint32_t *slot = nw_map_slot(&d->inheap, (uintptr_t)p);
    if (*slot == NW_DUMP_NONE) {
        *slot = nw_dump_block(NW_BLOCK_IN, d->sb, (uint32_t)(p - d->sb_lo), NULL, type, 1, p, type->host_size);
        nw_dump_name(d, *slot, site);
    }
    return *slot;
}

static uint32_t nw_dump_mark(NwDump *d, const void *p, const NwType *type, const char *site)
{
    uint32_t target;
    size_t off = 0;
    const NwHeapRec *rec = nw_heap_find(p);
    if (rec) {
        target = nw_dump_rec(d, rec, type, site);
        off = (size_t)((const char *)p - (const char *)rec->host);
    } else if (d->sb != NW_DUMP_NONE && (const char *)p >= d->sb_lo && (const char *)p <= d->sb_hi) {
        target = nw_dump_inheap(d, p, type, site, &off);
    } else {
        uint32_t *slot = nw_map_slot(&d->opaque, (uintptr_t)p);
        if (*slot == NW_DUMP_NONE) {
            *slot = (uint32_t)d->nopaque;
            d->opaque_host = nw_dump_grow(d->opaque_host, &d->ocap, d->nopaque + 1, sizeof(*d->opaque_host));
            d->opaque_host[d->nopaque++] = p;
        }
        target = NW_DUMP_OPAQUE | *slot;
    }
    size_t cap = d->mcap;
    d->marks = nw_dump_grow(d->marks, &cap, (size_t)d->nmarks + 1, sizeof(*d->marks));
    d->mcap = (uint32_t)cap;
    d->marks[d->nmarks].target = target;
    d->marks[d->nmarks].host_off = off;
    return NW_DUMP_MARK | d->nmarks++;
}

static uint32_t nw_dump_ptr_out(NwXCtx *ctx, const void *p, const NwType *type, const char *site)
{
    uint32_t ps2;
    if (nw_image_kept_int(p)) {
        return (uint32_t)(uintptr_t)p;
    }
    return nw_image_ps2(p, &ps2) ? ps2 : nw_dump_mark(ctx->user, p, type, site);
}

/* A data or code address, a heap address, else the plain value it was. */
static uint32_t nw_dump_addr_out(NwXCtx *ctx, uintptr_t value, const char *site)
{
    NwDump *d = ctx->user;
    uint32_t ps2;
    if (nw_image_ps2((const void *)value, &ps2)) {
        return ps2;
    }
    const NwFuncmapEntry *e = nw_funcmap_find_host((NwFuncPtr)value);
    if (e) {
        return e->address;
    }
    const char *p = (const char *)value;
    if (nw_heap_find(p) || (d->sb != NW_DUMP_NONE && p >= d->sb_lo && p <= d->sb_hi)) {
        return nw_dump_mark(d, p, &nw_type_void, site);
    }
    if (value <= UINT32_MAX || ((intptr_t)value < 0 && (intptr_t)value >= INT32_MIN)) {
        return (uint32_t)value;
    }
    return nw_dump_mark(d, p, &nw_type_void, site);
}

static uint32_t nw_dump_fn_out(NwXCtx *ctx, NwFuncPtr fn, const char *site)
{
    const NwVuProg *vu = nw_vu_prog((const void *)fn);
    return vu ? vu->addr : nw_dump_mark(ctx->user, (const void *)fn, &nw_type_void, site);
}

static void nw_dump_fatal(NwXCtx *ctx, const char *message)
{
    NwDump *d = ctx->user;
    snprintf(d->msg, 256, "%s", message);
    longjmp(d->fail, 1);
}

/* The marks of one converted element become relocs. */
static void nw_dump_place(NwDump *d, unsigned char *elem, uint32_t size, uint32_t base)
{
    uint32_t placed = 0;
    for (uint32_t off = 0; off + 4 <= size && placed < d->nmarks; off += 4) {
        uint32_t w;
        memcpy(&w, elem + off, 4);
        if ((w & 0xF0000000u) != NW_DUMP_MARK || (w & 0x0FFFFFFFu) >= d->nmarks) {
            continue;
        }
        uint32_t k = w & 0x0FFFFFFFu;
        if (d->marks[k].target == NW_DUMP_NONE) {
            continue;
        }
        d->relocs = nw_dump_grow(d->relocs, &d->rcap, d->nrelocs + 1, sizeof(*d->relocs));
        d->relocs[d->nrelocs++] = (NwDumpReloc){ base + off, d->marks[k].target, d->marks[k].host_off };
        d->marks[k].target = NW_DUMP_NONE;
        memset(elem + off, 0, 4);
        placed++;
    }
    d->unplaced += d->nmarks - placed;
    d->nmarks = 0;
}

static void nw_dump_convert(NwDump *d, uint32_t i)
{
    NwDumpBlock *b = &d->blocks[i]; /* converting appends blocks: b is stale after the first element */
    b->done = true;
    b->reloc = d->nrelocs;
    if (!b->type) {
        return;
    }
    const NwType *t = b->type;
    const NwDumpHack *h = b->hack;
    const unsigned char *host = b->host;
    uint32_t count = b->count, ntail = b->ntail;
    size_t size = (size_t)(count ? count : 1) * t->ps2_size;
    if (h && h->ps2_off + (size_t)ntail * h->elem->ps2_size > size) {
        size = h->ps2_off + (size_t)ntail * h->elem->ps2_size;
    }
    unsigned char *out = calloc(1, size);
    if (!out) {
        nw_dump_oom();
    }
    b->out = out;
    b->out_size = h ? size : (size_t)count * t->ps2_size;
    const char *root = b->name ? b->name : t->name;
    for (uint32_t n = 0; n < count; n++) {
        unsigned char *elem = out + (size_t)n * t->ps2_size;
        nw_xr_elem(&d->ctx, t, elem, host + (size_t)n * t->host_size, root, count > 1 ? (int)n : -1);
        nw_dump_place(d, elem, t->ps2_size, n * t->ps2_size);
    }
    for (uint32_t k = 1; h && k < ntail; k++) {
        uint32_t at = h->ps2_off + k * h->elem->ps2_size;
        const unsigned char *src = host + h->host_off + (size_t)k * h->elem->host_size;
        if (nw_dump_typed(h->elem)) {
            nw_xr_elem(&d->ctx, h->elem, out + at, src, root, (int)k);
        } else {
            memcpy(out + at, src, h->elem->ps2_size);
        }
        nw_dump_place(d, out + at, h->elem->ps2_size, at);
    }
}

/* Heap bytes retail rewrites and the port does not (port/heap.h): a "hole" block of that length. */
static void nw_dump_hole(const void *at, size_t size, const char *why, void *user)
{
    NwDump *d = user;
    const char *p = at;
    if (d->sb != NW_DUMP_NONE && p >= d->sb_lo && p < d->sb_hi && size) {
        uint32_t b = nw_dump_block(NW_BLOCK_IN, d->sb, (uint32_t)(p - d->sb_lo), why, NULL, (uint32_t)size, p, 0);
        d->blocks[b].hole = true;
    }
}

static void nw_dump_root(const NwImageObj *o, void *user)
{
    (void)user;
    const NwType *t = o->type;
    nw_dump_block(NW_BLOCK_ABS, NW_DUMP_NONE, o->addr, o->name, t, o->count, o->host,
                  t ? (size_t)o->count * t->host_size : o->size);
}

static uint32_t nw_dump_ps2_off(NwDump *d, const NwDumpBlock *b, size_t host_off)
{
    if (!b->type) {
        return (uint32_t)host_off;
    }
    const NwType *t = b->type;
    uint32_t inner = 0;
    const NwDumpHack *h = b->hack;
    if (h && host_off >= h->host_off + h->elem->host_size) {
        size_t k = (host_off - h->host_off) / h->elem->host_size, rest = (host_off - h->host_off) % h->elem->host_size;
        if (rest && !nw_xoff_host(h->elem, (uint32_t)rest, &inner)) {
            d->unmapped++;
        }
        return (uint32_t)(h->ps2_off + k * h->elem->ps2_size + inner);
    }
    size_t elem = host_off / t->host_size;
    if (host_off != b->host_size && !nw_xoff_host(t, (uint32_t)(host_off % t->host_size), &inner)) {
        d->unmapped++;
    }
    return (uint32_t)(elem * t->ps2_size + inner);
}

static bool nw_dump_put(FILE *f, const void *p, size_t n)
{
    return n == 0 || fwrite(p, 1, n, f) == n;
}

static bool nw_dump_u32(FILE *f, uint32_t v)
{
    return nw_dump_put(f, &v, 4);
}

static bool nw_dump_str(FILE *f, const char *s)
{
    uint16_t n = (uint16_t)(s ? strlen(s) : 0);
    return nw_dump_put(f, &n, 2) && nw_dump_put(f, s, n);
}

/* Header "NWPS2V1\0", vblank, vbcnt; per block id, kind, parent, base, size, count, name, type ("hole": count
   bytes not compared), path (u16 length + bytes), its PS2 bytes, relocs (count, then off, target, target offset);
   NW_DUMP_NONE; the opaque targets' host addresses (count, u64 each); the counters failed, unplaced, conflicts,
   unmapped. tools/ps2_diff.py reads it. */
static bool nw_dump_emit(NwDump *d, FILE *f, int vblank, int vbcnt)
{
    bool ok = nw_dump_put(f, NW_DUMP_MAGIC, 8) && nw_dump_u32(f, (uint32_t)vblank) && nw_dump_u32(f, (uint32_t)vbcnt);
    for (size_t i = 0; ok && i < d->nblocks; i++) {
        const NwDumpBlock *b = &d->blocks[i];
        size_t size = b->type ? b->out_size : b->host_size;
        ok = nw_dump_u32(f, (uint32_t)i) && nw_dump_u32(f, b->kind) && nw_dump_u32(f, b->parent) &&
             nw_dump_u32(f, b->base) && nw_dump_u32(f, (uint32_t)size) && nw_dump_u32(f, b->count) &&
             nw_dump_str(f, b->name) && nw_dump_str(f, b->hole ? "hole" : b->type ? b->type->name : NULL) &&
             nw_dump_str(f, b->path) &&
             nw_dump_put(f, b->type ? b->out : b->host, size) && nw_dump_u32(f, (uint32_t)b->nrelocs);
        for (size_t r = b->reloc; ok && r < b->reloc + b->nrelocs; r++) {
            const NwDumpReloc *x = &d->relocs[r];
            uint32_t off = x->target & NW_DUMP_OPAQUE ? 0 : nw_dump_ps2_off(d, &d->blocks[x->target], x->host_off);
            ok = nw_dump_u32(f, x->off) && nw_dump_u32(f, x->target) && nw_dump_u32(f, off);
        }
    }
    ok = ok && nw_dump_u32(f, NW_DUMP_NONE) && nw_dump_u32(f, (uint32_t)d->nopaque);
    for (size_t i = 0; ok && i < d->nopaque; i++) {
        uint64_t h = (uint64_t)(uintptr_t)d->opaque_host[i];
        ok = nw_dump_put(f, &h, 8);
    }
    return ok && nw_dump_u32(f, d->failed) && nw_dump_u32(f, d->unplaced) && nw_dump_u32(f, d->conflicts) &&
           nw_dump_u32(f, d->unmapped);
}

static void nw_dump_free(NwDump *d)
{
    for (size_t i = 0; i < d->nblocks; i++) {
        free(d->blocks[i].out);
        SDL_free(d->blocks[i].path);
    }
    free(d->blocks);
    free(d->relocs);
    free(d->marks);
    free(d->opaque_host);
    nw_map_free(&d->recs);
    nw_map_free(&d->inheap);
    nw_map_free(&d->opaque);
}

bool nw_dump_write(const char *path, int vblank, int vbcnt, bool heap, char *err, size_t err_size)
{
    NwDump *d = &nw_d;
    char msg[256] = "";
    *d = (NwDump){ .msg = msg, .sb = NW_DUMP_NONE };
    d->ctx = (NwXCtx){ .ptr_out = nw_dump_ptr_out, .addr_out = nw_dump_addr_out, .vutext_out = nw_dump_fn_out,
                       .fatal = nw_dump_fatal, .user = d };
    volatile size_t i = 0;
    int how = setjmp(d->fail);
    if (how == 2) {
        snprintf(err, err_size, "%s", msg);
        nw_dump_free(d);
        return false;
    }
    if (how == 0) {
        if (superbuffer_base.voidptr) {
            d->sb_lo = superbuffer_base.charptr;
            d->sb_hi = superbuffer_end.charptr;
            d->sb = nw_dump_block(NW_BLOCK_FREE, NW_DUMP_NONE, 0, "superbuffer", NULL, 1, d->sb_lo,
                                  heap ? (size_t)(d->sb_hi - d->sb_lo) : 0);
        }
        nw_image_each(nw_dump_root, d);
        nw_heap_each_hole(nw_dump_hole, d);
    } else {
        /* the block that failed keeps what it converted; its relocs are dropped */
        NwDumpBlock *b = &d->blocks[i];
        if (d->failed++ < 8) {
            fprintf(stderr, "dump: %s: %s\n", b->name ? b->name : b->path ? b->path : "?", msg);
        }
        d->nrelocs = b->reloc;
        d->nmarks = 0;
        b->count = 0;
        b->out_size = 0;
        i++;
    }
    for (; i < d->nblocks; i++) {
        nw_dump_convert(d, (uint32_t)i);
        d->blocks[i].nrelocs = d->nrelocs - d->blocks[i].reloc;
    }
    FILE *f = fopen(path, "wb");
    bool ok = f && nw_dump_emit(d, f, vblank, vbcnt);
    ok = f && fclose(f) == 0 && ok;
    if (!ok) {
        snprintf(err, err_size, "%s: cannot write", path);
    }
    nw_dump_free(d);
    return ok;
}

static struct {
    int vblanks[NW_DUMP_MAX];
    int count, next;
    char *dir;
    bool heap;
    unsigned written, failed;
    struct {
        int vblank;
        uint32_t addr, value;
        bool done;
    } pokes[NW_POKE_MAX];
    int npokes;
    struct {
        uint32_t addr, value;
        int base; /* the vblank it first held; -1 before */
    } after;
    SDL_AtomicInt finished;
} nw_dump = { .after.base = -1 };

static int nw_dump_cmp(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

bool nw_dump_start(const char *list, const char *dir, bool heap)
{
    size_t size = 0;
    char *text = SDL_LoadFile(list, &size);
    const char *s = text ? text : list;
    while (*s && nw_dump.count < NW_DUMP_MAX) {
        char *end;
        if (text) {
            const char *num = NULL;
            if (SDL_strncmp(s, "frame ", 6) == 0 || SDL_strncmp(s, "vblank ", 7) == 0) {
                num = s + (*s == 'f' ? 6 : 7);
            }
            long v = num ? SDL_strtol(num, &end, 10) : 0;
            if (num && end != num) {
                nw_dump.vblanks[nw_dump.count++] = (int)v;
            }
            s += strcspn(s, "\n");
            s += *s == '\n';
            continue;
        }
        long v = SDL_strtol(s, &end, 10);
        if (end == s) {
            break;
        }
        nw_dump.vblanks[nw_dump.count++] = (int)v;
        s = end + (*end == ',');
    }
    SDL_free(text);
    qsort(nw_dump.vblanks, (size_t)nw_dump.count, sizeof(int), nw_dump_cmp);
    nw_dump.dir = SDL_strdup(dir);
    nw_dump.heap = heap;
    return nw_dump.count > 0 && nw_dump.dir;
}

/* The PS2 address of a data image name, D_<address> or 0x<address>, with an optional +offset; 0 when unknown. */
static uint32_t nw_poke_addr(const char *name, size_t len)
{
    char buf[128];
    if (len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, name, len);
    buf[len] = '\0';
    uint32_t off = 0;
    char *plus = strchr(buf, '+');
    if (plus) {
        *plus = '\0';
        off = (uint32_t)SDL_strtoul(plus + 1, NULL, 0);
    }
    if (SDL_strncmp(buf, "D_", 2) == 0 || SDL_strncmp(buf, "0x", 2) == 0) {
        return (uint32_t)SDL_strtoul(buf + 2, NULL, 16) + off;
    }
    const NwImageTables *t = &nw_image_tables;
    for (unsigned i = 0; i < t->nroots; i++) {
        if (strcmp(t->roots[i].name, buf) == 0) {
            return t->roots[i].addr + off;
        }
    }
    for (unsigned u = 0; u < t->nunits; u++) {
        for (unsigned i = 0; i < t->units[u]->count; i++) {
            if (strcmp(t->units[u]->objs[i].name, buf) == 0) {
                return t->units[u]->objs[i].addr + off;
            }
        }
    }
    return 0;
}

bool nw_poke_start(const char *list)
{
    const char *s = list;
    while (*s) {
        char *end;
        long vblank = SDL_strtol(s, &end, 10);
        const char *eq = *end == ':' ? strchr(end, '=') : NULL;
        uint32_t addr = eq ? nw_poke_addr(end + 1, (size_t)(eq - end - 1)) : 0;
        if (!addr || nw_dump.npokes == NW_POKE_MAX) {
            fprintf(stderr, "--poke: cannot read \"%s\"\n", s);
            return false;
        }
        nw_dump.pokes[nw_dump.npokes].vblank = (int)vblank;
        nw_dump.pokes[nw_dump.npokes].addr = addr;
        nw_dump.pokes[nw_dump.npokes++].value = (uint32_t)SDL_strtoll(eq + 1, &end, 0);
        s = end + (*end == ',');
    }
    return nw_dump.npokes > 0;
}

bool nw_dump_after(const char *cond)
{
    const char *eq = strchr(cond, '=');
    nw_dump.after.addr = eq ? nw_poke_addr(cond, (size_t)(eq - cond)) : 0;
    if (!nw_dump.after.addr) {
        fprintf(stderr, "--dump-after: cannot read \"%s\"\n", cond);
        return false;
    }
    nw_dump.after.value = (uint32_t)SDL_strtoll(eq + 1, NULL, 0);
    return true;
}

void nw_dump_vblank(int vbcnt)
{
    int vblank;
    if ((!nw_dump.count && !nw_dump.npokes) || !nw_trace_vblank(vbcnt, &vblank)) {
        return;
    }
    for (int i = 0; i < nw_dump.npokes; i++) {
        if (!nw_dump.pokes[i].done && nw_dump.pokes[i].vblank <= vblank) {
            void *host = nw_image_host(nw_dump.pokes[i].addr);
            if (host) {
                memcpy(host, &nw_dump.pokes[i].value, 4);
            } else {
                fprintf(stderr, "--poke: no host object at 0x%08x\n", (unsigned)nw_dump.pokes[i].addr);
            }
            nw_dump.pokes[i].done = true;
        }
    }
    if (nw_dump.after.addr) {
        const void *host = nw_dump.after.base < 0 ? nw_image_host(nw_dump.after.addr) : NULL;
        uint32_t v = 0;
        if (host) {
            memcpy(&v, host, 4);
        }
        if (host && v == nw_dump.after.value) {
            nw_dump.after.base = vblank;
        }
        if (nw_dump.after.base < 0) {
            return;
        }
        vblank -= nw_dump.after.base;
    }
    while (nw_dump.next < nw_dump.count && nw_dump.vblanks[nw_dump.next] <= vblank) {
        if (nw_dump.vblanks[nw_dump.next] == vblank &&
            (nw_dump.next == 0 || nw_dump.vblanks[nw_dump.next - 1] != vblank)) {
            char *path = NULL, err[512];
            SDL_asprintf(&path, "%s%d.nwd", nw_dump.dir, vblank);
            if (path && nw_dump_write(path, vblank, vbcnt, nw_dump.heap, err, sizeof(err))) {
                nw_dump.written++;
            } else {
                fprintf(stderr, "dump: vblank %d: %s\n", vblank, path ? err : "out of memory");
                nw_dump.failed++;
            }
            SDL_free(path);
        }
        nw_dump.next++;
    }
    if (nw_dump.count && nw_dump.next == nw_dump.count) {
        SDL_SetAtomicInt(&nw_dump.finished, 1);
    }
}

bool nw_dump_finished(void)
{
    return SDL_GetAtomicInt(&nw_dump.finished) != 0;
}

unsigned nw_dump_written(unsigned *failed)
{
    *failed = nw_dump.failed;
    return nw_dump.written;
}
