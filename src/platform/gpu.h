#ifndef NUWORLD_PLATFORM_GPU_H
#define NUWORLD_PLATFORM_GPU_H

#include <stdbool.h>
#include <stddef.h>

#define NW_GPU_NAME 128
#define NW_GPU_CARDS 2

bool nw_gpu_flags(const char *flags, int preference, char *out, size_t size);
int nw_gpu_list(char names[NW_GPU_CARDS][NW_GPU_NAME]);
void nw_gpu_prefer(const char *name);

#endif
