/* What the disc loaders (src/port/nugscn.c, nuhgobj.c) copy file records with: pointer fields hold indexes, offsets
   or -1 there, kept as integers for the fixups. */
#ifndef NUWORLD_PORT_XFILE_H
#define NUWORLD_PORT_XFILE_H

#include <stdint.h>
#include <string.h>

#include "nw_convert.h"

static void *nw_file_ptr_in(NwXCtx *ctx, uint32_t addr, const NwType *type, const char *site)
{
    (void)ctx;
    (void)type;
    (void)site;
    return (void *)(intptr_t)(int32_t)addr;
}

static uintptr_t nw_file_addr_in(NwXCtx *ctx, uint32_t value, int is_signed, const char *site)
{
    (void)ctx;
    (void)site;
    return is_signed ? (uintptr_t)(intptr_t)(int32_t)value : value;
}

static NwXCtx nw_file_x = { .ptr_in = nw_file_ptr_in, .addr_in = nw_file_addr_in };

/* A file record to its host copy, copied out first (the file is only 4-byte aligned). */
#define NW_FILE_X(type, dst, src) \
    do { \
        nw_ps2_##type nw_rec_; \
        memcpy(&nw_rec_, (src), sizeof(nw_rec_)); \
        nw_x_##type((dst), &nw_rec_, &nw_file_x); \
    } while (0)

/* The integer a converted pointer field kept. */
#define NW_FILE_IX(p) ((int)(intptr_t)(p))

#endif
