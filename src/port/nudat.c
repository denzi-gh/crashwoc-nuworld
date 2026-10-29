/* NuDat* (port/units.toml override) on the port's .DAT reader (src/vfs/dat.c): the directory comes from
   nw_dat_parse and lookups from nw_dat_find, the archive's bytes through the kept nufile layer on ndh->fh,
   so the device, the working directory and the buffering stay retail. Everything a caller can see is
   retail's (decomp src/nucore/nufile.c), quirks included; the one difference is nw_dat_find's: the root's
   back link is a miss, so a nonsense path like "ats\ats\x" no longer resolves. An entry an --overlay replaces
   (vfs/vfs.h) is read from the overlay's file, its size in the directory the loose file's, unpacked, and without
   the hardware speed's disc timing. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "nu_types.h"
#include "nucore/nufile.h"
#include "shim/shim.h"
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
    SDL_IOStream *loose; /* an overlaid entry's file */
} NwDatFile;

typedef struct NwDatLoose {
    int index;
    char *path;
} NwDatLoose;

typedef struct NwNuDat {
    struct nudathdr_s *ndh; /* NULL: free */
    NwDat dat;
    NwDatLoose *loose; /* the entries overlays replace */
    int loose_count;
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

typedef struct NwNuDatOverlay {
    NwNuDat *slot;
    struct nudathdr_s *ndh;
} NwNuDatOverlay;

static void nw_nudat_overlay_entry(void *user, const char *rest, const char *path)
{
    NwNuDatOverlay *o = user;
    char entry[NW_VFS_PATH_MAX];
    SDL_strlcpy(entry, rest, sizeof(entry));
    for (char *c = entry; *c; c++) {
        *c = *c == '/' ? '\\' : *c;
    }
    int ix = nw_dat_find(&o->slot->dat, entry);
    SDL_PathInfo info;
    if (ix < 0 || !SDL_GetPathInfo(path, &info)) {
        return; /* nw_vfs_overlay checked them when it mounted the overlay */
    }
    NwDatLoose *loose = SDL_realloc(o->slot->loose, (size_t)(o->slot->loose_count + 1) * sizeof(*loose));
    char *copy = loose ? SDL_strdup(path) : NULL;
    if (loose) {
        o->slot->loose = loose;
    }
    if (!copy) {
        return;
    }
    loose[o->slot->loose_count++] = (NwDatLoose){ ix, copy };
    int *f = &o->ndh->data[2 + 4 * ix];
    f[1] = f[2] = (int)info.size; /* flen, uplen */
    f[3] = 0;                     /* ppack */
}

/* The entries the overlays replace, by the archive's key ("ATS.DAT/"...). */
static void nw_nudat_overlay(NwNuDat *slot, struct nudathdr_s *ndh, const char *fname)
{
    const NwVfs *vfs = nw_shim_file_vfs();
    NwVfsDevice device;
    char key[NW_VFS_PATH_MAX];
    if (vfs && nw_vfs_map(fname, &device, key) && device == NW_VFS_CDROM &&
        SDL_strlcat(key, "/", sizeof(key)) < sizeof(key)) {
        NwNuDatOverlay o = { slot, ndh };
        nw_vfs_overlay_each(vfs, key, nw_nudat_overlay_entry, &o);
    }
}

static const char *nw_nudat_loose(const NwNuDat *slot, int ix)
{
    for (int i = 0; slot && i < slot->loose_count; i++) {
        if (slot->loose[i].index == ix) {
            return slot->loose[i].path;
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
    nw_nudat_overlay(slot, ndh, fname);
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
        NuDatFileClose(ndh->dfhandle);
    }
    if (slot) {
        nw_dat_close(&slot->dat);
        for (int i = 0; i < slot->loose_count; i++) {
            SDL_free(slot->loose[i].path);
        }
        SDL_free(slot->loose);
        slot->loose = NULL;
        slot->loose_count = 0;
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
            const char *loose = nw_nudat_loose(nw_nudat_find(ndh), ix);
            SDL_IOStream *io = loose ? SDL_IOFromFile(loose, "rb") : NULL;
            if (loose && !io) {
                break;
            }
            nw_datfiles[i] = (NwDatFile){ ndh, io ? 0 : f->foffset, f->flen, f->uplen, f->ppack, 0, io };
            while (!io && NuFileSeek(ndh->fh, f->foffset, 0) < 0) {
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
    const NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    if (df->loose) {
        size_t n = SDL_ReadIO(df->loose, data, (size_t)size);
        return n < (size_t)size && SDL_GetIOStatus(df->loose) == SDL_IO_STATUS_ERROR ? -1 : (int)n;
    }
    return NuFileRead(df->ndh->fh, data, size);
}

int NuDatFileSeek(int fh, int offset, int origin)
{
    const NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    if (df->loose) { /* origin 1, absolute in the archive, is the entry's own for a loose file */
        return (int)SDL_SeekIO(df->loose, origin == 2 ? df->len - offset : offset, SDL_IO_SEEK_SET);
    }
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
    return df->loose ? (int)SDL_TellIO(df->loose) : NuFileSeek(df->ndh->fh, 0, 1) - df->start;
}

int NuDatFileOpenSize(int fh)
{
    return nw_datfiles[fh - NW_NUDAT_HANDLE].len;
}

void NuDatFileClose(int fh)
{
    NwDatFile *df = &nw_datfiles[fh - NW_NUDAT_HANDLE];
    if (df->loose) {
        SDL_CloseIO(df->loose);
        df->loose = NULL;
    }
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
