/* Synthetic PS2 memory card images (no game data) as save/ps2card.c reads them: a root, one folder of files. gap
   leaves that many free clusters after each allocated one, so chains are not contiguous. */
#ifndef NUWORLD_TESTS_PS2CARD_BUILDER_H
#define NUWORLD_TESTS_PS2CARD_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PCB_PAGE 512
#define PCB_PPC 2
#define PCB_CLUSTER (PCB_PAGE * PCB_PPC)
#define PCB_CLUSTERS 256 /* on the card */
#define PCB_IFC 8        /* the indirect FAT cluster */
#define PCB_FAT 9        /* the FAT cluster */
#define PCB_ALLOC 16     /* the first file cluster */
#define PCB_FREE 0x7FFFFFFFu
#define PCB_END 0xFFFFFFFFu

typedef struct Pcb {
    uint8_t c[PCB_CLUSTERS][PCB_CLUSTER]; /* the clusters' bytes, without ECC */
    uint32_t next, gap;
} Pcb;

static inline void pcb_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void pcb_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static inline uint32_t pcb_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* File cluster n's FAT entry. */
static inline uint8_t *pcb_fat(Pcb *m, uint32_t n)
{
    return m->c[PCB_FAT] + 4 * n;
}

static inline void pcb_init(Pcb *m, uint32_t gap)
{
    uint8_t *sb = m->c[0];
    memset(m, 0, sizeof(*m));
    m->gap = gap;
    memcpy(sb, "Sony PS2 Memory Card Format 1.2.0.0", 35);
    pcb_le16(sb + 0x28, PCB_PAGE);
    pcb_le16(sb + 0x2A, PCB_PPC);
    pcb_le16(sb + 0x2C, 16);
    pcb_le16(sb + 0x2E, 0xFF00);
    pcb_le32(sb + 0x30, PCB_CLUSTERS);
    pcb_le32(sb + 0x34, PCB_ALLOC);
    pcb_le32(sb + 0x38, PCB_CLUSTERS);
    pcb_le32(sb + 0x3C, 0);
    pcb_le32(sb + 0x50, PCB_IFC);
    sb[0x150] = 2;
    sb[0x151] = 0x2B;
    pcb_le32(m->c[PCB_IFC], PCB_FAT);
    for (uint32_t n = 0; n < PCB_CLUSTERS - PCB_ALLOC; n++) {
        pcb_le32(pcb_fat(m, n), PCB_FREE);
    }
}

/* len bytes (at least one cluster) in a new chain; returns its first file cluster. */
static inline uint32_t pcb_alloc(Pcb *m, const void *data, size_t len)
{
    uint32_t first = m->next, prev = PCB_END;
    size_t done = 0;
    do {
        uint32_t n = m->next;
        m->next += 1 + m->gap;
        size_t part = len - done < PCB_CLUSTER ? len - done : PCB_CLUSTER;
        memcpy(m->c[PCB_ALLOC + n], (const uint8_t *)data + done, part);
        done += part;
        if (prev != PCB_END) {
            pcb_le32(pcb_fat(m, prev), 0x80000000u | n);
        }
        pcb_le32(pcb_fat(m, n), PCB_END);
        prev = n;
    } while (done < len);
    return first;
}

/* Entry index of the directory whose chain starts at file cluster first. */
static inline uint8_t *pcb_entry_at(Pcb *m, uint32_t first, int index)
{
    uint32_t c = first, at = (uint32_t)index * 512;
    for (; at >= PCB_CLUSTER; at -= PCB_CLUSTER) {
        c = pcb_rd32(pcb_fat(m, c)) & 0x7FFFFFFFu;
    }
    return m->c[PCB_ALLOC + c] + at;
}

static inline void pcb_entry(uint8_t *e, uint16_t mode, uint32_t length, uint32_t cluster, const char *name)
{
    memset(e, 0, 512);
    pcb_le16(e, mode);
    pcb_le32(e + 0x04, length);
    pcb_le32(e + 0x10, cluster);
    strncpy((char *)e + 0x40, name, 32);
}

/* The card: the root holding the folder dir, which holds the n files. */
static inline void pcb_build(Pcb *m, uint32_t gap, const char *dir, int n, const char *const *names,
                             const void *const *data, const size_t *lens)
{
    uint8_t root[3 * 512] = { 0 }, *folder = calloc((size_t)(n + 2), 512);
    pcb_init(m, gap);
    uint32_t root_cluster = pcb_alloc(m, root, sizeof(root)); /* file cluster 0 */
    uint32_t folder_cluster = pcb_alloc(m, folder, (size_t)(n + 2) * 512);
    pcb_entry(folder, 0x8427, (uint32_t)n + 2, folder_cluster, ".");
    pcb_entry(folder + 512, 0x8427, 0, 0, "..");
    for (int i = 0; i < n; i++) {
        uint32_t c = pcb_alloc(m, data[i], lens[i]);
        pcb_entry(folder + (size_t)(i + 2) * 512, 0x8497, (uint32_t)lens[i], c, names[i]);
    }
    pcb_entry(root, 0x8427, 3, 0, ".");
    pcb_entry(root + 512, 0x8427, 0, 0, "..");
    pcb_entry(root + 1024, 0x8427, (uint32_t)n + 2, folder_cluster, dir);
    /* the directories again, over their chains */
    for (uint32_t i = 0, c = root_cluster; i < 2; i++, c = pcb_rd32(pcb_fat(m, c)) & 0x7FFFFFFFu) {
        memcpy(m->c[PCB_ALLOC + c], root + i * PCB_CLUSTER, i ? sizeof(root) - PCB_CLUSTER : PCB_CLUSTER);
    }
    size_t left = (size_t)(n + 2) * 512;
    for (uint32_t c = folder_cluster, at = 0; left;) {
        size_t part = left < PCB_CLUSTER ? left : PCB_CLUSTER;
        memcpy(m->c[PCB_ALLOC + c], folder + at, part);
        at += (uint32_t)part;
        left -= part;
        c = pcb_rd32(pcb_fat(m, c)) & 0x7FFFFFFFu;
    }
    free(folder);
}

/* The image, malloc'd: pages with a zero ECC when ecc. */
static inline uint8_t *pcb_image(const Pcb *m, bool ecc, size_t *size)
{
    size_t stride = PCB_PAGE + (ecc ? 16 : 0);
    *size = (size_t)PCB_CLUSTERS * PCB_PPC * stride;
    uint8_t *img = calloc(1, *size);
    for (size_t page = 0; page < (size_t)PCB_CLUSTERS * PCB_PPC; page++) {
        memcpy(img + page * stride, m->c[page / PCB_PPC] + (page % PCB_PPC) * PCB_PAGE, PCB_PAGE);
    }
    return img;
}

#endif
