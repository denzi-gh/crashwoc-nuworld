#include "gfx/packet.h"

#include <string.h>

#include <SDL3/SDL.h>

/* Three packets: the game thread builds one, the newest published one waits, the render thread draws one. */
static NwPacket nw_slots[3];
static int nw_building = 0, nw_ready = 1, nw_drawing = 2;
static bool nw_fresh;      /* the ready slot holds a packet the render thread has not taken */
static uint64_t nw_seq;    /* of the last published packet */
static NwGpuJob *nw_jobs;
static uint32_t nw_njobs, nw_jobcap;
static SDL_Mutex *nw_lock;
static SDL_InitState nw_lock_init;

static void nw_packet_lock(void)
{
    if (SDL_ShouldInit(&nw_lock_init)) {
        nw_lock = SDL_CreateMutex();
        SDL_SetInitialized(&nw_lock_init, nw_lock != NULL);
    }
    SDL_LockMutex(nw_lock);
}

static bool nw_packet_grow(void **items, uint32_t *cap, uint32_t n, size_t size)
{
    if (n < *cap) {
        return true;
    }
    uint32_t c = *cap ? *cap * 2 : 256;
    while (c <= n) {
        c *= 2;
    }
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

NwMtlSnap *nw_packet_mtl(NwPacket *p, uint32_t *index)
{
    if (!nw_packet_grow((void **)&p->mtls, &p->mtlcap, p->nmtls, sizeof(*p->mtls))) {
        return NULL;
    }
    *index = p->nmtls;
    return &p->mtls[p->nmtls++];
}

NwLights *nw_packet_light(NwPacket *p, uint32_t *index)
{
    if (!nw_packet_grow((void **)&p->lights, &p->lightcap, p->nlights, sizeof(*p->lights))) {
        return NULL;
    }
    *index = p->nlights;
    return &p->lights[p->nlights++];
}

uint32_t *nw_packet_idx(NwPacket *p, uint32_t n, uint32_t *first)
{
    if (!nw_packet_grow((void **)&p->idx, &p->idxcap, p->nidx + n - 1, sizeof(*p->idx))) {
        return NULL;
    }
    *first = p->nidx;
    p->nidx += n;
    return &p->idx[*first];
}

float (*nw_packet_joints(NwPacket *p, uint32_t n, uint32_t *first))[16]
{
    if (!nw_packet_grow((void **)&p->joints, &p->jointcap, p->njoints + n - 1, sizeof(*p->joints))) {
        return NULL;
    }
    *first = p->njoints;
    p->njoints += n;
    return &p->joints[*first];
}

float *nw_packet_weights(NwPacket *p, uint32_t n, uint32_t *first)
{
    if (!nw_packet_grow((void **)&p->weights, &p->weightcap, p->nweights + n - 1, sizeof(*p->weights))) {
        return NULL;
    }
    *first = p->nweights;
    p->nweights += n;
    return &p->weights[*first];
}

NwVuSpriteVertex *nw_packet_verts(NwPacket *p, uint32_t n, uint32_t *first)
{
    if (!nw_packet_grow((void **)&p->verts, &p->vertcap, p->nverts + n - 1, sizeof(*p->verts))) {
        return NULL;
    }
    *first = p->nverts;
    p->nverts += n;
    return &p->verts[*first];
}

NwPtlList *nw_packet_ptllist(NwPacket *p, uint32_t *index)
{
    if (!nw_packet_grow((void **)&p->ptllists, &p->ptllistcap, p->nptllists, sizeof(*p->ptllists))) {
        return NULL;
    }
    *index = p->nptllists;
    return &p->ptllists[p->nptllists++];
}

NwPtl *nw_packet_ptls(NwPacket *p, uint32_t n, uint32_t *first)
{
    if (!nw_packet_grow((void **)&p->ptls, &p->ptlcap, p->nptls + n - 1, sizeof(*p->ptls))) {
        return NULL;
    }
    *first = p->nptls;
    p->nptls += n;
    return &p->ptls[*first];
}

NwZClut *nw_packet_zclut(NwPacket *p, uint32_t *index)
{
    if (!nw_packet_grow((void **)&p->zcluts, &p->zclutcap, p->nzcluts, sizeof(*p->zcluts))) {
        return NULL;
    }
    *index = p->nzcluts;
    return &p->zcluts[p->nzcluts++];
}

NwWaves *nw_packet_waves(NwPacket *p, uint32_t *index)
{
    if (!nw_packet_grow((void **)&p->waves, &p->wavecap, p->nwaves, sizeof(*p->waves))) {
        return NULL;
    }
    *index = p->nwaves;
    return &p->waves[p->nwaves++];
}

void nw_packet_reset(NwPacket *p)
{
    p->seq = 0;
    p->vbcnt = 0;
    p->ncmds = 0;
    p->nviews = 0;
    p->nmtls = 0;
    p->nlights = 0;
    p->nidx = 0;
    p->njoints = 0;
    p->nweights = 0;
    p->nverts = 0;
    p->nptllists = 0;
    p->nptls = 0;
    p->nzcluts = 0;
    p->nwaves = 0;
}

NwPacket *nw_packet_building(void)
{
    return &nw_slots[nw_building];
}

uint64_t nw_packet_publish(int32_t vbcnt)
{
    nw_packet_lock();
    NwPacket *p = &nw_slots[nw_building];
    uint64_t seq = p->seq = ++nw_seq;
    p->vbcnt = vbcnt;
    int ready = nw_ready;
    nw_ready = nw_building;
    nw_building = ready;
    nw_fresh = true;
    SDL_UnlockMutex(nw_lock);
    nw_packet_reset(&nw_slots[nw_building]);
    return seq;
}

const NwPacket *nw_packet_take(void)
{
    nw_packet_lock();
    if (nw_fresh) {
        int drawing = nw_drawing;
        nw_drawing = nw_ready;
        nw_ready = drawing;
        nw_fresh = false;
    }
    const NwPacket *p = nw_slots[nw_drawing].seq ? &nw_slots[nw_drawing] : NULL;
    SDL_UnlockMutex(nw_lock);
    return p;
}

void nw_gpu_queue(NwGpuOp op, void *obj)
{
    nw_packet_lock();
    if (nw_packet_grow((void **)&nw_jobs, &nw_jobcap, nw_njobs, sizeof(*nw_jobs))) {
        nw_jobs[nw_njobs++] = (NwGpuJob){ op, obj, nw_seq + 1 };
    }
    SDL_UnlockMutex(nw_lock);
}

uint32_t nw_gpu_take(uint64_t seq, NwGpuJob **out, uint32_t *cap)
{
    uint32_t n = 0, kept = 0;
    nw_packet_lock();
    for (uint32_t i = 0; i < nw_njobs; i++) {
        bool free = nw_jobs[i].op >= NW_GPU_FREE_MESH;
        if ((!free || seq > nw_jobs[i].fence) && nw_packet_grow((void **)out, cap, n, sizeof(**out))) {
            (*out)[n++] = nw_jobs[i];
        } else {
            nw_jobs[kept++] = nw_jobs[i];
        }
    }
    nw_njobs = kept;
    SDL_UnlockMutex(nw_lock);
    return n;
}
