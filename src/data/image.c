#include "data/image.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "data/convert.h"
#include "data/elf.h"

/* Below .text: flags and counts that retail keeps in pointer slots and never dereferences. */
#define NW_IMAGE_INT_LIMIT 0x00100000u

enum { NW_OBJ_PLAIN = 1u, NW_OBJ_REGION = 2u };

typedef struct NwObj {
    uint32_t addr, size; /* PS2 extent; 0: an unsized mirror root, only its start maps */
    unsigned char *host;
    uint32_t host_size;
    const NwType *type; /* element type; NULL and not plain: only offset 0 maps */
    uint32_t count;
    unsigned flags;
    const char *name;
} NwObj;

typedef struct NwRegion {
    NwObj obj;
    char name[16];
} NwRegion;

typedef struct NwImage {
    const NwImageTables *t;
    NwObj *objs; /* address order */
    size_t nobjs, cap;
    NwRegion **regions; /* created on demand, converted in creation order */
    size_t nregions, region_cap;
    const NwObj **by_host; /* the objects outside the mirror in host address order, for the way back */
    size_t nby_host;
    NwImageStats *stats;
    jmp_buf fail;
    char *err;
    size_t err_size;
    NwXCtx ctx;
} NwImage;

static NwImage nw_image;

static void nw_image_fatal(NwXCtx *ctx, const char *message)
{
    (void)ctx;
    snprintf(nw_image.err, nw_image.err_size, "%s", message);
    longjmp(nw_image.fail, 1);
}

/* A pointee that converts element by element: no POD, opaque or void type. */
static bool nw_image_typed(const NwType *type)
{
    return type && type->ps2_size && !(type->flags & (NW_TYPE_POD | NW_TYPE_OPAQUE));
}

static void nw_image_push(const NwObj *o)
{
    if (nw_image.nobjs == nw_image.cap) {
        size_t cap = nw_image.cap ? nw_image.cap * 2 : 4096;
        NwObj *objs = realloc(nw_image.objs, cap * sizeof(*objs));
        if (!objs) {
            nw_image_fatal(&nw_image.ctx, "out of memory");
        }
        nw_image.objs = objs;
        nw_image.cap = cap;
    }
    nw_image.objs[nw_image.nobjs++] = *o;
}

static int nw_image_cmp(const void *a, const void *b)
{
    uint32_t x = ((const NwObj *)a)->addr, y = ((const NwObj *)b)->addr;
    return x < y ? -1 : x > y;
}

