#include "data/convert.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define NW_PRIMITIVE(name, size) \
    const NwType nw_type_##name = { #name, size, size, size, size, NW_TYPE_POD, 0, 0, 0, 0 }

NW_PRIMITIVE(u8, 1);
NW_PRIMITIVE(s8, 1);
NW_PRIMITIVE(u16, 2);
NW_PRIMITIVE(s16, 2);
NW_PRIMITIVE(u32, 4);
NW_PRIMITIVE(s32, 4);
NW_PRIMITIVE(u64, 8);
NW_PRIMITIVE(s64, 8);
NW_PRIMITIVE(u128, 16);
NW_PRIMITIVE(f32, 4);
NW_PRIMITIVE(f64, 8);

const NwType nw_type_void = { "void", 0, 1, 0, 1, NW_TYPE_OPAQUE, 0, 0, 0, 0 };

static void x_fnptr(void *dst, const void *src, NwXCtx *ctx)
{
    uint32_t addr;
    memcpy(&addr, src, sizeof addr);
    *(NwFuncPtr *)dst = nw_fn_in(ctx, addr, NULL);
}

static void xr_fnptr(void *dst, const void *src, NwXCtx *ctx)
{
    uint32_t addr = nw_fn_out(ctx, *(const NwFuncPtr *)src, NULL);
    memcpy(dst, &addr, sizeof addr);
}

static const NwSpan nw_fnptr_span = { 0, 0, 4, sizeof(NwFuncPtr), 1, NW_SPAN_SLOT, 0 };
const NwType nw_type_fnptr = {
    "fnptr", 4, 4, sizeof(NwFuncPtr), _Alignof(NwFuncPtr), 0, x_fnptr, xr_fnptr, &nw_fnptr_span, 1
};

static void convert(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root, int index,
                    int reverse)
{
    ctx->depth = 0;
    nw_xpush(ctx, root, index);
    if (type->flags & NW_TYPE_POD) {
        memcpy(dst, src, type->ps2_size);
    } else if (reverse ? type->xr : type->x) {
        (reverse ? type->xr : type->x)(dst, src, ctx);
    } else {
        nw_xfatal(ctx, NULL, "%s cannot be converted (opaque)", type->name);
    }
    ctx->depth = 0;
}

void nw_x(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root)
{
    convert(ctx, type, dst, src, root, -1, 0);
}

void nw_xr(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root)
{
    convert(ctx, type, dst, src, root, -1, 1);
}

void nw_x_elem(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root, int index)
{
    convert(ctx, type, dst, src, root, index, 0);
}

void nw_xr_elem(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root, int index)
{
    convert(ctx, type, dst, src, root, index, 1);
}

int nw_xoff(const NwType *type, uint32_t ps2_off, uint32_t *host_off)
{
    if (ps2_off == type->ps2_size) {
        *host_off = type->host_size;
        return 1;
    }
    if (ps2_off > type->ps2_size) {
        return 0;
    }
    if (type->flags & NW_TYPE_POD) {
        *host_off = ps2_off;
        return 1;
    }
    unsigned lo = 0, hi = type->nspans;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (type->spans[mid].ps2_off <= ps2_off) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return 0;
    }
    const NwSpan *s = &type->spans[lo - 1];
    uint32_t rel = ps2_off - s->ps2_off;
    if (rel >= s->ps2_size * s->count) {
        return 0;
    }
    uint32_t i = rel / s->ps2_size, inner = rel % s->ps2_size;
    if (s->kind == NW_SPAN_SLOT && inner != 0) {
        return 0;
    }
    if (s->kind == NW_SPAN_RECORD && !nw_xoff(s->type, inner, &inner)) {
        return 0;
    }
    *host_off = s->host_off + i * s->host_size + inner;
    return 1;
}

int nw_xoff_host(const NwType *type, uint32_t host_off, uint32_t *ps2_off)
{
    if (host_off == type->host_size) {
        *ps2_off = type->ps2_size;
        return 1;
    }
    if (host_off > type->host_size) {
        return 0;
    }
    if (type->flags & NW_TYPE_POD) {
        *ps2_off = host_off;
        return 1;
    }
    unsigned lo = 0, hi = type->nspans;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (type->spans[mid].host_off <= host_off) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return 0;
    }
    const NwSpan *s = &type->spans[lo - 1];
    uint32_t rel = host_off - s->host_off;
    if (rel >= s->host_size * s->count) {
        return 0;
    }
    uint32_t i = rel / s->host_size, inner = rel % s->host_size;
    if (s->kind == NW_SPAN_SLOT && inner != 0) {
        return 0;
    }
    if (s->kind == NW_SPAN_RECORD && !nw_xoff_host(s->type, inner, &inner)) {
        return 0;
    }
    *ps2_off = s->ps2_off + i * s->ps2_size + inner;
    return 1;
}

void nw_xpath(const NwXCtx *ctx, const char *site, char *buf, size_t size)
{
    size_t n = 0;
    buf[0] = '\0';
    for (int i = 0; i <= ctx->depth; i++) {
        const char *name = i < ctx->depth ? ctx->path[i].name : site;
        int index = i < ctx->depth ? ctx->path[i].index : -1;
        if (name && *name) {
            snprintf(buf + n, size - n, "%s%s", n ? "." : "", name);
            n = strlen(buf);
        }
        if (index >= 0) {
            snprintf(buf + n, size - n, "[%d]", index);
            n = strlen(buf);
        }
    }
}

void nw_xfatal(NwXCtx *ctx, const char *site, const char *fmt, ...)
{
    char message[512];
    nw_xpath(ctx, site, message, sizeof message);
    size_t n = strlen(message);
    if (n + 2 < sizeof message) {
        memcpy(message + n, ": ", 3);
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(message + n + 2, sizeof message - n - 2, fmt, ap);
        va_end(ap);
    }
    if (ctx->fatal) {
        ctx->fatal(ctx, message);
    }
    fprintf(stderr, "convert: %s\n", message);
    abort();
}

NwFuncPtr nw_fn_in(NwXCtx *ctx, uint32_t addr, const char *site)
{
    if (addr == 0) {
        return NULL;
    }
    const NwFuncmapEntry *e = nw_funcmap_find(addr);
    if (e) {
        ctx->code_pointers++;
        ctx->stub_pointers += e->provenance == NW_PROV_STUB;
        return e->fn;
    }
    if (ctx->vutext_in) {
        return ctx->vutext_in(ctx, addr, site);
    }
    nw_xfatal(ctx, site, "no host function for code address 0x%08x", (unsigned)addr);
}

uint32_t nw_fn_out(NwXCtx *ctx, NwFuncPtr fn, const char *site)
{
    if (fn == NULL) {
        return 0;
    }
    const NwFuncmapEntry *e = nw_funcmap_find_host(fn);
    if (e) {
        return e->address;
    }
    if (ctx->vutext_out) {
        return ctx->vutext_out(ctx, fn, site);
    }
    nw_xfatal(ctx, site, "no PS2 address for a host function");
}
