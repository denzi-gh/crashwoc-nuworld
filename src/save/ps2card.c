#include "save/ps2card.h"

#include <stdint.h>

#include <SDL3/SDL.h>

#define NW_PS2CARD_ENTRY 512 /* a directory entry */
#define NW_PS2CARD_EXISTS 0x8000
#define NW_PS2CARD_IS_DIR 0x0020
#define NW_PS2CARD_IS_FILE 0x0010
#define NW_PS2CARD_FAT_END 0xFFFFFFFFu
#define NW_PS2CARD_FAT_USED 0x80000000u
#define NW_PS2CARD_FAT_FREE 0x7FFFFFFFu
#define NW_PS2CARD_NONE 0xFFFFFFFFu /* no cluster */
#define NW_PS2CARD_USE_ECC 0x01     /* superblock card flag: pages carry their ECC */
/* The modes the PS2's own card manager gives what it makes: a folder (and its "." and ".."), a closed file. */
#define NW_PS2CARD_MODE_DIR 0x8427
#define NW_PS2CARD_MODE_FILE 0x8497
#define NW_PS2CARD_JST (9 * 3600 * SDL_NS_PER_SECOND) /* the PS2 keeps Japan's time */

typedef struct NwCard {
    const uint8_t *image;
    uint8_t *rw;                    /* image, when it is written */
    uint32_t page_len, stride, ppc; /* bytes per page in the image (stride) and in a cluster (page_len) */
    uint32_t cluster;               /* bytes */
    uint32_t clusters;              /* on the card */
    uint32_t alloc_offset;          /* the first file cluster; FAT and directory entries count from it */
    uint32_t alloc_end;             /* file clusters that may be allocated */
    uint32_t root;
    uint32_t ifc[32]; /* the indirect FAT clusters */
    uint8_t flags;
    uint32_t next_free; /* where the search for a free cluster goes on */
    uint8_t *scratch;   /* one cluster */
} NwCard;

static uint16_t nw_card_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t nw_card_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void nw_card_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void nw_card_put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
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
    c->alloc_end = nw_card_u32(image + 0x38);
    c->root = nw_card_u32(image + 0x3C);
    c->flags = image[0x151];
    for (int i = 0; i < 32; i++) {
        c->ifc[i] = nw_card_u32(image + 0x50 + 4 * i);
    }
    if (c->page_len < 512 || c->page_len > 4096 || c->page_len % 128 || !c->ppc || c->ppc > 16 || !c->clusters ||
        c->clusters > 0x100000 || c->alloc_offset >= c->clusters) {
        return SDL_SetError("a PS2 memory card superblock with an unknown geometry");
    }
    c->alloc_end = SDL_min(c->alloc_end, c->clusters - c->alloc_offset);
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

/* Byte offset of absolute cluster n in the image (offset < c->cluster). */
static size_t nw_card_offset(const NwCard *c, uint64_t n, uint32_t offset)
{
    return (size_t)((n * c->ppc + offset / c->page_len) * c->stride + offset % c->page_len);
}

/* Copies absolute cluster n to out. */
static bool nw_card_cluster(const NwCard *c, uint64_t n, uint8_t *out)
{
    if (n >= c->clusters) {
        return SDL_SetError("a PS2 memory card cluster outside the card");
    }
    for (uint32_t p = 0; p < c->ppc; p++) {
        SDL_memcpy(out + p * c->page_len, c->image + nw_card_offset(c, n, p * c->page_len), c->page_len);
    }
    return true;
}

static uint8_t nw_card_parity(uint8_t b)
{
    b ^= b >> 4;
    b ^= b >> 2;
    b ^= b >> 1;
    return b & 1;
}

