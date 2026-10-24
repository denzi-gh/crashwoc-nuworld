/* NuDat* (port/units.toml override) on the port's .DAT reader (src/vfs/dat.c): the directory comes from
   nw_dat_parse and lookups from nw_dat_find, the archive's bytes through the kept nufile layer on ndh->fh,
   so the device, the working directory and the buffering stay retail. Everything a caller can see is
   retail's (decomp src/nucore/nufile.c), quirks included; the one difference is nw_dat_find's: the root's
   back link is a miss, so a nonsense path like "ats\ats\x" no longer resolves. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "nu_types.h"
#include "nucore/nufile.h"
#include "vfs/dat.h"

#define NW_NUDAT_FILES 20     /* datfiles[20] */
#define NW_NUDAT_ARCHIVES 8   /* open at once; retail opens one at a time */
#define NW_NUDAT_HANDLE 0x800 /* entry handles are 0x800 + slot */

/* The kept nufile layer, numem and rnc (no header declares them). */
int NuFileOpen(char *file, enum nufilemode_e mode);
int NuFileRead(int fh, void *data, int size);
int NuFileSeek(int fh, int offset, int origin);
int NuFileStatus(int fh);
int NuFileOpenSize(int fh);
void NuFileClose(int fh);
void *NuMemAllocFn(int size, char *file, int line);
void NuMemFreeFn(void *ptr, char *file, int line);
void NuPPUnpack(unsigned char *src, unsigned char *out);
extern int nufile_buffering_enabled;

/* datfiles[]: one open entry of an archive. */
typedef struct NwDatFile {
    struct nudathdr_s *ndh;
    int start, len, uplen, ppack;
    int used;
} NwDatFile;

typedef struct NwNuDat {
    struct nudathdr_s *ndh; /* NULL: free */
    NwDat dat;
} NwNuDat;

static NwDatFile nw_datfiles[NW_NUDAT_FILES];
static NwNuDat nw_nudats[NW_NUDAT_ARCHIVES];

static NwNuDat *nw_nudat_find(const struct nudathdr_s *ndh)
{
    for (int i = 0; i < NW_NUDAT_ARCHIVES; i++) {
        if (nw_nudats[i].ndh == ndh) {
            return &nw_nudats[i];
        }
    }
    return NULL;
}

static void nw_nudat_wait(int fh, int mode)
{
    if (mode == NUFILE_READ_NOWAIT) {
        while (NuFileStatus(fh) != 0) {
        }
    }
}

struct nudathdr_s *NuDatOpenEx(char *fname, char **mem, int *used, short mode)
{
    int saved_buffering = nufile_buffering_enabled;
    int hdr[2];
    struct nudathdr_s *ndh = NULL;

    int fh = NuFileOpen(fname, mode);
    if (fh == 0) {
        nufile_buffering_enabled = saved_buffering;
        return NULL;
    }
    nw_nudat_wait(fh, mode);
    while (NuFileRead(fh, hdr, sizeof(hdr)) < 0) {
        while (NuFileSeek(fh, 0, 0) < 0) {
        }
    }
    nw_nudat_wait(fh, mode);
    while (NuFileSeek(fh, hdr[0], 0) < 0) {
    }
    nw_nudat_wait(fh, mode);

    NwNuDat *slot = nw_nudat_find(NULL);
    unsigned char *dir = slot && hdr[1] > 0 ? malloc((size_t)hdr[1]) : NULL;
    if (dir) {
        while (NuFileRead(fh, dir, hdr[1]) < 0) {
            while (NuFileSeek(fh, hdr[0], 0) < 0) {
            }
        }
        nw_nudat_wait(fh, mode);
        if (!nw_dat_parse(&slot->dat, dir, (size_t)hdr[1], (uint64_t)NuFileOpenSize(fh))) {
            fprintf(stderr, "NuDatOpenEx: %s: %s\n", fname, SDL_GetError());
        } else {
            /* header | data[0..1] = { directory magic, nfiles } | finfo[nfiles]: finfo[-1].uplen is the
               magic (-1), which NuFileSize returns for a miss */
            int size = (int)(offsetof(struct nudathdr_s, data) + (2 + 4 * (size_t)slot->dat.count) * sizeof(int));
            if (mem) {
                ndh = (struct nudathdr_s *)*mem;
                *mem += size;
                if (used) {
                    *used = size;
                }
                ndh->intalloc = 0;
            } else {
                ndh = NuMemAllocFn(size, "nudat.c", __LINE__);
                if (ndh) {
                    ndh->intalloc = 1;
                }
            }
        }
    }
    if (!ndh) {
        free(dir);
        if (slot) {
            nw_dat_close(&slot->dat);
        }
        NuFileClose(fh);
        nufile_buffering_enabled = saved_buffering;
        return NULL;
    }
    memcpy(&ndh->data[0], dir, 2 * sizeof(int));
    for (uint32_t i = 0; i < slot->dat.count; i++) {
        const NwDatEntry *e = &slot->dat.entries[i];
        int *f = &ndh->data[2 + 4 * i];
        f[0] = (int)e->offset;
        f[1] = (int)e->size;
        f[2] = (int)e->unpacked_size;
        f[3] = (int)e->flags;
    }
    free(dir);
    ndh->ver = ndh->data[0];
    ndh->nfiles = ndh->data[1];
    ndh->finfo = (struct NUDATFINFO_s *)&ndh->data[2];
    ndh->treecnt = (int)slot->dat.node_count;
    ndh->filetree = NULL; /* the port's lookups use slot->dat */
    ndh->leafnamesize = 0;
    ndh->leafnames = slot->dat.names;
    ndh->fh = fh;
    ndh->openmode = mode;
    ndh->dfhandle = 0;
    slot->ndh = ndh;
    nufile_buffering_enabled = saved_buffering;
    return ndh;
}

