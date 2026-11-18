#include "port/disc.h"

#include <SDL3/SDL.h>

#include "data/image.h"
#include "game/sfx.h"
#include "shim/shim.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

/* Declared by no header: the global table and the level's, as edbits looks sounds up in them. */
extern GameSound SfxTabGLOBAL[];
extern GameSound *edSfxLevelTab;
extern int edSfxGlobalCount;

typedef struct NwDiscSfxRun {
    uint32_t table; /* a PAL sound table's address */
    int rows;       /* the disc ELF's rows of it */
    int first, pal, count;
} NwDiscSfxRun;

#define NW_SFX_RUN(name, table, rows, first, pal, count) { table, rows, first, pal, count },
static const NwDiscSfxRun nw_disc_sfx_usa[] = {
#include "port/sfx_table.inc"
};
#undef NW_SFX_RUN

/* The discs whose files number sounds by their own ELF's tables. */
static const struct {
    const NwManifest *disc;
    const NwDiscSfxRun *runs;
    size_t count;
} nw_disc_sfx_tables[] = {
    { &nw_disc_manifest_usa100, nw_disc_sfx_usa, SDL_arraysize(nw_disc_sfx_usa) },
};

static const NwManifest *nw_disc(void)
{
    const NwVfs *vfs = nw_shim_file_vfs();
    return vfs ? vfs->manifest : NULL;
}

bool nw_disc_ntsc(void)
{
    const NwManifest *m = nw_disc();
    return m && m->vmode && SDL_strcmp(m->vmode, "NTSC") == 0;
}

static int nw_disc_sfx_row(const NwDiscSfxRun *runs, size_t n, int sfx)
{
    int global = -1;
    for (size_t i = 0; i < n; i++) {
        if (nw_image_host(runs[i].table) == SfxTabGLOBAL) {
            global = runs[i].rows;
            break;
        }
    }
    if (global < 0) {
        return -1;
    }
    const GameSound *tab = sfx < global ? SfxTabGLOBAL : edSfxLevelTab;
    int row = sfx < global ? sfx : sfx - global;
    for (size_t i = 0; tab && i < n; i++) {
        const NwDiscSfxRun *r = &runs[i];
        if (nw_image_host(r->table) == tab && row >= r->first && row < r->first + r->count) {
            return (tab == SfxTabGLOBAL ? 0 : edSfxGlobalCount) + r->pal + row - r->first;
        }
    }
    return -1;
}

int nw_disc_sfx(int sfx)
{
    const NwManifest *m = nw_disc();
    for (size_t i = 0; sfx >= 0 && i < SDL_arraysize(nw_disc_sfx_tables); i++) {
        if (m == nw_disc_sfx_tables[i].disc) {
            return nw_disc_sfx_row(nw_disc_sfx_tables[i].runs, nw_disc_sfx_tables[i].count, sfx);
        }
    }
    return sfx;
}