/* First object starting above addr. */
static size_t nw_image_upper(uint32_t addr)
{
    size_t lo = 0, hi = nw_image.nobjs;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (nw_image.objs[mid].addr <= addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* Whether a retail data symbol starts at addr. */
static bool nw_image_symbol_at(uint32_t addr)
{
    const NwImageTables *t = nw_image.t;
    size_t lo = 0, hi = t->nregistry;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (t->registry[mid][0] < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < t->nregistry && t->registry[lo][0] == addr;
}

/* The object holding addr, with the offset into it. One past its end counts unless a retail symbol starts
   there: then the pointer is to that symbol (CData[0].anim is CrashAnim, right after CrashAnim_CREDITS). */
static NwObj *nw_image_find(uint32_t addr, uint32_t *off)
{
    size_t i = nw_image_upper(addr);
    if (i == 0) {
        return NULL;
    }
    NwObj *o = &nw_image.objs[i - 1];
    if (addr - o->addr < (o->size ? o->size : 1) ||
        (o->size && addr - o->addr == o->size && !nw_image_symbol_at(addr))) {
        *off = addr - o->addr;
        return o;
    }
    return NULL;
}

static bool nw_image_map(const NwObj *o, uint32_t off, unsigned char **out)
{
    if (o->size && off == o->size) {
        *out = o->host + o->host_size;
        return true;
    }
    if (o->flags & NW_OBJ_PLAIN) {
        *out = o->host + off;
        return true;
    }
    if (!o->type) {
        *out = o->host;
        return off == 0;
    }
    uint32_t ps = o->type->ps2_size, h;
    if (!nw_xoff(o->type, off % ps, &h)) {
        return false;
    }
    *out = o->host + (size_t)(off / ps) * o->type->host_size + h;
    return true;
}

static unsigned char *nw_image_at(NwXCtx *ctx, const NwObj *o, uint32_t addr, uint32_t off, const char *site)
{
    unsigned char *p;
    if (!nw_image_map(o, off, &p)) {
        nw_xfatal(ctx, site, "0x%08x is %s+0x%x, which does not map to the host layout%s", (unsigned)addr, o->name,
                  (unsigned)off, o->type ? "" : " (the C definition's type is not in decomp/include)");
    }
    return p;
}

/* An object only pointers reach: the retail symbol around addr, else up to the next known boundary, typed by
   the pointer and converted after the roots. */
static void *nw_image_region(NwXCtx *ctx, uint32_t addr, const NwType *type, const char *site)
{
    const NwImageTables *t = nw_image.t;
    uint32_t start = addr, end = 0, ps = type->ps2_size;
    size_t lo = 0, hi = t->nregistry;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (t->registry[mid][0] <= addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo && addr - t->registry[lo - 1][0] < t->registry[lo - 1][1]) {
        start = t->registry[lo - 1][0];
        end = start + t->registry[lo - 1][1];
        uint32_t off;
        if ((addr - start) % ps || nw_image_find(start, &off)) {
            start = addr; /* not an array of this type, or a root holds its head: start at the pointer */
        }
    }
    if (end <= addr) {
        lo = 0;
        hi = t->nbounds;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (t->bounds[mid] <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        end = lo < t->nbounds ? t->bounds[lo] : addr + ps;
    }
    size_t next = nw_image_upper(start);
    if (next < nw_image.nobjs && nw_image.objs[next].addr < end) {
        end = nw_image.objs[next].addr;
    }
    uint32_t count = (end - start) / ps ? (end - start) / ps : 1;

    if (nw_image.nregions == nw_image.region_cap) {
        size_t cap = nw_image.region_cap ? nw_image.region_cap * 2 : 256;
        NwRegion **regions = realloc(nw_image.regions, cap * sizeof(*regions));
        if (!regions) {
            nw_xfatal(ctx, site, "out of memory");
        }
        nw_image.regions = regions;
        nw_image.region_cap = cap;
    }
    NwRegion *r = calloc(1, sizeof(*r));
    unsigned char *host = calloc(count, type->host_size);
    if (!r || !host) {
        free(r);
        free(host);
        nw_xfatal(ctx, site, "out of memory");
    }
    snprintf(r->name, sizeof(r->name), "@%08x", (unsigned)start);
    r->obj = (NwObj){ start, count * ps, host, count * type->host_size, type, count, NW_OBJ_REGION, r->name };
    nw_image.regions[nw_image.nregions++] = r;
    nw_image_push(&r->obj);
    memmove(&nw_image.objs[next + 1], &nw_image.objs[next], (nw_image.nobjs - 1 - next) * sizeof(NwObj));
    nw_image.objs[next] = r->obj;
    nw_image.stats->regions++;
    nw_image.stats->region_bytes += r->obj.host_size;
    return nw_image_at(ctx, &r->obj, addr, addr - start, site);
}

static bool nw_image_in_vutext(uint32_t addr)
{
    return addr >= nw_image.t->vutext_base && addr < nw_image.t->vutext_end;
}

/* The package holding a .vutext address, else NULL. */
static const NwVuProg *nw_image_vu_find(const NwImageTables *t, uint32_t addr)
{
    for (unsigned i = 0; i < t->nvu_progs; i++) {
        if (addr - t->vu_progs[i]->addr < t->vu_progs[i]->size) {
            return t->vu_progs[i];
        }
    }
    return NULL;
}

/* The token of the microcode package starting at addr; any other .vutext address is fatal. */
static const NwVuProg *nw_image_vu(NwXCtx *ctx, uint32_t addr, const char *site)
{
    const NwVuProg *p = nw_image_vu_find(nw_image.t, addr);
    if (!p) {
        nw_xfatal(ctx, site, "0x%08x lies in .vutext outside every named microcode package", (unsigned)addr);
    }
    if (p->addr != addr) {
        nw_xfatal(ctx, site, "0x%08x is %s+0x%x, not the start of a microcode package", (unsigned)addr, p->name,
                  (unsigned)(addr - p->addr));
    }
    nw_image.stats->vu_tokens++;
    return p;
}

static void *nw_image_ptr_in(NwXCtx *ctx, uint32_t addr, const NwType *type, const char *site)
{
    const NwImageTables *t = nw_image.t;
    if (addr < NW_IMAGE_INT_LIMIT || addr == UINT32_MAX) {
        nw_image.stats->kept_ints++;
        return (void *)(intptr_t)(int32_t)addr;
    }
    if (addr >= t->text_base && addr < t->text_end) {
        nw_xfatal(ctx, site, "0x%08x points into .text, not at data", (unsigned)addr);
    }
    if (nw_image_in_vutext(addr)) {
        return (void *)(uintptr_t)nw_image_vu(ctx, addr, site);
    }
    if (addr < t->mirror_base || addr - t->mirror_base > t->mirror_size) {
        nw_xfatal(ctx, site, "0x%08x lies outside the retail image", (unsigned)addr);
    }
    nw_image.stats->pointers++;
    uint32_t off;
    NwObj *o = nw_image_find(addr, &off);
    if (o) {
        if ((o->flags & NW_OBJ_PLAIN) && nw_image_typed(type)) {
            nw_xfatal(ctx, site, "0x%08x is in %s, plain data, but points to %s, which has pointer slots",
                      (unsigned)addr, o->name, type->name);
        }
        return nw_image_at(ctx, o, addr, off, site);
    }
    if (!nw_image_typed(type)) {
        if (!type || !type->ps2_size || (type->flags & NW_TYPE_OPAQUE)) {
            nw_image.stats->unclaimed++;
        }
        return t->mirror + (addr - t->mirror_base);
    }
    return nw_image_region(ctx, addr, type, site);
}

/* A .text value that starts no function stays a plain value. */
static uintptr_t nw_image_addr_in(NwXCtx *ctx, uint32_t value, int is_signed, const char *site)
{
    const NwImageTables *t = nw_image.t;
    if (value >= t->mirror_base && value - t->mirror_base < t->mirror_size) {
        return (uintptr_t)nw_image_ptr_in(ctx, value, &nw_type_void, site);
    }
    if (value >= t->text_base && value < t->text_end) {
        const NwFuncmapEntry *e = nw_funcmap_find(value);
        if (e) {
            ctx->code_pointers++;
            ctx->stub_pointers += e->provenance == NW_PROV_STUB;
            return (uintptr_t)e->fn;
        }
    }
    return is_signed ? (uintptr_t)(intptr_t)(int32_t)value : (uintptr_t)value;
}

/* A code address the funcmap does not know: a microcode package (a DMA call target), else fatal. */
static NwFuncPtr nw_image_vutext_in(NwXCtx *ctx, uint32_t addr, const char *site)
{
    if (!nw_image_in_vutext(addr)) {
        nw_xfatal(ctx, site, "no host function for code address 0x%08x", (unsigned)addr);
    }
    return (NwFuncPtr)(uintptr_t)nw_image_vu(ctx, addr, site);
}

/* Loading converts only towards the host; the round trip has its own context. */
static uint32_t nw_image_ptr_out(NwXCtx *ctx, const void *p, const NwType *type, const char *site)
{
    (void)p, (void)type;
    nw_xfatal(ctx, site, "the data image converts only towards the host");
}

static uint32_t nw_image_addr_out(NwXCtx *ctx, uintptr_t value, const char *site)
{
    (void)value;
    nw_xfatal(ctx, site, "the data image converts only towards the host");
}

static int nw_image_host_cmp(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)(*(const NwObj *const *)a)->host, y = (uintptr_t)(*(const NwObj *const *)b)->host;
    return x < y ? -1 : x > y;
}

/* The mirror maps back by itself; every other object goes into the host order. */
static void nw_image_index_host(void)
{
    const NwImageTables *t = nw_image.t;
    uintptr_t lo = (uintptr_t)t->mirror, hi = lo + t->mirror_size;
    nw_image.by_host = malloc((nw_image.nobjs ? nw_image.nobjs : 1) * sizeof(*nw_image.by_host));
    if (!nw_image.by_host) {
        nw_image_fatal(&nw_image.ctx, "out of memory");
    }
    for (size_t i = 0; i < nw_image.nobjs; i++) {
        uintptr_t h = (uintptr_t)nw_image.objs[i].host;
        if (h < lo || h > hi) {
            nw_image.by_host[nw_image.nby_host++] = &nw_image.objs[i];
        }
    }
    qsort(nw_image.by_host, nw_image.nby_host, sizeof(*nw_image.by_host), nw_image_host_cmp);
}

/* The PS2 offset of a host offset into o (its host size included). */
static bool nw_image_unmap(const NwObj *o, size_t off, uint32_t *ps2_off)
{
    if (off == o->host_size) {
        *ps2_off = o->size;
        return true;
    }
    if (o->flags & NW_OBJ_PLAIN) {
        *ps2_off = (uint32_t)off;
        return true;
    }
    if (!o->type) {
        *ps2_off = 0;
        return off == 0;
    }
    uint32_t hs = o->type->host_size, inner;
    if (!nw_xoff_host(o->type, (uint32_t)(off % hs), &inner)) {
        return false;
    }
    *ps2_off = (uint32_t)(off / hs) * o->type->ps2_size + inner;
    return true;
}

void nw_image_free(void)
{
    for (size_t i = 0; i < nw_image.nregions; i++) {
        free(nw_image.regions[i]->obj.host);
        free(nw_image.regions[i]);
    }
    free(nw_image.regions);
    free(nw_image.objs);
    free(nw_image.by_host);
    memset(&nw_image, 0, sizeof(nw_image));
}

bool nw_image_load(const NwImageTables *t, const void *elf, size_t size, NwImageStats *stats, char *err,
                   size_t err_size)
{
    nw_image_free();
    memset(stats, 0, sizeof(*stats));
    nw_image.t = t;
    nw_image.stats = stats;
    nw_image.err = err;
    nw_image.err_size = err_size;
    nw_image.ctx = (NwXCtx){ .ptr_in = nw_image_ptr_in, .ptr_out = nw_image_ptr_out,
                             .addr_in = nw_image_addr_in, .addr_out = nw_image_addr_out,
                             .vutext_in = nw_image_vutext_in, .fatal = nw_image_fatal };
    const char *why = nw_elf_flatten(elf, size, t->mirror_base, t->mirror_base + t->mirror_size, t->mirror);
    if (why) {
        snprintf(err, err_size, "%s", why);
        return false;
    }
    if (setjmp(nw_image.fail)) {
        return false;
    }

    for (unsigned i = 0; i < t->nroots; i++) {
        const NwImageRoot *r = &t->roots[i];
        stats->roots[r->kind]++;
        const unsigned char *src = t->mirror + (r->addr - t->mirror_base);
        if (r->kind == NW_ROOT_COPY) {
            memcpy(r->host, src, r->ps2_size);
        }
        if (r->kind != NW_ROOT_ALIAS && r->kind != NW_ROOT_VU) {
            NwObj o = { r->addr, r->ps2_size, r->host, r->host_size, r->kind == NW_ROOT_CONVERT ? r->elem : NULL,
                        r->count, r->kind == NW_ROOT_CONVERT ? 0u : NW_OBJ_PLAIN, r->name };
            nw_image_push(&o);
        }
    }
    for (unsigned u = 0; u < t->nunits; u++) {
        const NwDataUnit *unit = t->units[u];
        for (unsigned i = 0; i < unit->count; i++) {
            const NwDataObj *d = &unit->objs[i];
            if (d->flags & NW_DATA_RETAIL) {
                memcpy(d->host, t->mirror + (d->addr - t->mirror_base), d->ps2_size);
            }
            NwObj o = { d->addr, d->ps2_size, d->host, d->host_size, d->type, d->count,
                        d->flags & NW_DATA_PLAIN ? NW_OBJ_PLAIN : 0u, d->name };
            nw_image_push(&o);
            stats->objects++;
        }
    }
    qsort(nw_image.objs, nw_image.nobjs, sizeof(NwObj), nw_image_cmp);

    for (unsigned i = 0; i < t->nroots; i++) {
        const NwImageRoot *r = &t->roots[i];
        if (r->kind != NW_ROOT_CONVERT) {
            continue;
        }
        const unsigned char *src = t->mirror + (r->addr - t->mirror_base);
        for (unsigned n = 0; n < r->count; n++) {
            nw_x_elem(&nw_image.ctx, r->elem, (unsigned char *)r->host + (size_t)n * r->elem->host_size,
                      src + (size_t)n * r->elem->ps2_size, r->name, r->count > 1 ? (int)n : -1);
        }
    }
    for (size_t w = 0; w < nw_image.nregions; w++) {
        const NwObj o = nw_image.regions[w]->obj; /* regions created while converting append */
        const unsigned char *src = t->mirror + (o.addr - t->mirror_base);
        for (uint32_t n = 0; n < o.count; n++) {
            nw_x_elem(&nw_image.ctx, o.type, o.host + (size_t)n * o.type->host_size,
                      src + (size_t)n * o.type->ps2_size, o.name, o.count > 1 ? (int)n : -1);
        }
    }
    stats->code_pointers = nw_image.ctx.code_pointers;
    stats->stub_pointers = nw_image.ctx.stub_pointers;
    nw_image_index_host();
    return true;
}

void *nw_image_host(uint32_t ps2)
{
    const NwImageTables *t = nw_image.t;
    uint32_t off;
    unsigned char *p;
    if (!t) {
        return NULL;
    }
    if (nw_image_in_vutext(ps2)) {
        const NwVuProg *p = nw_image_vu_find(t, ps2);
        return p && p->addr == ps2 ? (void *)(uintptr_t)p : NULL;
    }
    NwObj *o = nw_image_find(ps2, &off);
    if (o) {
        return nw_image_map(o, off, &p) ? p : NULL;
    }
    return ps2 >= t->mirror_base && ps2 - t->mirror_base < t->mirror_size ? t->mirror + (ps2 - t->mirror_base) : NULL;
}

bool nw_image_ps2(const void *host, uint32_t *ps2)
{
    const NwImageTables *t = nw_image.t;
    uintptr_t p = (uintptr_t)host;
    if (!t) {
        return false;
    }
    const NwVuProg *vu = nw_vu_prog(host);
    if (vu) {
        *ps2 = vu->addr;
        return true;
    }
    if (p >= (uintptr_t)t->mirror && p - (uintptr_t)t->mirror <= t->mirror_size) {
        *ps2 = t->mirror_base + (uint32_t)(p - (uintptr_t)t->mirror);
        return true;
    }
    size_t lo = 0, hi = nw_image.nby_host;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uintptr_t)nw_image.by_host[mid]->host <= p) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return false;
    }
    const NwObj *o = nw_image.by_host[lo - 1];
    uint32_t off;
    if (p - (uintptr_t)o->host > o->host_size || !nw_image_unmap(o, p - (uintptr_t)o->host, &off)) {
        return false;
    }
    *ps2 = o->addr + off;
    return true;
}

