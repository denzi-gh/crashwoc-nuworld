/* A synthetic PS2 disc for the tests and the fuzz seeds (no game data): an ISO9660 image of LSN_END 2048-byte
   sectors with DUMMY.DAT, EMPTY.BIN, SLES_503.86, SYSTEM.CNF and LEVELS/B/HUB/HUB.GSC, and its raw 2352-byte
   Mode 1 / Mode 2 form. */
#ifndef NUWORLD_TESTS_DISC_BUILDER_H
#define NUWORLD_TESTS_DISC_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vfs/disc.h"

#define IMG_SECTORS 40
#define LSN_ROOT 18 /* 2 sectors */
#define LSN_LEVELS 20
#define LSN_B 21
#define LSN_HUB 22
#define LSN_CNF 23
#define LSN_ELF 24 /* 3 sectors */
#define LSN_DUMMY 27 /* 2 sectors */
#define LSN_GSC 29 /* 3 sectors */
#define LSN_END 32

#define ELF_SIZE 5000
#define DUMMY_SIZE 3000
#define GSC_SIZE 4100

static const char pal_cnf[] = "BOOT2 = cdrom0:\\SLES_503.86;1\r\nVER = 1.03\r\nVMODE = PAL\r\n";

typedef struct Spec {
    const char *cnf;
    uint8_t elf_xor; /* flips bits of the ELF's first byte */
    uint8_t gsc_flags;
    uint8_t gsc_unit;
    bool duplicate;
} Spec;

typedef struct Image {
    uint8_t data[IMG_SECTORS * NW_DISC_SECTOR];
    size_t size;
} Image;

typedef struct Dir {
    uint8_t *base;
    size_t pos;
} Dir;

static uint8_t elf_data[ELF_SIZE], gsc_data[GSC_SIZE], dummy_data[DUMMY_SIZE];

static inline void fill(uint8_t *p, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
}

static inline void put_both32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
        p[7 - i] = (uint8_t)(v >> (8 * i));
    }
}

static inline void dir_record(Dir *d, const char *name, size_t nlen, uint32_t lba, uint32_t size, uint8_t flags,
                              uint8_t unit)
{
    size_t len = 33 + nlen + (nlen % 2 == 0);
    if (d->pos % NW_DISC_SECTOR + len > NW_DISC_SECTOR) {
        d->pos = (d->pos / NW_DISC_SECTOR + 1) * NW_DISC_SECTOR;
    }
    uint8_t *r = d->base + d->pos;
    r[0] = (uint8_t)len;
    put_both32(r + 2, lba);
    put_both32(r + 10, size);
    r[25] = flags;
    r[26] = unit;
    r[28] = 1;
    r[32] = (uint8_t)nlen;
    memcpy(r + 33, name, nlen);
    d->pos += len;
}

static inline void dir_file(Dir *d, const char *name, uint32_t lba, uint32_t size)
{
    dir_record(d, name, strlen(name), lba, size, 0, 0);
}

static inline void dir_sub(Dir *d, const char *name, uint32_t lba)
{
    dir_record(d, name, strlen(name), lba, NW_DISC_SECTOR, 2, 0);
}

static inline Dir dir_begin(Image *img, uint32_t lsn, uint32_t parent, uint32_t sectors)
{
    Dir d = { img->data + (size_t)lsn * NW_DISC_SECTOR, 0 };
    dir_record(&d, "\0", 1, lsn, sectors * NW_DISC_SECTOR, 2, 0);
    dir_record(&d, "\1", 1, parent, NW_DISC_SECTOR, 2, 0);
    return d;
}

