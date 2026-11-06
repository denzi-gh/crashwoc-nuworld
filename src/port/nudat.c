/* Overrides NuDat* and NuPPLoadBuffer (port/units.toml) on the port's .DAT reader (src/vfs/dat.c), reading through the
   kept nufile layer, so callers see retail's behaviour, quirks included. Differences: the root's back link is a miss
   ("ats\ats\x" no longer resolves), an --overlay entry is read unpacked from its loose file, without the disc timing,
   and every load stays inside its buffer: an archive's header and an RNC file's sizes are checked, RNC streams unpack
   bounded (src/vfs/rnc.c), and NuFileLoadBuffer's size is capped at its buffer's room (NW_LOAD_ROOM). */
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "data/image.h"
#include "nu_types.h"
#include "nucore/nuerror.h"
#include "nucore/nufile.h"
#include "port/heap.h"
#include "shim/shim.h"
#include "vfs/dat.h"
#include "vfs/rnc.h"

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
extern int nufile_buffering_enabled;

/* The file NuFileLoadBuffer loads, for the errors of the loads below. */
static char nw_load_name[NW_VFS_PATH_MAX];

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
    if (info.size > INT_MAX) { /* NuFile sizes are ints */
        SDL_Log("overlay: %s: too large, the archive's entry stays", path);
        return;
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
    int got;
    while ((got = NuFileRead(fh, hdr, sizeof(hdr))) < 0) {
        while (NuFileSeek(fh, 0, 0) < 0) {
        }
    }
    nw_nudat_wait(fh, mode);
    /* retail retries the seek until it succeeds: an offset past the archive would spin forever */
    int total = NuFileOpenSize(fh);
    bool fits = got == (int)sizeof(hdr) && hdr[0] >= (int)sizeof(hdr) && hdr[0] <= total && hdr[1] > 0 &&
                hdr[1] <= total - hdr[0] && (unsigned)hdr[1] <= NW_DAT_DIR_MAX;
    if (!fits) {
        fprintf(stderr, "NuDatOpenEx: %s: not an archive (header %d bytes, directory at %d of %d)\n", fname, got,
                got >= 4 ? hdr[0] : -1, total);
    }
    while (fits && NuFileSeek(fh, hdr[0], 0) < 0) {
    }
    nw_nudat_wait(fh, mode);

    NwNuDat *slot = nw_nudat_find(NULL);
    unsigned char *dir = slot && fits ? malloc((size_t)hdr[1]) : NULL;
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

typedef int (*NwLoadRead)(int fh, void *data, int size);
typedef int (*NwLoadSeek)(int fh, int offset, int origin);

static _Noreturn void nw_load_fail(const char *what, unsigned a, unsigned b)
{
    NuErrorProlog("nudat.c", __LINE__)("%s: %s (%u, %u)", nw_load_name, what, a, b);
    for (;;) {
    }
}

/* The RNC file of total bytes at fh unpacked into dest, its unpacked size at most maxsize; fatal when its sizes do not
   fit or its stream is damaged. Retail reads bytes 4.. to end at dest + unpacked + 0x100 and unpacks in place; that
   layout stays when it fits maxsize, else the packed bytes are read beside the buffer. */
static int nw_load_packed(int fh, NwLoadRead read, NwLoadSeek seek, int total, unsigned char *dest, int maxsize)
{
    uint8_t hdr[NW_RNC_HEADER];
    uint32_t unpacked, packed;
    if (total < NW_RNC_HEADER || seek(fh, 0, 0) < 0 || read(fh, hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        !nw_rnc_header(hdr, sizeof(hdr), &unpacked, &packed)) {
        nw_load_fail("not an RNC file", (unsigned)total, 0);
    }
    if (packed > (uint32_t)total - NW_RNC_HEADER) {
        nw_load_fail("packed bytes past the file's end", packed, (unsigned)total);
    }
    if (maxsize < 0 || unpacked > (uint32_t)maxsize) {
        nw_load_fail("unpacked size larger than its buffer", unpacked, (unsigned)maxsize);
    }
    size_t length = (size_t)packed + NW_RNC_HEADER;
    unsigned char *at, *beside = NULL;
    if ((size_t)unpacked + 0x100 >= length && (size_t)unpacked + 0xFC <= (size_t)maxsize) {
        at = dest + unpacked + 0x100 - length;
    } else if (!(at = beside = malloc(length - 4))) {
        nw_load_fail("out of memory for the packed bytes", (unsigned)length, 0);
    }
    if (seek(fh, 4, 0) < 0 || read(fh, at, (int)(length - 4)) != (int)(length - 4)) {
        free(beside);
        nw_load_fail("read failed", (unsigned)length, (unsigned)total);
    }
    int64_t n = nw_rnc_unpack(at + NW_RNC_HEADER - 4, at + length - 4, dest, dest + unpacked);
    free(beside);
    if (n != (int64_t)unpacked) {
        nw_load_fail("damaged RNC stream", n < 0 ? 0 : (unsigned)n, unpacked);
    }
    return (int)unpacked;
}

/* nucore/rnc's NuPPLoadBuffer: retail's, its packed branch bounded (nw_load_packed). */
int NuPPLoadBuffer(int fh, unsigned char *dest, int maxsize)
{
    int total = NuFileOpenSize(fh);
    uint8_t id[4];
    if (total >= NW_RNC_HEADER && NuFileSeek(fh, 0, 0) >= 0 && NuFileRead(fh, id, sizeof(id)) == (int)sizeof(id) &&
        id[0] == 'R' && id[1] == 'N' && id[2] == 'C' && id[3] == 2) {
        return nw_load_packed(fh, NuFileRead, NuFileSeek, total, dest, maxsize);
    }
    if (total < 0 || total > maxsize) {
        nw_load_fail("larger than its buffer", (unsigned)total, (unsigned)maxsize);
    }
    if (NuFileSeek(fh, 0, 0) < 0 || NuFileRead(fh, dest, total) < 0) {
        nw_load_fail("read failed", (unsigned)total, 0);
    }
    return total;
}

int nw_load_room(const char *file, const void *mem, int size)
{
    size_t room;
    SDL_strlcpy(nw_load_name, file ? file : "", sizeof(nw_load_name));
    if (size > 0 && (nw_load_buffer_room(mem, &room) || nw_image_room(mem, &room)) && room < (size_t)size) {
        return (int)room;
    }
    return size;
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
        int size = df->uplen;
        if (size > 0 && size <= maxsize) {
            SDL_strlcpy(nw_load_name, fname, sizeof(nw_load_name));
            if (nw_load_packed(fh, NuDatFileRead, NuDatFileSeek, df->len, dest, maxsize) != size) {
                nw_load_fail("unpacked size differs from the archive's", 0, (unsigned)size);
            }
            nw_nudat_wait(ndh->fh, ndh->openmode);
            NuFileClose(fh);
            return size;
        }
        SDL_Log("NuDatFileLoadBuffer: %s: %d bytes do not fit its buffer of %d", fname, size, maxsize);
    } else {
        int size = df->len;
        if (maxsize >= size && size > 0) {
            while (NuDatFileRead(fh, dest, size) < 0) {
                while (NuDatFileSeek(fh, 0, 0) < 0) {
                }
            }
            nw_nudat_wait(ndh->fh, ndh->openmode);
            NuFileClose(fh);
            return size;
        }
        if (size > maxsize) {
            SDL_Log("NuDatFileLoadBuffer: %s: %d bytes do not fit its buffer of %d", fname, size, maxsize);
        }
    }
    NuFileClose(fh);
    return 0;
}