const NwVuProg *nw_vu_prog(const void *mpg)
{
    const NwImageTables *t = nw_image.t;
    for (unsigned i = 0; t && i < t->nvu_progs; i++) {
        if ((const void *)t->vu_progs[i] == mpg) {
            return t->vu_progs[i];
        }
    }
    return NULL;
}

void nw_image_report(const NwImageStats *s, FILE *out)
{
    fprintf(out, "roots: %u mirror, %u copied, %u converted, %u aliases, %u microcode packages\n",
            s->roots[NW_ROOT_MIRROR], s->roots[NW_ROOT_COPY], s->roots[NW_ROOT_CONVERT], s->roots[NW_ROOT_ALIAS],
            s->roots[NW_ROOT_VU]);
    fprintf(out, "C definitions at retail addresses: %u\n", s->objects);
    fprintf(out, "objects only pointers reach: %u (%llu bytes)\n", s->regions,
            (unsigned long long)s->region_bytes);
    fprintf(out, "data pointers: %u translated, %u small values or -1 kept, %u void pointers to unclaimed bytes\n",
            s->pointers, s->kept_ints, s->unclaimed);
    fprintf(out, "code pointers: %u (%u to stubs), %u .vutext addresses made microcode package tokens\n",
            s->code_pointers, s->stub_pointers, s->vu_tokens);
}

