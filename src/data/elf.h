/* The retail ELF (ELF32 little-endian MIPS) as the EE loads it. */
#ifndef NUWORLD_DATA_ELF_H
#define NUWORLD_DATA_ELF_H

#include <stddef.h>
#include <stdint.h>

/* Writes the loaded bytes of [base, end) to dst: the PT_LOAD segments' file bytes, zeros elsewhere (.sbss,
   .bss). NULL on success, else what is wrong with the file. */
const char *nw_elf_flatten(const void *file, size_t size, uint32_t base, uint32_t end, void *dst);

#endif
