/* game/shadcom (port/units.toml override): retail's ShadDataLoad with the table and the entries, which hold a
   material pointer, as host records beside the caller's buffer (src/port/heap.h; the native textures stay in the file,
   where their TexList slots point), and ShadRndr's quad as a record with its palette as a CLUT. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/rndr.h"
#include "nw_convert.h"
#include "port/heap.h"

#define shadcom_srcfile ".\\shadcom.c"
#define NW_SHD_TEX 0x10 /* the native texture after an entry's header */

/* Declared by no header. */
int NuFileSize(char *fname);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
extern u32 shadcol;
extern s32 palids[4];

typedef struct NwShdLoad {
    char *base;
    size_t size;
    const char *file;
} NwShdLoad;

static _Noreturn void nw_shd_bad(const NwShdLoad *ld, const char *what, size_t off)
{
    NuErrorProlog(shadcom_srcfile, 0x12B)("shadow data <%s>: %s at 0x%x outside the file", ld->file, what,
                                          (unsigned)off);
    for (;;) {
    }
}

static int32_t nw_shd_i32(const NwShdLoad *ld, size_t off)
{
    int32_t v;
    if (off > ld->size - 4) {
        nw_shd_bad(ld, "table", off);
    }
    memcpy(&v, ld->base + off, 4);
    return v;
}

struct shaddata_s *ShadDataLoad(char *file, union variptr_u *buffer)
{
    int size = NuFileSize(file);
    if (size <= 0) {
        return NULL;
    }
    if (!buffer) {
        /* Retail would add materials at a null-based &mtls[i]; its only caller passes the superbuffer. */
        NuErrorProlog(shadcom_srcfile, 0x12B)("shadow data <%s>: no buffer", file);
    }
    buffer->intaddr = (buffer->intaddr + 0xF) & ~(nuintptr)0xF;
    size = NuFileLoadBuffer(file, buffer->voidptr, 0x7FFFFFFF);
    NwShdLoad ld = { .base = buffer->charptr, .size = (size_t)size, .file = file };
    buffer->u8 += size;
    int16_t count;
    if (ld.size < 4 || (memcpy(&count, ld.base, 2), count < 0) || (ld.size - 4) / 8 < (size_t)count) {
        nw_shd_bad(&ld, "table", 0);
    }
    /* The material block retail zeroes and never uses: its mtls != 0 test is inverted. */
    buffer->intaddr = (buffer->intaddr + 0xF) & ~(nuintptr)0xF;
    memset(buffer->voidptr, 0, count * sizeof(nw_ps2_numtl_s));
    buffer->u8 += count * sizeof(nw_ps2_numtl_s);
    NwHeapSide *side = nw_heap_side();
    nw_heap_side_own(side, ld.base, buffer->charptr);
    size_t table = offsetof(struct shaddata_s, table) + 2 * (size_t)count * sizeof(union shadslot_u);
    struct shaddata_s *data = nw_heap_side_at(side, table, _Alignof(struct shaddata_s), ld.base);
    data->count = count;
    data->external = 1;
    for (int i = 0; i < count; i++) {
        int32_t at = nw_shd_i32(&ld, 4 + 4 * (size_t)i), name = nw_shd_i32(&ld, 4 + 4 * (size_t)(count + i));
        if (at < 0 || ld.size < NW_SHD_TEX + 16 || (size_t)at > ld.size - NW_SHD_TEX - 16) {
            nw_shd_bad(&ld, "entry", (size_t)at);
        }
        if (name < 0 || (size_t)name >= ld.size ||
            SDL_strnlen(ld.base + name, ld.size - (size_t)name) == ld.size - (size_t)name) {
            nw_shd_bad(&ld, "name", (size_t)name);
        }
        struct shadentry_s *entry = nw_heap_side_at(side, sizeof(*entry), _Alignof(struct shadentry_s), ld.base + at);
        memcpy(&entry->xframes, ld.base + at, 2);
        memcpy(&entry->yframes, ld.base + at + 2, 2);
        memcpy(&entry->pad04, ld.base + at + 4, 4);
        entry->tid = NuTexCreateNative(ld.base + at + NW_SHD_TEX);
        if (entry->tid != 0) {
            entry->mtl = NuMtlCreate(1);
            entry->mtl->diffuse.r = 1.0f;
            entry->mtl->diffuse.g = 1.0f;
            entry->mtl->diffuse.b = 1.0f;
            entry->mtl->attrib.cull = 2;
            entry->mtl->attrib.zmode = 0;
            entry->mtl->alpha = 0.999f;
            entry->mtl->attrib.alpha = 3;
            entry->mtl->attrib.filter = 1;
            entry->mtl->tid = entry->tid;
            entry->mtl->sort_pri = 100;
            NuMtlUpdate(entry->mtl);
        }
        data->table[i].entry = entry;
        data->table[count + i].name = ld.base + name;
    }
    return data;
}

/* The frame's cell of the atlas, whose 4-bit texels hold four frames in their bits: palids[frame & 3] shows one bit
   of them, put over the material texture's CLUT where retail uploads it into the stream. */
void ShadRndr(struct numtx_s *mtx, struct shadentry_s *shad, f32 frame, f32 scale)
{
    static const float corner[4][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
    struct nuvtx_tc1_s vtx[4];
    struct numtx_s m;
    if (shad == NULL || shad->mtl == NULL) {
        return;
    }
    int last = (s32)frame - 1;
    int n = shad->xframes * shad->yframes * 4;
    f32 du = 1.0f / shad->xframes;
    f32 dv = 1.0f / shad->yframes;
    if (last < n) {
        n = last;
    }
    if (n < 0) {
        n = 0;
    }
    int pal = n & 3;
    n >>= 2;
    int col = n % shad->xframes, row = n / shad->xframes;
    memset(vtx, 0, sizeof(vtx));
    for (int k = 0; k < 4; k++) {
        vtx[k].pnt.x = corner[k][0];
        vtx[k].pnt.z = corner[k][1];
        vtx[k].diffuse = (int)shadcol;
        vtx[k].tc[0] = (col + (k & 1)) * du;
        vtx[k].tc[1] = (row + (k >> 1)) * dv;
    }
    m = *mtx;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            (&m._00)[r * 4 + c] *= scale;
        }
    }
    struct rndrstream_s *rs = &shad->mtl->rs;
    NuRndrStreamAddMtl(rs, shad->mtl);
    NwCmd *c = nw_rndr_strip(vtx, 4, &m, rs);
    c->clut = palids[pal] ? nw_rndr_texture(palids[pal]) : NULL;
}
