/* nu3d/nuanim's .ani loader (port/units.toml override): retail's NuAnimDataLoadBuff with host copies of the records
   it fixes in place, beside the caller's buffer (src/port/heap.h); and the anim v1 walk it shares with the animlib. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nw_convert.h"
#include "port/heap.h"
#include "port/nuanim.h"
#include "port/xfile.h"

#define nuanim_srcfile "..\\nu2crash.ps2\\nu3d\\nuanim.c"

/* Declared by no header. */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
void buildBitCountTable(void);
extern int isBitCountTable;

#define NW_ANIM_AT(f, p, type, field, size) (f)->at((f), (p) + offsetof(nw_ps2_##type, field), (size))
#define NW_ANIM_NEW(f, type, n, ps2) ((type *)(f)->alloc((f), (size_t)(n) * sizeof(type), _Alignof(type), (ps2)))

static struct nuanimcurveset_s *nw_anim_curveset(NwAnimFile *f, char *p)
{
    struct nuanimcurveset_s *set = NW_ANIM_NEW(f, struct nuanimcurveset_s, 1, p);
    NW_FILE_X(nuanimcurveset_s, set, p);
    int n = set->ncurves > 0 ? set->ncurves : 0;
    set->constants = (float *)NW_ANIM_AT(f, p, nuanimcurveset_s, constants, (size_t)n * 4);
    char *curves = NW_ANIM_AT(f, p, nuanimcurveset_s, set, (size_t)n * 4);
    set->set = curves ? NW_ANIM_NEW(f, struct nuanimcurve_s *, n, curves) : NULL;
    for (int k = 0; curves && k < n; k++) {
        char *c = f->at(f, curves + 4 * k, sizeof(nw_ps2_nuanimcurve_s));
        if (c) {
            struct nuanimcurve_s *curve = NW_ANIM_NEW(f, struct nuanimcurve_s, 1, c);
            NW_FILE_X(nuanimcurve_s, curve, c);
            curve->animkeys = (struct nuanimkey_s *)NW_ANIM_AT(f, c, nuanimcurve_s, animkeys,
                                                               curve->numkeys * sizeof(nw_ps2_nuanimkey_s));
            set->set[k] = curve;
        }
    }
    return set;
}

static struct nuanimdatachunk_s *nw_anim_chunk(NwAnimFile *f, char *p)
{
    struct nuanimdatachunk_s *chunk = NW_ANIM_NEW(f, struct nuanimdatachunk_s, 1, p);
    NW_FILE_X(nuanimdatachunk_s, chunk, p);
    int n = chunk->numnodes > 0 ? chunk->numnodes : 0;
    char *sets = NW_ANIM_AT(f, p, nuanimdatachunk_s, animcurvesets, (size_t)n * 4);
    chunk->animcurvesets = sets ? NW_ANIM_NEW(f, struct nuanimcurveset_s *, n, sets) : NULL;
    for (int j = 0; sets && j < n; j++) {
        char *s = f->at(f, sets + 4 * j, sizeof(nw_ps2_nuanimcurveset_s));
        chunk->animcurvesets[j] = s ? nw_anim_curveset(f, s) : NULL;
    }
    return chunk;
}

struct nuanimdata_s *nw_anim_data(NwAnimFile *f, char *p)
{
    struct nuanimdata_s *data = NW_ANIM_NEW(f, struct nuanimdata_s, 1, p);
    NW_FILE_X(nuanimdata_s, data, p);
    if (f->node_names) {
        data->node_names = NW_ANIM_AT(f, p, nuanimdata_s, node_names, 0);
    }
    int n = data->nchunks > 0 ? data->nchunks : 0;
    char *chunks = NW_ANIM_AT(f, p, nuanimdata_s, chunks, (size_t)n * 4);
    data->chunks = chunks ? NW_ANIM_NEW(f, struct nuanimdatachunk_s *, n, chunks) : NULL;
    for (int i = 0; chunks && i < n; i++) {
        char *c = f->at(f, chunks + 4 * i, sizeof(nw_ps2_nuanimdatachunk_s));
        data->chunks[i] = c ? nw_anim_chunk(f, c) : NULL;
    }
    return data;
}

typedef struct NwAniLoad {
    NwAnimFile f;
    char *base;
    size_t size;
    uint32_t addr; /* the exporter's address of the file's first byte */
    char *file;
    NwHeapSide *side;
} NwAniLoad;

static _Noreturn void nw_ani_bad(const NwAniLoad *ld, uint32_t off)
{
    NuErrorProlog(nuanim_srcfile, 0x2FE)("<%s>: record at 0x%x outside the file", ld->file, off);
    for (;;) {
    }
}

static char *nw_ani_at(NwAnimFile *f, char *field, size_t size)
{
    const NwAniLoad *ld = (const NwAniLoad *)f;
    uint32_t v;
    memcpy(&v, field, 4);
    uint32_t off = v - ld->addr;
    if (v == 0) {
        return NULL;
    }
    if ((size_t)off > ld->size || size > ld->size - off) {
        nw_ani_bad(ld, off);
    }
    return ld->base + off;
}

static void *nw_ani_new(NwAnimFile *f, size_t size, size_t align, const void *ps2)
{
    return nw_heap_side_at(((NwAniLoad *)f)->side, size, align, ps2);
}

struct nuanimdata_s *NuAnimDataLoadBuff(char *file, union variptr_u *buffer, union variptr_u *buffend)
{
    buffer->charptr = (char *)(((uintptr_t)buffer->charptr + 15) & ~(uintptr_t)15);
    int size = NuFileLoadBuffer(file, buffer->voidptr, (int)(buffend->charptr - buffer->charptr));
    if (!size) {
        NuErrorProlog(nuanim_srcfile, 0x2F8)(
            "Could not read from <%s>, either file is not there or the buffer may be full", file);
    }
    NwAniLoad ld = { .f = { nw_ani_at, nw_ani_new, 1 }, .base = buffer->charptr, .size = (size_t)size,
                     .file = file, .side = nw_heap_side() };
    buffer->charptr += size;
    nw_heap_side_own(ld.side, ld.base, buffer->charptr);
    nw_ps2_nuanimdatahdr_s hdr;
    if (ld.size < sizeof(hdr)) {
        nw_ani_bad(&ld, 0);
    }
    memcpy(&hdr, ld.base, sizeof(hdr));
    if (hdr.version != 1) {
        NuErrorProlog(nuanim_srcfile, 0x2FE)("NuAnimDataLoadBuff: Is in new format, expecting old format.", file);
    }
    if (!isBitCountTable) {
        buildBitCountTable();
    }
    ld.addr = (uint32_t)hdr.address_offset;
    char *data = nw_ani_at(&ld.f, ld.base + offsetof(nw_ps2_nuanimdatahdr_s, data), sizeof(nw_ps2_nuanimdata_s));
    if (!data) {
        nw_ani_bad(&ld, offsetof(nw_ps2_nuanimdatahdr_s, data));
    }
    return nw_anim_data(&ld.f, data);
}