/* The card's Hamming code of 128 bytes: column parity, then the line parities. */
static void nw_card_ecc(const uint8_t *chunk, uint8_t out[3])
{
    static const uint8_t columns[7] = { 0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0 };
    uint8_t cp = 0x77, lp0 = 0x7F, lp1 = 0x7F;
    for (int i = 0; i < 128; i++) {
        uint8_t b = chunk[i], mask = 0;
        for (int k = 0; k < 7; k++) {
            mask |= (uint8_t)(nw_card_parity(b & columns[k]) << k);
        }
        cp ^= mask;
        if (nw_card_parity(b)) {
            lp0 ^= (uint8_t)~i;
            lp1 ^= (uint8_t)i;
        }
    }
    out[0] = cp;
    out[1] = lp0 & 0x7F;
    out[2] = lp1;
}

/* Page p of the image, its ECC brought up to date when the card keeps one (the rest of the spare zero, as the PS2
   writes it). */
static void nw_card_page_written(NwCard *c, uint64_t p)
{
    uint8_t *page = c->rw + (size_t)(p * c->stride);
    if (c->stride > c->page_len && (c->flags & NW_PS2CARD_USE_ECC)) {
        uint32_t chunks = c->page_len / 128;
        for (uint32_t k = 0; k < chunks; k++) {
            nw_card_ecc(page + 128 * k, page + c->page_len + 3 * k);
        }
        SDL_memset(page + c->page_len + 3 * chunks, 0, c->stride - c->page_len - 3 * chunks);
    }
}

static bool nw_card_put_cluster(NwCard *c, uint64_t n, const uint8_t *data)
{
    if (n >= c->clusters) {
        return SDL_SetError("a PS2 memory card cluster outside the card");
    }
    for (uint32_t p = 0; p < c->ppc; p++) {
        SDL_memcpy(c->rw + nw_card_offset(c, n, p * c->page_len), data + p * c->page_len, c->page_len);
        nw_card_page_written(c, n * c->ppc + p);
    }
    return true;
}

/* Where file cluster n's FAT entry is: an absolute cluster and the offset in it. */
static bool nw_card_fat_where(const NwCard *c, uint32_t n, uint64_t *cluster, uint32_t *offset)
{
    uint32_t per = c->cluster / 4, index = n / per;
    uint32_t ifc = index / per < 32 ? c->ifc[index / per] : NW_PS2CARD_NONE;
    uint32_t fat = ifc < c->clusters ? nw_card_u32(c->image + nw_card_offset(c, ifc, 4 * (index % per)))
                                     : NW_PS2CARD_NONE;
    if (n >= c->clusters || fat >= c->clusters) {
        return SDL_SetError("a PS2 memory card FAT entry outside the card");
    }
    *cluster = fat;
    *offset = 4 * (n % per);
    return true;
}

/* The FAT entry of file cluster n. */
static bool nw_card_fat(const NwCard *c, uint32_t n, uint32_t *entry)
{
    uint64_t cluster = 0;
    uint32_t offset = 0;
    if (!nw_card_fat_where(c, n, &cluster, &offset)) {
        return false;
    }
    *entry = nw_card_u32(c->image + nw_card_offset(c, cluster, offset));
    return true;
}