/* Pointer slots the way back cannot place get a mark (the index of the host pointer) for the comparison. */
#define NW_TRIP_MARK 0xF0000000u
#define NW_TRIP_MARKS 256
/* Differences listed before the rest only count. */
#define NW_TRIP_LIST 40

typedef struct NwTrip {
    NwXCtx ctx;
    NwImageTrip *stats;
    FILE *out;
    unsigned listed;
    const void *marks[NW_TRIP_MARKS];
    unsigned nmarks;
    unsigned char *buf;
    size_t buf_size;
    jmp_buf fail;
    char *err;
    size_t err_size;
} NwTrip;

static void nw_trip_fatal(NwXCtx *ctx, const char *message)
{
    NwTrip *trip = ctx->user;
    snprintf(trip->err, trip->err_size, "%s", message);
    longjmp(trip->fail, 1);
}

static uint32_t nw_trip_mark(NwXCtx *ctx, const void *p, const char *site)
{
    NwTrip *trip = ctx->user;
    if (trip->nmarks == NW_TRIP_MARKS) {
        nw_xfatal(ctx, site, "more than %d pointers outside the image in one element", NW_TRIP_MARKS);
    }
    trip->marks[trip->nmarks] = p;
    return NW_TRIP_MARK | trip->nmarks++;
}