/* Root: DUMMY.DAT EMPTY.BIN LEVELS/ | (next sector) SLES_503.86 SYSTEM.CNF; LEVELS/B/HUB/HUB.GSC. */
static inline void build_image(Image *img, const Spec *spec)
{
    memset(img, 0, sizeof(*img));
    img->size = (size_t)LSN_END * NW_DISC_SECTOR;
    fill(elf_data, ELF_SIZE, 1);
    fill(gsc_data, GSC_SIZE, 2);
    memset(dummy_data, 0, DUMMY_SIZE);

    uint8_t *pvd = img->data + 16 * NW_DISC_SECTOR;
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    memset(pvd + 40, ' ', 32);
    memcpy(pvd + 40, "NWTEST", 6);
    pvd[128] = 0x00, pvd[129] = 0x08, pvd[130] = 0x08, pvd[131] = 0x00;
    Dir pvd_root = { pvd + 156, 0 };
    dir_record(&pvd_root, "\0", 1, LSN_ROOT, 2 * NW_DISC_SECTOR, 2, 0);
    uint8_t *term = img->data + 17 * NW_DISC_SECTOR;
    term[0] = 255;
    memcpy(term + 1, "CD001", 5);
    term[6] = 1;

    Dir root = dir_begin(img, LSN_ROOT, LSN_ROOT, 2);
    dir_file(&root, "DUMMY.DAT;1", LSN_DUMMY, DUMMY_SIZE);
    dir_file(&root, "EMPTY.BIN;1", 0, 0);
    dir_sub(&root, "LEVELS", LSN_LEVELS);
    root.pos = NW_DISC_SECTOR; /* zero padding to the end of the first sector */
    dir_file(&root, "SLES_503.86;1", LSN_ELF, ELF_SIZE);
    dir_file(&root, "SYSTEM.CNF;1", LSN_CNF, (uint32_t)strlen(spec->cnf));
    if (spec->duplicate) {
        dir_file(&root, "SYSTEM.CNF;1", LSN_CNF, (uint32_t)strlen(spec->cnf));
    }
    Dir levels = dir_begin(img, LSN_LEVELS, LSN_ROOT, 1);
    dir_sub(&levels, "B", LSN_B);
    Dir b = dir_begin(img, LSN_B, LSN_LEVELS, 1);
    dir_sub(&b, "HUB", LSN_HUB);
    Dir hub = dir_begin(img, LSN_HUB, LSN_B, 1);
    dir_record(&hub, "HUB.GSC;1", 9, LSN_GSC, GSC_SIZE, spec->gsc_flags, spec->gsc_unit);

    memcpy(img->data + LSN_CNF * NW_DISC_SECTOR, spec->cnf, strlen(spec->cnf));
    memcpy(img->data + LSN_ELF * NW_DISC_SECTOR, elf_data, ELF_SIZE);
    img->data[LSN_ELF * NW_DISC_SECTOR] ^= spec->elf_xor;
    memcpy(img->data + LSN_DUMMY * NW_DISC_SECTOR, dummy_data, DUMMY_SIZE);
    memcpy(img->data + LSN_GSC * NW_DISC_SECTOR, gsc_data, GSC_SIZE);
}

static inline uint8_t bcd(unsigned v)
{
    return (uint8_t)(v / 10 * 16 + v % 10);
}

/* 2352-byte sectors; form2_lsn marks one sector as Mode 2 Form 2. */
static inline uint8_t *to_raw(const Image *img, int mode, uint32_t form2_lsn, size_t *size)
{
    static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    uint32_t sectors = (uint32_t)(img->size / NW_DISC_SECTOR);
    uint8_t *out = calloc(sectors, 2352);
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t *r = out + (size_t)s * 2352;
        unsigned lba = s + 150;
        memcpy(r, sync, sizeof(sync));
        r[12] = bcd(lba / 75 / 60);
        r[13] = bcd(lba / 75 % 60);
        r[14] = bcd(lba % 75);
        r[15] = (uint8_t)mode;
        if (mode == 2) {
            r[18] = r[22] = s == form2_lsn ? 0x20 : 0x08;
            memcpy(r + 24, img->data + (size_t)s * NW_DISC_SECTOR, NW_DISC_SECTOR);
        } else {
            memcpy(r + 16, img->data + (size_t)s * NW_DISC_SECTOR, NW_DISC_SECTOR);
        }
    }
    *size = (size_t)sectors * 2352;
    return out;
}

#endif
