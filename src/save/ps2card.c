#include "save/ps2card.h"

#include <stdint.h>

#include <SDL3/SDL.h>

#define NW_PS2CARD_ENTRY 512 /* a directory entry */
#define NW_PS2CARD_EXISTS 0x8000
#define NW_PS2CARD_IS_DIR 0x0020
#define NW_PS2CARD_IS_FILE 0x0010
#define NW_PS2CARD_FAT_END 0xFFFFFFFFu
#define NW_PS2CARD_FAT_USED 0x80000000u

typedef struct NwCard {
    const uint8_t *image;
    uint32_t page_len, stride, ppc; /* bytes per page in the image (stride) and in a cluster (page_len) */
    uint32_t cluster;               /* bytes */
    uint32_t clusters;              /* on the card */
    uint32_t alloc_offset;          /* the first file cluster; FAT and directory entries count from it */
    uint32_t root;
    uint32_t ifc[32]; /* the indirect FAT clusters */
    uint8_t *scratch; /* one cluster */
} NwCard;

static uint16_t nw_card_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t nw_card_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool nw_ps2card_is_card(const void *image, size_t size)
{
    return size >= 0x154 && SDL_memcmp(image, NW_PS2CARD_MAGIC, sizeof(NW_PS2CARD_MAGIC) - 1) == 0;
}

static bool nw_card_open(NwCard *c, const uint8_t *image, size_t size)
{
    SDL_zerop(c);
    if (!nw_ps2card_is_card(image, size)) {
        return SDL_SetError("not a formatted PS2 memory card");
    }
    c->image = image;
    c->page_len = nw_card_u16(image + 0x28);
    c->ppc = nw_card_u16(image + 0x2A);
    c->clusters = nw_card_u32(image + 0x30);
    c->alloc_offset = nw_card_u32(image + 0x34);
    c->root = nw_card_u32(image + 0x3C);
    for (int i = 0; i < 32; i++) {
        c->ifc[i] = nw_card_u32(image + 0x50 + 4 * i);
    }
    if (c->page_len < 512 || c->page_len > 4096 || c->page_len % 32 || !c->ppc || c->ppc > 16 || !c->clusters ||
        c->clusters > 0x100000 || c->alloc_offset >= c->clusters) {
        return SDL_SetError("a PS2 memory card superblock with an unknown geometry");
    }
    uint64_t pages = (uint64_t)c->clusters * c->ppc;
    if (size == pages * (c->page_len + c->page_len / 32)) {
        c->stride = c->page_len + c->page_len / 32;
    } else if (size == pages * c->page_len) {
        c->stride = c->page_len;
    } else {
        return SDL_SetError("a PS2 memory card image of %u bytes, but its superblock has %u pages of %u bytes",
                            (unsigned)size, (unsigned)pages, (unsigned)c->page_len);
    }
    c->cluster = c->page_len * c->ppc;
    return (c->scratch = SDL_malloc(c->cluster)) != NULL;
}

/* Copies absolute cluster n to out. */
static bool nw_card_cluster(const NwCard *c, uint64_t n, uint8_t *out)
{
    if (n >= c->clusters) {
        return SDL_SetError("a PS2 memory card cluster outside the card");
    }
    for (uint32_t p = 0; p < c->ppc; p++) {
        SDL_memcpy(out + p * c->page_len, c->image + (size_t)((n * c->ppc + p) * c->stride), c->page_len);
    }
    return true;
}

/* The FAT entry of file cluster n. */
static bool nw_card_fat(const NwCard *c, uint32_t n, uint32_t *entry)
{
    uint32_t per = c->cluster / 4, index = n / per;
    if (index / per >= 32 || !nw_card_cluster(c, c->ifc[index / per], c->scratch) ||
        !nw_card_cluster(c, nw_card_u32(c->scratch + 4 * (index % per)), c->scratch)) {
        return SDL_SetError("a PS2 memory card FAT entry outside the card");
    }
    *entry = nw_card_u32(c->scratch + 4 * (n % per));
    return true;
}

