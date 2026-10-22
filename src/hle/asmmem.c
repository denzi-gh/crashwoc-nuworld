#include <string.h>

void NuMemCopy128(void *dst, void *src, int count)
{
    if (count > 0) {
        memcpy(dst, src, (size_t)count * 16);
    }
}
