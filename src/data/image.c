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

/* The object holding addr (one past its end too), with the offset into it. */
static NwObj *nw_image_find(uint32_t addr, uint32_t *off)
{
    size_t i = nw_image_upper(addr);
    if (i == 0) {
        return NULL;
    }
    NwObj *o = &nw_image.objs[i - 1];
    if (addr - o->addr < (o->size ? o->size : 1) || (o->size && addr - o->addr == o->size)) {
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

static uintptr_t nw_image_addr_in(NwXCtx *ctx, uint32_t value, int is_signed, const char *site)
{
    const NwImageTables *t = nw_image.t;
    if (value >= t->mirror_base && value - t->mirror_base < t->mirror_size) {
        return (uintptr_t)nw_image_ptr_in(ctx, value, &nw_type_void, site);
    }
    if (value >= t->text_base && value < t->text_end) {
        const NwFuncmapEntry *e = nw_funcmap_find(value);
        if (e) {
            return (uintptr_t)e->fn;
        }
    }
    return is_signed ? (uintptr_t)(intptr_t)(int32_t)value : (uintptr_t)value;
}

/* The reverse direction comes with the round trip. */
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

void nw_image_free(void)
{
    for (size_t i = 0; i < nw_image.nregions; i++) {
        free(nw_image.regions[i]->obj.host);
        free(nw_image.regions[i]);
    }
    free(nw_image.regions);
    free(nw_image.objs);
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
                             .fatal = nw_image_fatal };
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
        if (r->kind != NW_ROOT_ALIAS) {
            NwObj o = { r->addr, r->ps2_size, r->host, r->host_size, r->kind == NW_ROOT_CONVERT ? r->elem : NULL,
                        r->count, r->kind == NW_ROOT_CONVERT ? 0u : NW_OBJ_PLAIN, r->name };
            nw_image_push(&o);
        }
    }
    for (unsigned u = 0; u < t->nunits; u++) {
        const NwDataUnit *unit = t->units[u];
        for (unsigned i = 0; i < unit->count; i++) {
            const NwDataObj *d = &unit->objs[i];
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
    NwObj *o = nw_image_find(ps2, &off);
    if (o) {
        return nw_image_map(o, off, &p) ? p : NULL;
    }
    return ps2 >= t->mirror_base && ps2 - t->mirror_base < t->mirror_size ? t->mirror + (ps2 - t->mirror_base) : NULL;
}

void nw_image_report(const NwImageStats *s, FILE *out)
{
    fprintf(out, "roots: %u mirror, %u copied, %u converted, %u aliases\n", s->roots[NW_ROOT_MIRROR],
            s->roots[NW_ROOT_COPY], s->roots[NW_ROOT_CONVERT], s->roots[NW_ROOT_ALIAS]);
    fprintf(out, "C definitions at retail addresses: %u\n", s->objects);
    fprintf(out, "objects only pointers reach: %u (%llu bytes)\n", s->regions,
            (unsigned long long)s->region_bytes);
    fprintf(out, "data pointers: %u translated, %u small values or -1 kept, %u void pointers to unclaimed bytes\n",
            s->pointers, s->kept_ints, s->unclaimed);
    fprintf(out, "code pointers: %u (%u to stubs)\n", s->code_pointers, s->stub_pointers);
}
