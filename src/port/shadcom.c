/* game/shadcom's .shd loader (port/units.toml override): retail's ShadDataLoad with the table and the entries, which
   hold a material pointer, as host records after the file. The native textures stay in the file, where their
   TexList slots point. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nw_convert.h"

#define shadcom_srcfile ".\\shadcom.c"
#define NW_SHD_TEX 0x10 /* the native texture after an entry's header */

/* Declared by no header. */
int NuFileSize(char *fname);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);

typedef struct NwShdLoad {
    char *base;
    size_t size;
    const char *file;
    union variptr_u *buffer;
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

/* Zeroed host records off the caller's buffer. */
static void *nw_shd_new(NwShdLoad *ld, size_t size, size_t align)
{
    union variptr_u *b = ld->buffer;
    b->intaddr = (b->intaddr + align - 1) & ~(nuintptr)(align - 1);
    void *p = b->voidptr;
    memset(p, 0, size);
    b->u8 += size;
    return p;
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
    NwShdLoad ld = { .base = buffer->charptr, .size = (size_t)size, .file = file, .buffer = buffer };
    buffer->u8 += size;
    int16_t count;
    if (ld.size < 4 || (memcpy(&count, ld.base, 2), count < 0) || (ld.size - 4) / 8 < (size_t)count) {
        nw_shd_bad(&ld, "table", 0);
    }
    /* The material block retail zeroes and never uses: its mtls != 0 test is inverted. */
    nw_shd_new(&ld, count * sizeof(struct numtl_s), 16);
    size_t table = offsetof(struct shaddata_s, table) + 2 * (size_t)count * sizeof(union shadslot_u);
    struct shaddata_s *data = nw_shd_new(&ld, table, _Alignof(struct shaddata_s));
    struct shadentry_s *entries = nw_shd_new(&ld, count * sizeof(struct shadentry_s), _Alignof(struct shadentry_s));
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
        struct shadentry_s *entry = &entries[i];
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
