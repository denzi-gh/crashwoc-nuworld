#ifndef NUWORLD_VFS_RNC_H
#define NUWORLD_VFS_RNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NW_RNC_HEADER 18 /* "RNC" 2, unpacked size, packed size (big-endian), CRCs, leeway, chunks */


bool nw_rnc_header(const uint8_t *p, size_t size, uint32_t *unpacked, uint32_t *packed);


int64_t nw_rnc_unpack(const uint8_t *in, const uint8_t *in_end, uint8_t *out, uint8_t *out_end);

#endif
