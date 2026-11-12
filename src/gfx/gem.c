#include "gfx/gem.h"

#include <string.h>

#define NW_GEM_MTLS 64

static struct {
    const void *mtl[NW_GEM_MTLS];
    NwGemInfo info[NW_GEM_MTLS];
    int n;
} nw_gem;

void nw_gem_reset(void)
{
    nw_gem.n = 0;
}

void nw_gem_add(const void *mtl, NwGemKind kind, NwGemRole role, const float tint[3])
{
    for (int i = 0; i < nw_gem.n; i++) {
        if (nw_gem.mtl[i] == mtl) {
            return;
        }
    }
    if (!mtl || nw_gem.n == NW_GEM_MTLS) {
        return;
    }
    NwGemInfo *g = &nw_gem.info[nw_gem.n];
    g->kind = (uint8_t)kind;
    g->role = (uint8_t)role;
    memcpy(g->tint, tint, sizeof(g->tint));
    nw_gem.mtl[nw_gem.n++] = mtl;
}

NwGemInfo nw_gem_find(const void *mtl)
{
    for (int i = 0; i < nw_gem.n; i++) {
        if (nw_gem.mtl[i] == mtl) {
            return nw_gem.info[i];
        }
    }
    return (NwGemInfo){ 0 };
}
