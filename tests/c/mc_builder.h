/* Synthetic PS2 memory card images (no game data) as save/ps2card.c reads them: a root, one folder of files. gap
   leaves that many free clusters after each allocated one, so chains are not contiguous. */
#ifndef NUWORLD_TESTS_MC_BUILDER_H
#define NUWORLD_TESTS_MC_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MCB_PAGE 512
#define MCB_PPC 2
#define MCB_CLUSTER (MCB_PAGE * MCB_PPC)
#define MCB_CLUSTERS 256 /* on the card */
#define MCB_IFC 8        /* the indirect FAT cluster */
#define MCB_FAT 9        /* the FAT cluster */
#define MCB_ALLOC 16     /* the first file cluster */
#define MCB_FREE 0x7FFFFFFFu
#define MCB_END 0xFFFFFFFFu

typedef struct Mcb {
    uint8_t c[MCB_CLUSTERS][MCB_CLUSTER]; /* the clusters' bytes, without ECC */
    uint32_t next, gap;
} Mcb;

static inline void mcb_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void mcb_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static inline uint32_t mcb_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* File cluster n's FAT entry. */
static inline uint8_t *mcb_fat(Mcb *m, uint32_t n)
{
    return m->c[MCB_FAT] + 4 * n;
}

static inline void mcb_init(Mcb *m, uint32_t gap)
{
    uint8_t *sb = m->c[0];
    memset(m, 0, sizeof(*m));
    m->gap = gap;
    memcpy(sb, "Sony PS2 Memory Card Format 1.2.0.0", 35);
    mcb_le16(sb + 0x28, MCB_PAGE);
    mcb_le16(sb + 0x2A, MCB_PPC);
    mcb_le16(sb + 0x2C, 16);
    mcb_le16(sb + 0x2E, 0xFF00);
    mcb_le32(sb + 0x30, MCB_CLUSTERS);
    mcb_le32(sb + 0x34, MCB_ALLOC);
    mcb_le32(sb + 0x38, MCB_CLUSTERS);
    mcb_le32(sb + 0x3C, 0);
    mcb_le32(sb + 0x50, MCB_IFC);
    sb[0x150] = 2;
    sb[0x151] = 0x2B;
    mcb_le32(m->c[MCB_IFC], MCB_FAT);
    for (uint32_t n = 0; n < MCB_CLUSTERS - MCB_ALLOC; n++) {
        mcb_le32(mcb_fat(m, n), MCB_FREE);
    }
}

/* len bytes (at least one cluster) in a new chain; returns its first file cluster. */
static inline uint32_t mcb_alloc(Mcb *m, const void *data, size_t len)
{
    uint32_t first = m->next, prev = MCB_END;
    size_t done = 0;
    do {
        uint32_t n = m->next;
        m->next += 1 + m->gap;
        size_t part = len - done < MCB_CLUSTER ? len - done : MCB_CLUSTER;
        memcpy(m->c[MCB_ALLOC + n], (const uint8_t *)data + done, part);
        done += part;
        if (prev != MCB_END) {
            mcb_le32(mcb_fat(m, prev), 0x80000000u | n);
        }
        mcb_le32(mcb_fat(m, n), MCB_END);
        prev = n;
    } while (done < len);
    return first;
}

/* Entry index of the directory whose chain starts at file cluster first. */
static inline uint8_t *mcb_entry_at(Mcb *m, uint32_t first, int index)
{
    uint32_t c = first, at = (uint32_t)index * 512;
    for (; at >= MCB_CLUSTER; at -= MCB_CLUSTER) {
        c = mcb_rd32(mcb_fat(m, c)) & 0x7FFFFFFFu;
    }
    return m->c[MCB_ALLOC + c] + at;
}

static inline void mcb_entry(uint8_t *e, uint16_t mode, uint32_t length, uint32_t cluster, const char *name)
{
    memset(e, 0, 512);
    mcb_le16(e, mode);
    mcb_le32(e + 0x04, length);
    mcb_le32(e + 0x10, cluster);
    strncpy((char *)e + 0x40, name, 32);
}

/* The card: the root holding the folder dir, which holds the n files. */
static inline void mcb_build(Mcb *m, uint32_t gap, const char *dir, int n, const char *const *names,
                             const void *const *data, const size_t *lens)
{
    uint8_t root[3 * 512] = { 0 }, *folder = calloc((size_t)(n + 2), 512);
    mcb_init(m, gap);
    uint32_t root_cluster = mcb_alloc(m, root, sizeof(root)); /* file cluster 0 */
    uint32_t folder_cluster = mcb_alloc(m, folder, (size_t)(n + 2) * 512);
    mcb_entry(folder, 0x8427, (uint32_t)n + 2, folder_cluster, ".");
    mcb_entry(folder + 512, 0x8427, 0, 0, "..");
    for (int i = 0; i < n; i++) {
        uint32_t c = mcb_alloc(m, data[i], lens[i]);
        mcb_entry(folder + (size_t)(i + 2) * 512, 0x8497, (uint32_t)lens[i], c, names[i]);
    }
    mcb_entry(root, 0x8427, 3, 0, ".");
    mcb_entry(root + 512, 0x8427, 0, 0, "..");
    mcb_entry(root + 1024, 0x8427, (uint32_t)n + 2, folder_cluster, dir);
    /* the directories again, over their chains */
    for (uint32_t i = 0, c = root_cluster; i < 2; i++, c = mcb_rd32(mcb_fat(m, c)) & 0x7FFFFFFFu) {
        memcpy(m->c[MCB_ALLOC + c], root + i * MCB_CLUSTER, i ? sizeof(root) - MCB_CLUSTER : MCB_CLUSTER);
    }
    size_t left = (size_t)(n + 2) * 512;
    for (uint32_t c = folder_cluster, at = 0; left;) {
        size_t part = left < MCB_CLUSTER ? left : MCB_CLUSTER;
        memcpy(m->c[MCB_ALLOC + c], folder + at, part);
        at += (uint32_t)part;
        left -= part;
        c = mcb_rd32(mcb_fat(m, c)) & 0x7FFFFFFFu;
    }
    free(folder);
}

/* The image, malloc'd: pages with a zero ECC when ecc. */
static inline uint8_t *mcb_image(const Mcb *m, bool ecc, size_t *size)
{
    size_t stride = MCB_PAGE + (ecc ? 16 : 0);
    *size = (size_t)MCB_CLUSTERS * MCB_PPC * stride;
    uint8_t *img = calloc(1, *size);
    for (size_t page = 0; page < (size_t)MCB_CLUSTERS * MCB_PPC; page++) {
        memcpy(img + page * stride, m->c[page / MCB_PPC] + (page % MCB_PPC) * MCB_PAGE, MCB_PAGE);
    }
    return img;
}

#endif