/* Small values and -1 went in as integers (nw_image_ptr_in). */
static uint32_t nw_trip_ptr_out(NwXCtx *ctx, const void *p, const NwType *type, const char *site)
{
    (void)type;
    uint32_t ps2;
    if ((uintptr_t)p < NW_IMAGE_INT_LIMIT || (intptr_t)p == -1) {
        return (uint32_t)(uintptr_t)p;
    }
    return nw_image_ps2(p, &ps2) ? ps2 : nw_trip_mark(ctx, p, site);
}

/* A data or code address (nw_image_addr_in), else the plain value it was. */
static uint32_t nw_trip_addr_out(NwXCtx *ctx, uintptr_t value, const char *site)
{
    uint32_t ps2;
    if (nw_image_ps2((const void *)value, &ps2)) {
        return ps2;
    }
    const NwFuncmapEntry *e = nw_funcmap_find_host((NwFuncPtr)value);
    if (e) {
        return e->address;
    }
    if (value <= UINT32_MAX || ((intptr_t)value < 0 && (intptr_t)value >= INT32_MIN)) {
        return (uint32_t)value;
    }
    return nw_trip_mark(ctx, (const void *)value, site);
}

/* A host function with no retail address, or a microcode package token. */
static uint32_t nw_trip_fn_out(NwXCtx *ctx, NwFuncPtr fn, const char *site)
{
    const NwVuProg *vu = nw_vu_prog((const void *)fn);
    return vu ? vu->addr : nw_trip_mark(ctx, (const void *)fn, site);
}

