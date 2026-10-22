/* nucore/asmmem.s (port/units.toml: hle). NuMemSet128, which retail never reaches, stays a generated stub. */
#include <string.h>

/* Copies count quadwords. */
void NuMemCopy128(void *dst, void *src, int count)
{
    if (count > 0) {
        memcpy(dst, src, (size_t)count * 16);
    }
}
