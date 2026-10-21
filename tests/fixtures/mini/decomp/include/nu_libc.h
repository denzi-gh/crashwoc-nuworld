#ifndef NU_LIBC_H
#define NU_LIBC_H

#ifdef NW_HOST
#include <stdlib.h>
#include <string.h>
#else
void *memcpy(void *dst, const void *src, unsigned int n);
#endif

#endif