void NuDatClose(struct nudathdr_s *ndh)
{
    NwNuDat *slot = nw_nudat_find(ndh);
    NuFileClose(ndh->fh);
    if (ndh->dfhandle != 0) {
        nw_datfiles[ndh->dfhandle - NW_NUDAT_HANDLE].used = 0;
    }
    if (slot) {
        nw_dat_close(&slot->dat);
        slot->ndh = NULL;
    }
    if (ndh->intalloc) {
        NuMemFreeFn(ndh, "nudat.c", __LINE__);
    }
}

int NuDatFileFindTree(struct nudathdr_s *ndh, char *fname)
{
    NwNuDat *slot = nw_nudat_find(ndh);
    return slot ? nw_dat_find(&slot->dat, fname) : -1;
}

int NuDatFileOpen(struct nudathdr_s *ndh, char *fname, enum nufilemode_e mode)
{
    if (mode != NUFILE_READ || ndh->dfhandle != 0) {
        return 0;
    }
    for (int i = 0; i < NW_NUDAT_FILES; i++) {
        if (nw_datfiles[i].used == 0) {
            int ix = NuDatFileFindTree(ndh, fname);
            if (ix < 0) {
                break;
            }
            const struct NUDATFINFO_s *f = &ndh->finfo[ix];
            nw_datfiles[i] = (NwDatFile){ ndh, f->foffset, f->flen, f->uplen, f->ppack, 0 };
            while (NuFileSeek(ndh->fh, f->foffset, 0) < 0) {
            }
            nw_datfiles[i].used = 1;
            ndh->dfhandle = NW_NUDAT_HANDLE + i;
            return ndh->dfhandle;
        }
    }
    return 0;
}

int NuDatFileRead(int fh, void *data, int size)
{
    return NuFileRead(nw_datfiles[fh - NW_NUDAT_HANDLE].ndh->fh, data, size);
}

int NuDatFileSeek(int fh, int offset, int origin)
{
    const NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    switch (origin) {
    case 1: /* retail: absolute in the archive */
        return NuFileSeek(df->ndh->fh, offset, 0) - df->start;
    case 2:
        return NuFileSeek(df->ndh->fh, df->start + df->len - offset, 0) - df->start;
    default:
        return NuFileSeek(df->ndh->fh, offset + df->start, 0) - df->start;
    }
}

int NuDatFilePos(int fh)
{
    const NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    return NuFileSeek(df->ndh->fh, 0, 1) - df->start;
}

int NuDatFileOpenSize(int fh)
{
    return nw_datfiles[fh - NW_NUDAT_HANDLE].len;
}

void NuDatFileClose(int fh)
{
    NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    df->ndh->dfhandle = 0;
    df->used = 0;
}

int NuDatFileLoadBuffer(struct nudathdr_s *ndh, char *fname, void *dest, int maxsize)
{
    int fh = NuDatFileOpen(ndh, fname, NUFILE_READ);
    if (fh == 0) {
        return 0;
    }
    const NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    nw_nudat_wait(ndh->fh, ndh->openmode);
    if (df->ppack) {
        int size = df->uplen, packed_len = df->len;
        unsigned char *packed = (unsigned char *)dest + size + 0x100 - packed_len;
        if (maxsize >= size + 0x100 && size != 0) {
            while (NuDatFileRead(fh, packed, packed_len) < 0) {
                while (NuDatFileSeek(fh, 0, 0) < 0) {
                }
            }
            nw_nudat_wait(ndh->fh, ndh->openmode);
            NuFileClose(fh);
            NuPPUnpack(packed, dest);
            return size;
        }
    } else {
        int size = df->len;
        if (maxsize >= size && size != 0) {
            while (NuDatFileRead(fh, dest, size) < 0) {
                while (NuDatFileSeek(fh, 0, 0) < 0) {
                }
            }
            nw_nudat_wait(ndh->fh, ndh->openmode);
            NuFileClose(fh);
            return size;
        }
    }
    NuFileClose(fh);
    return 0;
}
