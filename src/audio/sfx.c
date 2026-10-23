#include "audio/sfx.h"

#include <SDL3/SDL.h>

bool nw_sfx_find(NwDat *sfx, const char *name, NwSfxSample *s)
{
    uint8_t head[NW_VAG_HEADER];
    int entry = nw_dat_find(sfx, name);

    if (entry < 0) {
        return SDL_SetError("%s is not in SFX.DAT", name);
    }
    uint32_t size = sfx->entries[entry].size;
    if ((size >= NW_VAG_HEADER && !nw_dat_read(sfx, entry, 0, head, sizeof(head))) ||
        !nw_vag_header(&s->vag, head, size)) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        return SDL_SetError("%s: %s", name, why);
    }
    s->entry = entry;
    return true;
}

bool nw_sfx_read(NwDat *sfx, const NwSfxSample *s, uint32_t offset, void *dst, uint32_t size)
{
    if (offset > s->vag.size || size > s->vag.size - offset) {
        return SDL_SetError("read of %u bytes at %u runs past the sample (%u bytes)", size, offset, s->vag.size);
    }
    return nw_dat_read(sfx, s->entry, NW_VAG_HEADER + offset, dst, size);
}
