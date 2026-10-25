/* gamelib/terrain's .ter loader (port/units.toml override): retail's ReadTerrain, with each linked wall spline copied
   after the file, so that its link to the previous one holds a host pointer instead of the file's 4-byte slot. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nw_convert.h"

#define terrain_srcfile "..\\nu2crash.ps2\\gamelib\\terrain.c"
#define NW_TER_LINK 0x12345678 /* a wall spline ReadTerrain links */
#define NW_TER_SITUS 0x800     /* terraininit's terr[] */

/* Records the host reads in place from the file. */
_Static_assert(sizeof(OffType) == sizeof(nw_ps2_OffType), "OffType");
_Static_assert(sizeof(TerrainGroupType) == sizeof(nw_ps2_TerrainGroupType), "TerrainGroupType");
_Static_assert(sizeof(tertype) == sizeof(nw_ps2_tertype), "tertype");
_Static_assert(offsetof(WallSplineType, spl) == offsetof(nw_ps2_WallSplineType, spl), "WallSplineType");

/* Declared by no header. */
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
extern void *crashdata;

typedef struct NwTerLoad {
    char *base;
    size_t size;
    const char *file;
} NwTerLoad;

static _Noreturn void nw_ter_bad(const NwTerLoad *ld, const char *what, size_t off)
{
    NuErrorProlog(terrain_srcfile, 0x313)("terrain <%s>: %s at 0x%x outside the file", ld->file, what, (unsigned)off);
    for (;;) {
    }
}

static int32_t nw_ter_i32(const NwTerLoad *ld, size_t off, const char *what)
{
    int32_t v;
    if (ld->size < 4 || off > ld->size - 4) {
        nw_ter_bad(ld, what, off);
    }
    memcpy(&v, ld->base + off, 4);
    return v;
}

/* A terrain object: groups of triangles up to one whose start is negative, all inside its record. */
static void nw_ter_model(const NwTerLoad *ld, size_t at, size_t end)
{
    for (;;) {
        int16_t start, count;
        if (end - at < 2) {
            nw_ter_bad(ld, "triangle group", at);
        }
        memcpy(&start, ld->base + at, 2);
        if (start < 0) {
            return;
        }
        memcpy(&count, ld->base + at + 2, 2);
        if (count < 0 || end - at < sizeof(TerrainGroupType) + (size_t)count * sizeof(tertype)) {
            nw_ter_bad(ld, "triangle group", at);
        }
        at += sizeof(TerrainGroupType) + (size_t)count * sizeof(tertype);
    }
}

s32 ReadTerrain(char *name, s32 situ, short **store, TerrainSetType *terrset)
{
    char filename[100];
    strcpy(filename, name);
    strcat(filename, ".ter");
    int size = NuFileLoadBuffer(filename, *store, 0x7FFFFFFF);
    if (size == 0) {
        return -1;
    }
    NwTerLoad ld = { .base = (char *)*store, .size = (size_t)size, .file = filename };
    int32_t dir = nw_ter_i32(&ld, 0, "directory");
    int32_t n = dir < 2 ? -1 : nw_ter_i32(&ld, 2 * (size_t)dir, "directory");
    if (n < 0 || n > 512 || (ld.size - 2 * (size_t)dir - 4) / sizeof(OffType) < (size_t)n) {
        nw_ter_bad(&ld, "directory", 2 * (size_t)dir);
    }
    const char *offlist = ld.base + 2 * (size_t)dir + 4;
    char *copies = (char *)(((uintptr_t)ld.base + ld.size + 7) & ~(uintptr_t)7), *end = copies;
    size_t at = 4;
    s32 count = 0;
    terrset->wallinfo = 0;
    crashdata = 0;
    for (int32_t lp = 0; lp < n; lp++) {
        OffType off;
        memcpy(&off, offlist + lp * sizeof(OffType), sizeof(off));
        size_t len = 2 * (size_t)off.offset;
        if (off.offset < 0 || len > ld.size - at || (at & 3)) {
            nw_ter_bad(&ld, "record", at);
        }
        switch (off.type) {
        case 0:
        case 1: {
            if (situ < 0 || situ >= NW_TER_SITUS) {
                nw_ter_bad(&ld, "terrain object past terr[0x800]", at);
            }
            nw_ter_model(&ld, at, at + len);
            TerrainSituType *t = &terrset->terr[situ];
            t->id = off.id;
            t->type = off.type;
            t->info = off.info;
            t->Location = off.translation;
            t->model = (s16 *)(ld.base + at);
            t->flags = off.flags;
            count++;
            situ++;
            break;
        }
        case 2:
            if (len >= 4 && nw_ter_i32(&ld, at, "wall spline") == NW_TER_LINK) {
                uint16_t points;
                if (len < 8 || (memcpy(&points, ld.base + at + 4, 2), points == 0) ||
                    len - 4 < offsetof(WallSplineType, spl) + points * sizeof(struct nuvec_s)) {
                    nw_ter_bad(&ld, "wall spline", at);
                }
                end = (char *)(((uintptr_t)end + 7) & ~(uintptr_t)7);
                memcpy(end, &terrset->wallinfo, sizeof(void *));
                WallSplineType *w = (WallSplineType *)(end + sizeof(void *));
                memcpy(w, ld.base + at + 4, len - 4);
                end += sizeof(void *) + (len - 4);
                terrset->wallinfo = w;
                w->count--;
            }
            break;
        case 3:
            crashdata = ld.base + at;
            break;
        }
        at += len;
    }
    *store = (short *)(end > copies ? end : ld.base + at);
    for (WallSplineType *w = terrset->wallinfo; w; w = ((WallSplineType **)w)[-1]) {
        for (int a = 0; a < w->count; a += 16) {
            if (w->spl[a].y == 2147483647.0f) {
                continue;
            }
            if (a + 15 < w->count) {
                float minx = w->spl[a].x, maxx = minx, minz = w->spl[a].z, maxz = minz;
                for (int b = a + 1; b <= a + 16; b++) {
                    minx = w->spl[b].x < minx ? w->spl[b].x : minx;
                    maxx = w->spl[b].x > maxx ? w->spl[b].x : maxx;
                    minz = w->spl[b].z < minz ? w->spl[b].z : minz;
                    maxz = w->spl[b].z > maxz ? w->spl[b].z : maxz;
                }
                w->spl[a].y = minx;
                w->spl[a + 1].y = maxx;
                w->spl[a + 2].y = minz;
                w->spl[a + 3].y = maxz;
            } else {
                w->spl[a].y = 2147483647.0f;
            }
        }
    }
    return count;
}