/* Whether the string at host p is the one at PS2 address addr. */
static bool nw_trip_same_string(const char *p, uint32_t addr)
{
    const NwImageTables *t = nw_image.t;
    if (addr < t->mirror_base || addr - t->mirror_base >= t->mirror_size) {
        return false;
    }
    const char *s = (const char *)t->mirror + (addr - t->mirror_base);
    size_t left = t->mirror_size - (addr - t->mirror_base);
    for (size_t i = 0; i < left; i++) {
        if (p[i] != s[i]) {
            return false;
        }
        if (!s[i]) {
            return true;
        }
    }
    return false;
}

static void nw_trip_hex(char *buf, const unsigned char *p, uint32_t n)
{
    size_t len = 0;
    buf[0] = '\0';
    for (uint32_t i = 0; i < n; i++) {
        len += (size_t)sprintf(buf + len, "%s%02x", i ? " " : "", p[i]);
    }
}

/* got: one element converted back; want: the bytes of the ELF. A marked slot matches when its host pointer is
   a string with the text the pointer of the ELF points at. */
static void nw_trip_compare(NwTrip *trip, int which, const char *name, int index, uint32_t addr,
                            unsigned char *got, const unsigned char *want, uint32_t size)
{
    trip->stats->objects[which] += index <= 0;
    trip->stats->bytes[which] += size;
    for (unsigned m = 0; m < trip->nmarks; m++) {
        for (uint32_t off = 0; off + 4 <= size; off += 4) {
            uint32_t g, w;
            memcpy(&g, got + off, 4);
            if (g != (NW_TRIP_MARK | m)) {
                continue;
            }
            memcpy(&w, want + off, 4);
            if (nw_trip_same_string(trip->marks[m], w)) {
                memcpy(got + off, &w, 4);
                trip->stats->strings++;
            }
            break;
        }
    }
    trip->nmarks = 0;
    if (memcmp(got, want, size) == 0) {
        return;
    }
    trip->stats->differ[which]++;
    if (trip->listed++ >= NW_TRIP_LIST) {
        return;
    }
    uint32_t off = 0, n = 0, mark = 0;
    while (got[off] == want[off]) {
        off++;
    }
    for (uint32_t i = off; i < size; i++) {
        n += got[i] != want[i];
    }
    uint32_t show = size - off < 4 ? size - off : 4, word = off & ~3u;
    char where[64], elf[16], back[16];
    if (index >= 0) {
        snprintf(where, sizeof(where), "%s[%d]", name, index);
    } else {
        snprintf(where, sizeof(where), "%s", name);
    }
    nw_trip_hex(elf, want + off, show);
    nw_trip_hex(back, got + off, show);
    if (word + 4 <= size) {
        memcpy(&mark, got + word, 4);
    }
    fprintf(trip->out, "  %s +0x%x (0x%08x): ELF %s, back %s%s; %u bytes differ\n", where, (unsigned)off,
            (unsigned)(addr + off), elf, back,
            (mark & 0xFFF00000u) == NW_TRIP_MARK ? " (a host pointer outside the image)" : "", (unsigned)n);
}

