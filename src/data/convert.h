/* Runtime of the generated converters (gen/nw_convert.h, tools/gen_converters.py): PS2 bytes in the
   nw_ps2types.h layout <-> host structs. Data pointers go through the context, code pointers through the
   funcmap. */
#ifndef NUWORLD_DATA_CONVERT_H
#define NUWORLD_DATA_CONVERT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "port/funcmap.h"

typedef struct NwType NwType;
typedef struct NwXCtx NwXCtx;
typedef void (*NwXFn)(void *dst, const void *src, NwXCtx *ctx);

/* NwType.flags */
#define NW_TYPE_POD 1u    /* the same bytes on both sides: offsets map 1:1 */
#define NW_TYPE_OPAQUE 2u /* not defined in decomp/include: only offset 0 maps */
#define NW_TYPE_HOOK 4u   /* converted in part by a hand-written hook (src/data/xhooks.c) */

/* NwSpan.kind: BYTES maps byte for byte, SLOT (pointers, nuintptr) only at the start of each element,
   RECORD through its element type. */
enum { NW_SPAN_BYTES, NW_SPAN_SLOT, NW_SPAN_RECORD };

typedef struct NwSpan {
    uint32_t ps2_off, host_off;
    uint32_t ps2_size, host_size; /* of one element */
    uint32_t count;
    unsigned kind;
    const NwType *type;
} NwSpan;

struct NwType {
    const char *name;
    uint32_t ps2_size, ps2_align, host_size, host_align;
    unsigned flags;
    NwXFn x, xr; /* NULL for primitives and opaque types */
    const NwSpan *spans; /* PS2 offset order; unused for POD */
    unsigned nspans;
};

#define NW_XCTX_DEPTH 32

/* The callbacks never see 0 / NULL pointers. ptr_in only records (addr, type) for the data image's worklist
   and returns the host address; the pointee is converted later. */
struct NwXCtx {
    void *(*ptr_in)(NwXCtx *ctx, uint32_t addr, const NwType *type, const char *site);
    uint32_t (*ptr_out)(NwXCtx *ctx, const void *p, const NwType *type, const char *site);
    /* nuintptr / nusintptr: a data or code address, or a plain value. */
    uintptr_t (*addr_in)(NwXCtx *ctx, uint32_t value, int is_signed, const char *site);
    uint32_t (*addr_out)(NwXCtx *ctx, uintptr_t value, const char *site);
    /* Code addresses the funcmap does not know (.vutext microprograms); NULL makes them fatal. */
    NwFuncPtr (*vutext_in)(NwXCtx *ctx, uint32_t addr, const char *site);
    uint32_t (*vutext_out)(NwXCtx *ctx, NwFuncPtr fn, const char *site);
    /* Must not return; NULL prints the message and aborts. */
    void (*fatal)(NwXCtx *ctx, const char *message);
    void *user;
    struct {
        const char *name;
        int index;
    } path[NW_XCTX_DEPTH];
    int depth;
};

/* Convert one object; root names it in errors (a symbol or the type name). */
void nw_x(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root);
void nw_xr(NwXCtx *ctx, const NwType *type, void *dst, const void *src, const char *root);

/* The host offset of a PS2 offset into type; the size (one past the end) maps too. */
int nw_xoff(const NwType *type, uint32_t ps2_off, uint32_t *host_off);

/* "root.field[i].site" */
void nw_xpath(const NwXCtx *ctx, const char *site, char *buf, size_t size);
_Noreturn void nw_xfatal(NwXCtx *ctx, const char *site, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

NwFuncPtr nw_fn_in(NwXCtx *ctx, uint32_t addr, const char *site);
uint32_t nw_fn_out(NwXCtx *ctx, NwFuncPtr fn, const char *site);

static inline void *nw_ptr_in(NwXCtx *ctx, uint32_t addr, const NwType *type, const char *site)
{
    return addr ? ctx->ptr_in(ctx, addr, type, site) : NULL;
}

static inline uint32_t nw_ptr_out(NwXCtx *ctx, const void *p, const NwType *type, const char *site)
{
    return p ? ctx->ptr_out(ctx, p, type, site) : 0;
}

static inline void nw_xpush(NwXCtx *ctx, const char *name, int index)
{
    if (ctx->depth == NW_XCTX_DEPTH) {
        nw_xfatal(ctx, name, "nested too deep");
    }
    ctx->path[ctx->depth].name = name;
    ctx->path[ctx->depth].index = index;
    ctx->depth++;
}

/* A nested converter call with its field on the path. */
#define NW_XNEST(ctx, name, index, call) \
    do { \
        nw_xpush(ctx, name, index); \
        call; \
        (ctx)->depth--; \
    } while (0)
#define NW_XCOPY(dst, src) memcpy(&(dst), &(src), sizeof(dst))
#define NW_XBYTES(dst, dst_off, src, src_off, size) \
    memcpy((char *)(dst) + (dst_off), (const char *)(src) + (src_off), size)

extern const NwType nw_type_void, nw_type_fnptr;
extern const NwType nw_type_u8, nw_type_s8, nw_type_u16, nw_type_s16, nw_type_u32, nw_type_s32;
extern const NwType nw_type_u64, nw_type_s64, nw_type_u128, nw_type_f32, nw_type_f64;

#endif