/* The first len bytes of the cluster chain from file cluster first, SDL_malloc'd. */
static uint8_t *nw_card_read(const NwCard *c, uint32_t first, size_t len)
{
    uint8_t *out = SDL_malloc(len ? len : 1);
    uint8_t *buf = SDL_malloc(c->cluster);
    size_t done = 0;
    bool ok = out && buf;
    for (uint32_t n = first, steps = 0; ok && done < len; steps++) {
        uint32_t entry = 0;
        if (steps >= c->clusters) {
            ok = SDL_SetError("a PS2 memory card cluster chain that loops");
        } else if ((ok = nw_card_cluster(c, (uint64_t)c->alloc_offset + n, buf))) {
            size_t n_bytes = len - done < c->cluster ? len - done : c->cluster;
            SDL_memcpy(out + done, buf, n_bytes);
            done += n_bytes;
            if (done < len && (ok = nw_card_fat(c, n, &entry))) {
                if (entry == NW_PS2CARD_FAT_END || !(entry & NW_PS2CARD_FAT_USED)) {
                    ok = SDL_SetError("a PS2 memory card file shorter than its size");
                }
                n = entry & ~NW_PS2CARD_FAT_USED;
            }
        }
    }
    SDL_free(buf);
    if (!ok) {
        SDL_free(out);
        return NULL;
    }
    return out;
}

/* The entry named name with the mode bit want among the count entries of the directory at file cluster first. */
static bool nw_card_find(const NwCard *c, uint32_t first, uint32_t count, const char *name, uint16_t want,
                         uint32_t *cluster, uint32_t *length, bool *found)
{
    if ((uint64_t)count * NW_PS2CARD_ENTRY > (uint64_t)c->clusters * c->cluster) {
        return SDL_SetError("a PS2 memory card directory larger than the card");
    }
    uint8_t *entries = nw_card_read(c, first, (size_t)count * NW_PS2CARD_ENTRY);
    if (!entries) {
        return false;
    }
    *found = false;
    for (uint32_t i = 0; i < count && !*found; i++) {
        const uint8_t *e = entries + (size_t)i * NW_PS2CARD_ENTRY;
        uint16_t mode = nw_card_u16(e);
        if ((mode & (NW_PS2CARD_EXISTS | want)) == (NW_PS2CARD_EXISTS | want) &&
            SDL_strncmp((const char *)e + 0x40, name, 32) == 0) {
            *length = nw_card_u32(e + 0x04);
            *cluster = nw_card_u32(e + 0x10);
            *found = true;
        }
    }
    SDL_free(entries);
    return true;
}

void *nw_ps2card_file(const void *image, size_t size, const char *dir, const char *name, size_t *len, bool *missing)
{
    NwCard c;
    uint32_t cluster = 0, length = 0;
    uint8_t *data = NULL;
    bool found = false;
    *len = 0;
    *missing = false;
    uint8_t *self = nw_card_open(&c, image, size) ? nw_card_read(&c, c.root, NW_PS2CARD_ENTRY) : NULL;
    if (self) { /* the root's "." entry holds its size */
        uint32_t count = nw_card_u32(self + 0x04);
        SDL_free(self);
        bool ok = nw_card_find(&c, c.root, count, dir, NW_PS2CARD_IS_DIR, &cluster, &length, &found);
        if (ok && found) {
            ok = nw_card_find(&c, cluster, length, name, NW_PS2CARD_IS_FILE, &cluster, &length, &found);
        }
        if (ok && !found) {
            SDL_SetError("no /%s/%s on the PS2 memory card", dir, name);
            *missing = true;
        } else if (ok && length > size) {
            SDL_SetError("a PS2 memory card file larger than the card");
        } else if (ok && (data = nw_card_read(&c, cluster, length))) {
            *len = length;
        }
    }
    SDL_free(c.scratch);
    return data;
}