static unsigned char *nw_trip_buf(NwTrip *trip, size_t size)
{
    if (size > trip->buf_size) {
        unsigned char *buf = realloc(trip->buf, size);
        if (!buf) {
            nw_xfatal(&trip->ctx, NULL, "out of memory");
        }
        trip->buf = buf;
        trip->buf_size = size;
    }
    return trip->buf;
}

/* Every element of an object, converted back. */
static void nw_trip_typed(NwTrip *trip, int which, const char *name, uint32_t addr, const unsigned char *host,
                          const NwType *type, uint32_t count)
{
    const NwImageTables *t = nw_image.t;
    unsigned char *buf = nw_trip_buf(trip, type->ps2_size);
    for (uint32_t n = 0; n < count; n++) {
        uint32_t a = addr + n * type->ps2_size;
        int index = count > 1 ? (int)n : -1;
        nw_xr_elem(&trip->ctx, type, buf, host + (size_t)n * type->host_size, name, index);
        nw_trip_compare(trip, which, name, index, a, buf, t->mirror + (a - t->mirror_base), type->ps2_size);
    }
}

static void nw_trip_plain(NwTrip *trip, int which, const char *name, uint32_t addr, const void *host,
                          uint32_t size)
{
    const NwImageTables *t = nw_image.t;
    unsigned char *buf = nw_trip_buf(trip, size);
    memcpy(buf, host, size);
    nw_trip_compare(trip, which, name, -1, addr, buf, t->mirror + (addr - t->mirror_base), size);
}

