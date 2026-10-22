/* A synthetic .DAT archive for the tests (no game data): "arc" holding top.bin, a\x.bin (packed), a\Y.BIN and
   b\deep.bin. Node 2's sibling links back to the root, as on the disc. */
#ifndef NUWORLD_TESTS_DAT_BUILDER_H
#define NUWORLD_TESTS_DAT_BUILDER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vfs/dat.h"

typedef struct DatBuild {
    uint8_t data[4096];
    size_t size;
    size_t dir_offset;
    size_t dir_size;
} DatBuild;

static const char *const dat_payload[4] = { "top!", "RNC packed", "deep data", "why" };

static inline void put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/* Entry 1 (a\x.bin) stores packed[packed_size], which unpacks to unpacked bytes; without packed it is the text
   "RNC packed", claiming 100. */
static inline void build_dat_with(DatBuild *d, const uint8_t *packed, size_t packed_size, uint32_t unpacked)
{
    static const struct {
        int16_t child, sibling;
        const char *name;
    } nodes[] = {
        { 0, 0, "" },          /* 0 */
        { 4, 0, "arc" },       /* 1: root */
        { 5, 1, "b" },         /* 2 */
        { 0, 2, "top.bin" },   /* 3: file 0 */
        { 6, 3, "a" },         /* 4 */
        { -2, 0, "deep.bin" }, /* 5: file 2 */
        { -1, 7, "x.bin" },    /* 6: file 1 */
        { -3, 0, "Y.BIN" },    /* 7: file 3 */
    };
    const size_t nnodes = sizeof(nodes) / sizeof(nodes[0]);
    uint32_t offsets[4];
    size_t sizes[4];
    size_t at = 8;

    memset(d, 0, sizeof(*d));
    for (int i = 0; i < 4; i++) {
        offsets[i] = (uint32_t)at;
        sizes[i] = i == 1 && packed ? packed_size : strlen(dat_payload[i]);
        memcpy(d->data + at, i == 1 && packed ? (const void *)packed : (const void *)dat_payload[i], sizes[i]);
        at += sizes[i];
    }
    d->dir_offset = at;
    put_le32(d->data + at, 0xffffffffu);
    put_le32(d->data + at + 4, 4);
    at += 8;
    for (int i = 0; i < 4; i++) {
        put_le32(d->data + at, offsets[i]);
        put_le32(d->data + at + 4, (uint32_t)sizes[i]);
        put_le32(d->data + at + 8, i == 1 ? unpacked : (uint32_t)sizes[i]);
        put_le32(d->data + at + 12, i == 1 ? NW_DAT_PACKED : 0);
        at += 16;
    }
    put_le32(d->data + at, (uint32_t)nnodes);
    at += 4;
    size_t names_at = at + nnodes * 8 + 4, name = 0;
    for (size_t i = 0; i < nnodes; i++) {
        d->data[at] = (uint8_t)nodes[i].child;
        d->data[at + 1] = (uint8_t)((uint16_t)nodes[i].child >> 8);
        d->data[at + 2] = (uint8_t)nodes[i].sibling;
        d->data[at + 3] = (uint8_t)((uint16_t)nodes[i].sibling >> 8);
        put_le32(d->data + at + 4, (uint32_t)name);
        memcpy(d->data + names_at + name, nodes[i].name, strlen(nodes[i].name) + 1);
        name += strlen(nodes[i].name) + 1;
        at += 8;
    }
    put_le32(d->data + at, (uint32_t)name);
    d->size = names_at + name;
    d->dir_size = d->size - d->dir_offset;
    put_le32(d->data, (uint32_t)d->dir_offset);
    put_le32(d->data + 4, (uint32_t)d->dir_size);
}

static inline void build_dat(DatBuild *d)
{
    build_dat_with(d, NULL, 0, 100);
}

#endif
