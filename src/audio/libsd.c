#include "audio/libsd.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "vfs/vfs.h"

#define NW_LIBSD_PATH "SYS\\LIBSD.IRX"
#define NW_LIBSD_MAX (64u << 10)
#define NW_LIBSD_NAME "Sound_Device_Library"
#define NW_LIBSD_DATA 0x8f0    /* .data of 2.3.10 */
#define NW_LIBSD_SIZES 0x38    /* in .data: per mode the work area in 8-byte units */
#define NW_LIBSD_PRESETS 0x60  /* per mode a mask (0: every register), then the 32 registers */
#define NW_LIBSD_PRESET 0x44

static uint32_t nw_libsd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The section by name, NULL when the ELF does not have it within its size. */
static const uint8_t *nw_libsd_section(const uint8_t *elf, size_t size, const char *name, uint32_t *len)
{
    if (size < 0x34 || memcmp(elf, "\x7f" "ELF\x01\x01", 6) != 0) {
        return NULL;
    }
    uint32_t shoff = nw_libsd_u32(elf + 0x20);
    unsigned shnum = elf[0x30] | elf[0x31] << 8, shstrndx = elf[0x32] | elf[0x33] << 8;
    if (shstrndx >= shnum || shoff > size || (size - shoff) / 40 < shnum) {
        return NULL;
    }
    const uint8_t *strtab = elf + shoff + shstrndx * 40;
    uint32_t names = nw_libsd_u32(strtab + 0x10), names_size = nw_libsd_u32(strtab + 0x14);
    if (names > size || names_size > size - names) {
        return NULL;
    }
    for (unsigned i = 0; i < shnum; i++) {
        const uint8_t *sh = elf + shoff + i * 40;
        uint32_t at = nw_libsd_u32(sh), off = nw_libsd_u32(sh + 0x10), n = nw_libsd_u32(sh + 0x14);
        if (at < names_size && strncmp((const char *)elf + names + at, name, names_size - at) == 0 && off <= size &&
            n <= size - off) {
            *len = n;
            return elf + off;
        }
    }
    return NULL;
}

bool nw_libsd_presets(const NwVfs *vfs, NwSpu2Presets *out)
{
    NwVfsFile *f = nw_vfs_open(vfs, NW_LIBSD_PATH, NW_VFS_READ);
    if (!f) {
        return false;
    }
    int64_t size = nw_vfs_size(f);
    uint8_t *elf = size > 0 && size <= NW_LIBSD_MAX ? SDL_malloc((size_t)size) : NULL;
    bool read = elf && nw_vfs_read(f, elf, (size_t)size) == size;
    nw_vfs_close(f);
    uint32_t mod_len = 0, data_len = 0;
    const uint8_t *mod = read ? nw_libsd_section(elf, (size_t)size, ".iopmod", &mod_len) : NULL;
    const uint8_t *data = read ? nw_libsd_section(elf, (size_t)size, ".data", &data_len) : NULL;
    bool ok = mod && data && data_len == NW_LIBSD_DATA && mod_len >= 0x1a + sizeof(NW_LIBSD_NAME) &&
              memcmp(mod + 0x1a, NW_LIBSD_NAME, sizeof(NW_LIBSD_NAME)) == 0;
    if (ok) {
        for (int m = 0; m < NW_SPU2_REVERB_MODES; m++) {
            const uint8_t *p = data + NW_LIBSD_PRESETS + m * NW_LIBSD_PRESET;
            out->size[m] = nw_libsd_u32(data + NW_LIBSD_SIZES + m * 4);
            out->mode[m].mask = nw_libsd_u32(p);
            for (int r = 0; r < NW_SPU2_REVERB_REGS; r++) {
                out->mode[m].reg[r] = (uint16_t)(p[4 + r * 2] | p[5 + r * 2] << 8);
            }
        }
    } else if (read) {
        SDL_SetError("%s is not libsd 2.3.10", NW_LIBSD_PATH);
    } else if (elf) {
        SDL_SetError("%s: short read", NW_LIBSD_PATH);
    } else {
        SDL_SetError("%s: %lld bytes", NW_LIBSD_PATH, (long long)size);
    }
    SDL_free(elf);
    return ok;
}