static bool nw_card_set_fat(NwCard *c, uint32_t n, uint32_t entry)
{
    uint64_t cluster = 0;
    uint32_t offset = 0;
    if (!nw_card_fat_where(c, n, &cluster, &offset)) {
        return false;
    }
    nw_card_put32(c->rw + nw_card_offset(c, cluster, offset), entry);
    nw_card_page_written(c, cluster * c->ppc + offset / c->page_len);
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

/* count entries of the directory at file cluster first, SDL_malloc'd. */
static uint8_t *nw_card_dir(const NwCard *c, uint32_t first, uint32_t count)
{
    if ((uint64_t)count * NW_PS2CARD_ENTRY > (uint64_t)c->clusters * c->cluster) {
        SDL_SetError("a PS2 memory card directory larger than the card");
        return NULL;
    }
    return nw_card_read(c, first, (size_t)count * NW_PS2CARD_ENTRY);
}

/* The root's entries; its "." entry holds how many. */
static uint8_t *nw_card_root(const NwCard *c, uint32_t *count)
{
    uint8_t *self = nw_card_read(c, c->root, NW_PS2CARD_ENTRY);
    *count = self ? nw_card_u32(self + 0x04) : 0;
    SDL_free(self);
    return self ? nw_card_dir(c, c->root, *count) : NULL;
}

/* The entry of entries (count) named name that exists, -1 when none. */
static int nw_card_named(const uint8_t *entries, uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = entries + (size_t)i * NW_PS2CARD_ENTRY;
        if ((nw_card_u16(e) & NW_PS2CARD_EXISTS) && SDL_strncmp((const char *)e + 0x40, name, 32) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* The entry named name with the mode bit want among the count entries of the directory at file cluster first. */
static bool nw_card_find(const NwCard *c, uint32_t first, uint32_t count, const char *name, uint16_t want,
                         uint32_t *cluster, uint32_t *length, bool *found)
{
    uint8_t *entries = nw_card_dir(c, first, count);
    if (!entries) {
        return false;
    }
    int i = nw_card_named(entries, count, name);
    const uint8_t *e = entries + (size_t)(i < 0 ? 0 : i) * NW_PS2CARD_ENTRY;
    *found = i >= 0 && (nw_card_u16(e) & want);
    if (*found) {
        *length = nw_card_u32(e + 0x04);
        *cluster = nw_card_u32(e + 0x10);
    }
    SDL_free(entries);
    return true;
}

void *nw_ps2card_file(const void *image, size_t size, const char *dir, const char *name, size_t *len, bool *missing)
{
    NwCard c;
    uint32_t cluster = 0, length = 0, count = 0;
    uint8_t *data = NULL;
    bool found = false;
    *len = 0;
    *missing = false;
    uint8_t *root = nw_card_open(&c, image, size) ? nw_card_root(&c, &count) : NULL;
    if (root) {
        SDL_free(root);
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

/* A free file cluster, marked the end of a chain; false with an SDL error when the card is full. */
static bool nw_card_alloc(NwCard *c, uint32_t *n)
{
    for (; c->next_free < c->alloc_end; c->next_free++) {
        uint32_t entry = 0;
        if (!nw_card_fat(c, c->next_free, &entry)) {
            return false;
        }
        if (!(entry & NW_PS2CARD_FAT_USED)) {
            *n = c->next_free++;
            return nw_card_set_fat(c, *n, NW_PS2CARD_FAT_END);
        }
    }
    return SDL_SetError("the PS2 memory card is full");
}

/* len bytes as the chain from *first (NW_PS2CARD_NONE: no chain yet, *first is set): its clusters reused, more
   allocated or the ones left over freed; the last cluster's tail is zeroed. A chain keeps its first cluster. */
static bool nw_card_put_chain(NwCard *c, uint32_t *first, const uint8_t *data, size_t len)
{
    size_t need = len ? (len + c->cluster - 1) / c->cluster : 1;
    if (need > c->alloc_end) {
        return SDL_SetError("the PS2 memory card is full");
    }
    uint32_t *chain = SDL_malloc(need * sizeof(*chain)), n = *first, have = 0;
    bool ok = chain != NULL;
    for (uint32_t steps = 0; ok && n != NW_PS2CARD_NONE && have < need; steps++) { /* reused */
        uint32_t entry = 0;
        if (steps >= c->clusters || n >= c->alloc_end) {
            ok = SDL_SetError("a PS2 memory card cluster chain that loops or leaves the card");
        } else if ((ok = nw_card_fat(c, n, &entry))) {
            chain[have++] = n;
            n = entry == NW_PS2CARD_FAT_END || !(entry & NW_PS2CARD_FAT_USED) ? NW_PS2CARD_NONE
                                                                               : entry & ~NW_PS2CARD_FAT_USED;
        }
    }
    for (uint32_t steps = 0; ok && n != NW_PS2CARD_NONE; steps++) { /* left over */
        uint32_t entry = 0;
        if (steps >= c->clusters || n >= c->alloc_end) {
            ok = SDL_SetError("a PS2 memory card cluster chain that loops or leaves the card");
        } else if ((ok = nw_card_fat(c, n, &entry) && nw_card_set_fat(c, n, NW_PS2CARD_FAT_FREE))) {
            n = entry == NW_PS2CARD_FAT_END || !(entry & NW_PS2CARD_FAT_USED) ? NW_PS2CARD_NONE
                                                                               : entry & ~NW_PS2CARD_FAT_USED;
        }
    }
    while (ok && have < need) {
        ok = nw_card_alloc(c, &chain[have]);
        have += ok;
    }
    for (uint32_t i = 0; ok && i < have; i++) {
        size_t at = (size_t)i * c->cluster, part = len > at ? SDL_min(len - at, (size_t)c->cluster) : 0;
        SDL_memset(c->scratch, 0, c->cluster);
        SDL_memcpy(c->scratch, data + at, part);
        ok = nw_card_set_fat(c, chain[i], i + 1 < have ? NW_PS2CARD_FAT_USED | chain[i + 1] : NW_PS2CARD_FAT_END) &&
             nw_card_put_cluster(c, (uint64_t)c->alloc_offset + chain[i], c->scratch);
    }
    if (ok) {
        *first = chain[0];
    }
    SDL_free(chain);
    return ok;
}

/* time as the card's date and time: Japan's, {0, s, min, h, day, month, year (2 bytes)}. */
static void nw_card_stamp(int64_t time, uint8_t stamp[8])
{
    SDL_DateTime dt;
    SDL_memset(stamp, 0, 8);
    if (SDL_TimeToDateTime(time + NW_PS2CARD_JST, &dt, false)) {
        stamp[1] = (uint8_t)dt.second;
        stamp[2] = (uint8_t)dt.minute;
        stamp[3] = (uint8_t)dt.hour;
        stamp[4] = (uint8_t)dt.day;
        stamp[5] = (uint8_t)dt.month;
        nw_card_put16(stamp + 6, (uint16_t)dt.year);
    }
}

static void nw_card_entry(uint8_t *e, uint16_t mode, uint32_t length, uint32_t cluster, uint32_t dir_entry,
                          const uint8_t stamp[8], const char *name)
{
    SDL_memset(e, 0, NW_PS2CARD_ENTRY);
    nw_card_put16(e, mode);
    nw_card_put32(e + 0x04, length);
    SDL_memcpy(e + 0x08, stamp, 8);
    nw_card_put32(e + 0x10, cluster);
    nw_card_put32(e + 0x14, dir_entry);
    SDL_memcpy(e + 0x18, stamp, 8);
    SDL_strlcpy((char *)e + 0x40, name, 32);
}

/* An entry of *entries (*count) that does not exist, else one more at the end; -1 without memory. */
static int nw_card_free_entry(uint8_t **entries, uint32_t *count)
{
    for (uint32_t i = 2; i < *count; i++) {
        if (!(nw_card_u16(*entries + (size_t)i * NW_PS2CARD_ENTRY) & NW_PS2CARD_EXISTS)) {
            return (int)i;
        }
    }
    uint8_t *more = SDL_realloc(*entries, (size_t)(*count + 1) * NW_PS2CARD_ENTRY);
    if (!more) {
        return -1;
    }
    *entries = more;
    SDL_memset(more + (size_t)*count * NW_PS2CARD_ENTRY, 0, NW_PS2CARD_ENTRY);
    return (int)(*count)++;
}

/* The files into the folder's entries (count), their data onto the card. */
static bool nw_card_put_files(NwCard *c, uint8_t **folder, uint32_t *count, const NwPs2CardFile *files, int n,
                              const uint8_t stamp[8])
{
    for (int i = 0; i < n; i++) {
        const NwPs2CardFile *f = &files[i];
        if (SDL_strlen(f->name) >= 32 || f->len > UINT32_MAX) {
            return SDL_SetError("%s: not a PS2 memory card file", f->name);
        }
        int at = nw_card_named(*folder, *count, f->name);
        uint8_t *e = at >= 0 ? *folder + (size_t)at * NW_PS2CARD_ENTRY : NULL;
        if (e && !(nw_card_u16(e) & NW_PS2CARD_IS_FILE)) {
            return SDL_SetError("%s on the PS2 memory card is a folder", f->name);
        }
        if (e && f->keep) {
            continue;
        }
        uint32_t first = NW_PS2CARD_NONE;
        if (e && nw_card_u32(e + 0x04)) {
            first = nw_card_u32(e + 0x10);
        } else if (!e) {
            if ((at = nw_card_free_entry(folder, count)) < 0) {
                return false;
            }
            e = *folder + (size_t)at * NW_PS2CARD_ENTRY;
            nw_card_entry(e, NW_PS2CARD_MODE_FILE, 0, NW_PS2CARD_NONE, 0, stamp, f->name);
        }
        if (!nw_card_put_chain(c, &first, f->data, f->len)) {
            return false;
        }
        nw_card_put32(e + 0x04, (uint32_t)f->len);
        nw_card_put32(e + 0x10, first);
        SDL_memcpy(e + 0x18, stamp, 8);
    }
    return true;
}

bool nw_ps2card_put(void *image, size_t size, const char *dir, const NwPs2CardFile *files, int count, int64_t time)
{
    NwCard c;
    uint8_t stamp[8], *folder = NULL;
    uint32_t root_count = 0, folder_count = 2, folder_first = NW_PS2CARD_NONE;
    nw_card_stamp(time, stamp);
    uint8_t *root = nw_card_open(&c, image, size) ? nw_card_root(&c, &root_count) : NULL;
    bool ok = root != NULL;
    c.rw = image;
    int at = ok ? nw_card_named(root, root_count, dir) : -1;
    if (ok && SDL_strlen(dir) >= 32) {
        ok = SDL_SetError("%s: not a PS2 memory card folder", dir);
    } else if (ok && at >= 0) {
        const uint8_t *e = root + (size_t)at * NW_PS2CARD_ENTRY;
        folder_count = nw_card_u32(e + 0x04);
        folder_first = nw_card_u32(e + 0x10);
        if (!(nw_card_u16(e) & NW_PS2CARD_IS_DIR)) {
            ok = SDL_SetError("/%s on the PS2 memory card is a file", dir);
        } else if (folder_count < 2 || !(folder = nw_card_dir(&c, folder_first, folder_count))) {
            ok = folder_count < 2 ? SDL_SetError("/%s on the PS2 memory card has no \".\"", dir) : false;
        }
    } else if (ok) { /* made as the PS2 makes one: "." pointing back at its entry in the root, ".." with the root's
                        date */
        at = nw_card_free_entry(&root, &root_count);
        folder = SDL_calloc(2, NW_PS2CARD_ENTRY);
        ok = at >= 0 && folder;
        if (ok) {
            nw_card_entry(root + (size_t)at * NW_PS2CARD_ENTRY, NW_PS2CARD_MODE_DIR, 2, NW_PS2CARD_NONE, 0, stamp, dir);
            nw_card_entry(folder, NW_PS2CARD_MODE_DIR, 0, c.root, (uint32_t)at, stamp, ".");
            nw_card_entry(folder + NW_PS2CARD_ENTRY, NW_PS2CARD_MODE_DIR, 0, 0, 0, root + 0x08, "..");
            nw_card_put32(root + 0x04, root_count);
            SDL_memcpy(root + 0x18, stamp, 8); /* the root changed */
        }
    }
    ok = ok && nw_card_put_files(&c, &folder, &folder_count, files, count, stamp) &&
         nw_card_put_chain(&c, &folder_first, folder, (size_t)folder_count * NW_PS2CARD_ENTRY);
    if (ok) {
        uint8_t *e = root + (size_t)at * NW_PS2CARD_ENTRY;
        nw_card_put32(e + 0x04, folder_count);
        nw_card_put32(e + 0x10, folder_first);
        SDL_memcpy(e + 0x18, stamp, 8);
        uint32_t root_first = c.root;
        ok = nw_card_put_chain(&c, &root_first, root, (size_t)root_count * NW_PS2CARD_ENTRY);
    }
    SDL_free(folder);
    SDL_free(root);
    SDL_free(c.scratch);
    return ok;
}
