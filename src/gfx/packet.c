#include "gfx/packet.h"

#include <string.h>

#include <SDL3/SDL.h>

static bool nw_packet_grow(void **items, uint32_t *cap, uint32_t n, size_t size)
{
    if (n < *cap) {
        return true;
    }
    uint32_t c = *cap ? *cap * 2 : 256;
    void *grown = SDL_realloc(*items, c * size);
    if (!grown) {
        return false;
    }
    *items = grown;
    *cap = c;
    return true;
}

NwCmd *nw_packet_cmd(NwPacket *p)
{
    if (!nw_packet_grow((void **)&p->cmds, &p->cmdcap, p->ncmds, sizeof(*p->cmds))) {
        return NULL;
    }
    NwCmd *c = &p->cmds[p->ncmds++];
    memset(c, 0, sizeof(*c));
    return c;
}

NwView *nw_packet_view(NwPacket *p, uint32_t *index)
{
    if (!nw_packet_grow((void **)&p->views, &p->viewcap, p->nviews, sizeof(*p->views))) {
        return NULL;
    }
    *index = p->nviews;
    return &p->views[p->nviews++];
}

void nw_packet_reset(NwPacket *p)
{
    p->ncmds = 0;
    p->nviews = 0;
}

void nw_packet_free(NwPacket *p)
{
    SDL_free(p->cmds);
    SDL_free(p->views);
    memset(p, 0, sizeof(*p));
}
