#include "data/elf.h"

#include <string.h>

#define NW_ELF_EM_MIPS 8
#define NW_ELF_PT_LOAD 1

static uint16_t nw_elf_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t nw_elf_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const char *nw_elf_flatten(const void *file, size_t size, uint32_t base, uint32_t end, void *dst)
{
    const uint8_t *b = file;
    if (size < 52 || memcmp(b, "\x7f" "ELF", 4) != 0) {
        return "not an ELF file";
    }
    if (b[4] != 1 || b[5] != 1 || nw_elf_le16(b + 18) != NW_ELF_EM_MIPS) {
        return "not a 32-bit little-endian MIPS ELF";
    }
    uint32_t phoff = nw_elf_le32(b + 28);
    uint16_t phentsize = nw_elf_le16(b + 42), phnum = nw_elf_le16(b + 44);
    if (phentsize < 32 || phoff > size || phnum > (size - phoff) / phentsize) {
        return "program headers out of range";
    }
    memset(dst, 0, end - base);
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *p = b + phoff + (size_t)i * phentsize;
        if (nw_elf_le32(p) != NW_ELF_PT_LOAD) {
            continue;
        }
        uint32_t offset = nw_elf_le32(p + 4), vaddr = nw_elf_le32(p + 8);
        uint32_t filesz = nw_elf_le32(p + 16), memsz = nw_elf_le32(p + 20);
        if (offset > size || filesz > size - offset || filesz > memsz || vaddr > UINT32_MAX - memsz) {
            return "segment out of range";
        }
        uint32_t lo = vaddr > base ? vaddr : base, hi = vaddr + filesz < end ? vaddr + filesz : end;
        if (lo < hi) {
            memcpy((uint8_t *)dst + (lo - base), b + offset + (lo - vaddr), hi - lo);
        }
    }
    return NULL;
}
