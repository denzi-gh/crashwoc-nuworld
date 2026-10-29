#include "port/hz.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "data/image.h"
#include "nu_port.h"

int nw_hz = 50;

#define NW_HZ_INT(name, addr, size, pal, ntsc) { name, addr, size, pal, ntsc, 0.0f, 0.0f },
#define NW_HZ_F32(name, addr, pal, ntsc) { name, addr, 0, 0, 0, pal, ntsc },

static const NwHzPatch nw_hz_patches[] = {
#include "port/hz_table.inc"
};

int nw_hz_set(int hz)
{
    nw_hz = hz == 60 ? 60 : 50;
    return nw_hz;
}

unsigned nw_hz_line_ns(void)
{
    return nw_hz == 60 ? 63556u : 64000u;
}

int nw_hz_apply(const NwHzPatch *patches, size_t count, void *(*host)(uint32_t addr))
{
    int kept = 0;
    for (size_t i = 0; i < count; i++) {
        const NwHzPatch *p = &patches[i];
        unsigned char pal[4], ntsc[4];
        size_t size = p->size ? (size_t)p->size : 4;
        if (p->size) {
            for (size_t k = 0; k < size; k++) {
                pal[k] = (unsigned char)(p->pal >> 8 * k);
                ntsc[k] = (unsigned char)(p->ntsc >> 8 * k);
            }
        } else {
            memcpy(pal, &p->fpal, 4);
            memcpy(ntsc, &p->fntsc, 4);
        }
        unsigned char *at = host(p->addr);
        if (!at || memcmp(at, pal, size) != 0) {
            kept++;
            continue;
        }
        memcpy(at, ntsc, size);
    }
    return kept;
}

int nw_hz_patch(void)
{
    return nw_hz_apply(nw_hz_patches, SDL_arraysize(nw_hz_patches), nw_image_host);
}