bool nw_image_roundtrip(NwImageTrip *stats, FILE *out, char *err, size_t err_size)
{
    static NwTrip nw_trip; /* not a local: the way back may longjmp out of it */
    const NwImageTables *t = nw_image.t;
    NwTrip *trip = &nw_trip;
    *trip = (NwTrip){ .stats = stats, .out = out, .err = err, .err_size = err_size };
    trip->ctx = (NwXCtx){ .ptr_in = nw_image_ptr_in, .ptr_out = nw_trip_ptr_out, .addr_in = nw_image_addr_in,
                          .addr_out = nw_trip_addr_out, .vutext_out = nw_trip_fn_out, .fatal = nw_trip_fatal,
                          .user = trip };
    memset(stats, 0, sizeof(*stats));
    if (!t) {
        snprintf(err, err_size, "no data image loaded");
        return false;
    }
    if (setjmp(trip->fail)) {
        free(trip->buf);
        return false;
    }

    for (unsigned i = 0; i < t->nroots; i++) {
        const NwImageRoot *r = &t->roots[i];
        if (r->kind == NW_ROOT_COPY) {
            nw_trip_plain(trip, 0, r->name, r->addr, r->host, r->ps2_size);
        } else if (r->kind == NW_ROOT_CONVERT) {
            nw_trip_typed(trip, 0, r->name, r->addr, r->host, r->elem, r->count);
        }
    }
    for (size_t i = 0; i < nw_image.nregions; i++) {
        const NwObj *o = &nw_image.regions[i]->obj;
        nw_trip_typed(trip, 0, o->name, o->addr, o->host, o->type, o->count);
    }
    for (unsigned u = 0; u < t->nunits; u++) {
        const NwDataUnit *unit = t->units[u];
        for (unsigned i = 0; i < unit->count; i++) {
            const NwDataObj *d = &unit->objs[i];
            if (d->flags & NW_DATA_PLAIN) {
                nw_trip_plain(trip, 1, d->name, d->addr, d->host, d->ps2_size);
            } else if (d->type) {
                nw_trip_typed(trip, 1, d->name, d->addr, d->host, d->type, d->count);
            } else {
                stats->untyped++;
            }
        }
    }
    if (trip->listed > NW_TRIP_LIST) {
        fprintf(out, "  ... and %u more\n", trip->listed - NW_TRIP_LIST);
    }
    free(trip->buf);
    return true;
}

void nw_image_trip_report(const NwImageTrip *s, FILE *out)
{
    fprintf(out, "round trip: %u image objects (%llu bytes), %u differ\n", s->objects[0],
            (unsigned long long)s->bytes[0], s->differ[0]);
    fprintf(out, "C definitions against the ELF: %u (%llu bytes), %u differ, %u without a type; %u string "
                 "pointers matched by text\n",
            s->objects[1], (unsigned long long)s->bytes[1], s->differ[1], s->untyped, s->strings);
}
