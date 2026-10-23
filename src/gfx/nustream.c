/* nu3d/nustream, which the port overrides whole (port/units.toml): the render streams. A stream keeps records of
   its draws instead of DMA packets, in t_base and t_ptr (retail's path-3 texture chain, unused on the host); a flush
   copies them to the frame packet in retail's stream order. base and ptr still point into retail's two output
   buffers for the compiled decomp code that writes raw packets; a stream holding some is reported, not drawn. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nuvport.h"
#include "nucore/nuerror.h"
#include "numath/numtx.h"
#include "numath/nuvec.h"

#define nustream_file "..\\nu2crash.ps2\\nu3d\\nustream.c"
#define NW_STREAMS 32
#define NW_ARENA_BLOCK 0x40000

void *NuMemAllocFn(int size, char *file, int line);

extern int gs_top;
extern volatile int nuvideo_global_vbcnt;
extern float NuTrigTable[];
extern u8 *rndrstream_free;
extern struct rndrstream_s *rndrstream_3d_u;
extern struct rndrstream_s *rndrstream_2d;
extern struct rndrstream_s *rndrstream_fx;
extern char vu_load_faceon_mpg[], vu_load_tricu_mpg[], vu_load_tric_mpg[], vu_load_ptl_mpg[], vu_load_fx_mpg[];
extern struct nucamera_s global_camera;
extern struct numtx_s numtx_identity;

struct rndrstream_s *rndrstream_system;
struct rndrstream_s *rndrstream_mtl_sort;
struct rndrstream_s *rndrstream_faceon;
struct rndrstream_s *rndrstream_3d_l;
struct rndrstream_s *rndrstream_ptl;
struct rndrstream_s *rndrstream_ot_sort;

static struct rndrstream_s rndrstream_pool[NW_STREAMS];
static struct rndrstream_s *rndrstream_sorted[NW_STREAMS];
static int rndrstream_count;
static u8 *stream_outbuf[2];
static int stream_outbuf_size;
static int rndrstream_buffer_index;

/* The records and view copies of a frame, in blocks that never move; one arena per output buffer, so a material
   stream left unflushed at a swap keeps its records for the next frame as retail keeps its packets. */
typedef struct NwBlock {
    struct NwBlock *next;
    size_t used;
    _Alignas(16) unsigned char data[NW_ARENA_BLOCK];
} NwBlock;

typedef struct NwArena {
    NwBlock *first, *cur;
} NwArena;

static NwArena nw_arena[2];

struct NwViewSnap {
    NwView view;
    uint64_t frame; /* the packet it was copied to (nw_frame + 1), and where */
    uint32_t index;
};

struct NwLightSnap {
    NwLights lights;
    uint64_t frame;
    uint32_t index;
};

struct NwJointSnap {
    uint64_t frame;
    uint32_t index, n;
    _Alignas(16) float mtx[][16];
};

static NwView nw_view;             /* what SetViewMtx, SetXYOffset and SetScissor set */
static NwViewSnap *nw_view_snap;   /* its copy in the arena; NULL after a change */
static NwLights nw_lights;         /* what SetDirectionalLights and SetAmbientLight set */
static NwLightSnap *nw_light_snap; /* its copy in the arena; NULL after a change */
static NwLights nw_spec, nw_fx;    /* the UV matrices of SetSpecularLight and SetFxMtx */
static NwLightSnap *nw_spec_snap, *nw_fx_snap;
static NwJointSnap *nw_joint_snap; /* nw_rndr_joints' */
static const uint32_t *nw_blend;   /* nw_rndr_blend's */
static struct nuvec_s spec_light_dir = { 0.0f, 0.0f, 1.0f }; /* retail's data */
static struct nuvec_s spec_view_dir = { 0.0f, 0.0f, -1.0f };
static float nw_local[16];       /* NuRndrSetLocalMtx's */
static uint64_t nw_frame;
static const struct numtl_s **nw_snap_key; /* nw_rndr_snap's table, by material address */
static uint32_t *nw_snap_index;
static uint32_t nw_snap_cap;
static const struct rndrstream_s *nw_raw[64];
static unsigned nw_nraw;

void *nw_rndr_alloc(size_t size)
{
    NwArena *a = &nw_arena[rndrstream_buffer_index];
    size = (size + 15) & ~(size_t)15;
    if (!a->cur || a->cur->used + size > NW_ARENA_BLOCK) {
        NwBlock **next = a->cur ? &a->cur->next : &a->first;
        if (!*next && !(*next = SDL_calloc(1, sizeof(NwBlock)))) {
            NuErrorProlog(nustream_file, 0x324)("out of memory for the stream records");
        }
        a->cur = *next;
        a->cur->used = 0;
    }
    void *p = a->cur->data + a->cur->used;
    a->cur->used += size;
    return p;
}

/* The current view and lights, copied to the arena once after each change. */
static NwViewSnap *nw_view_now(void)
{
    if (!nw_view_snap) {
        nw_view_snap = nw_rndr_alloc(sizeof(*nw_view_snap));
        nw_view_snap->view = nw_view;
        nw_view_snap->frame = 0;
    }
    return nw_view_snap;
}

static NwLightSnap *nw_misc_now(int misc)
{
    NwLightSnap **snap = misc == 1 ? &nw_light_snap : misc == 2 ? &nw_spec_snap : misc == 0x40 ? &nw_fx_snap : NULL;
    if (snap && !*snap) {
        *snap = nw_rndr_alloc(sizeof(**snap));
        (*snap)->lights = misc == 1 ? nw_lights : misc == 2 ? nw_spec : nw_fx;
        (*snap)->frame = 0;
    }
    return snap ? *snap : NULL;
}

void nw_rndr_blend(const uint32_t *weights)
{
    nw_blend = weights;
}

struct numtx_s *nw_rndr_joints(uint32_t n)
{
    NwJointSnap *j = nw_rndr_alloc(sizeof(*j) + (size_t)n * sizeof(j->mtx[0]));
    j->frame = 0;
    j->n = n;
    nw_joint_snap = j;
    return (struct numtx_s *)j->mtx;
}

NwRecord *nw_rndr_record(NwCmdOp op, struct numtl_s *mtl, const void *mpg, int misc)
{
    NwRecord *r = nw_rndr_alloc(sizeof(*r));
    memset(r, 0, sizeof(*r));
    r->cmd.op = (uint8_t)op;
    r->cmd.material = mtl;
    r->cmd.mpg = mpg;
    memcpy(r->cmd.local, nw_local, sizeof(nw_local));
    r->view = nw_view_now();
    r->lights = nw_misc_now(misc);
    r->misc = (uint8_t)misc;
    r->joints = op == NW_CMD_SKIN ? nw_joint_snap : NULL;
    r->blend = op == NW_CMD_SKIN ? nw_blend : NULL;
    return r;
}

void nw_rndr_push(struct rndrstream_s *stream, NwRecord *r)
{
    r->next = NULL;
    if (stream->t_ptr.voidptr) {
        ((NwRecord *)stream->t_ptr.voidptr)->next = r;
    } else {
        stream->t_base.voidptr = r;
    }
    stream->t_ptr.voidptr = r;
}

void nw_rndr_latch(NwRecord *r)
{
    r->view = nw_view_now();
    r->lights = nw_misc_now(r->misc);
}

NwCmd *nw_rndr_cmd(struct rndrstream_s *stream, NwCmdOp op)
{
    NwRecord *r = nw_rndr_record(op, stream->mtl, stream->mpg, (int)(stream->attr & 0x43));
    nw_rndr_push(stream, r);
    return &r->cmd;
}

/* Raw packets in a stream: reported once per stream. */
static void nw_rndr_raw(const struct rndrstream_s *stream)
{
    for (unsigned i = 0; i < nw_nraw; i++) {
        if (nw_raw[i] == stream) {
            return;
        }
    }
    if (nw_nraw < SDL_arraysize(nw_raw)) {
        nw_raw[nw_nraw++] = stream;
    }
    fprintf(stderr, NW_APP_NAME ": stream %p (drawpri 0x%08x) holds packets the port does not draw\n",
            (const void *)stream, stream->drawpri);
}

void nw_rndr_splice(struct rndrstream_s *dst, struct rndrstream_s *src)
{
    if (src->base.voidptr && src->ptr.voidptr != src->base.voidptr) {
        nw_rndr_raw(src);
    }
    if (!src->t_base.voidptr) {
        return;
    }
    if (dst->t_ptr.voidptr) {
        ((NwRecord *)dst->t_ptr.voidptr)->next = src->t_base.voidptr;
    } else {
        dst->t_base.voidptr = src->t_base.voidptr;
    }
    dst->t_ptr.voidptr = src->t_ptr.voidptr;
    src->t_base.voidptr = src->t_ptr.voidptr = NULL;
}

NwPacket *nw_rndr_packet(void)
{
    return nw_packet_building();
}

unsigned nw_rndr_raw_streams(void)
{
    return nw_nraw;
}

/* A stream's records to the packet, its views copied once a frame. */
static void nw_rndr_emit(struct rndrstream_s *stream)
{
    for (NwRecord *r = stream->t_base.voidptr; r; r = r->next) {
        NwViewSnap *v = (NwViewSnap *)r->view;
        NwCmd *c = nw_packet_cmd(nw_packet_building());
        if (v->frame != nw_frame + 1) {
            NwView *copy = nw_packet_view(nw_packet_building(), &v->index);
            if (copy) {
                *copy = v->view;
                v->frame = nw_frame + 1;
            }
        }
        NwLightSnap *l = (NwLightSnap *)r->lights;
        if (l && l->frame != nw_frame + 1) {
            NwLights *copy = nw_packet_light(nw_packet_building(), &l->index);
            if (copy) {
                *copy = l->lights;
                l->frame = nw_frame + 1;
            }
        }
        if (c) {
            *c = r->cmd;
            c->view = v->index;
            c->light = l ? l->index : NW_LIGHT_NONE;
            uint32_t *idx = r->idx ? nw_packet_idx(nw_packet_building(), r->cmd.count, &c->first) : NULL;
            if (idx) {
                memcpy(idx, r->idx, r->cmd.count * sizeof(*idx));
            }
            NwJointSnap *j = (NwJointSnap *)r->joints;
            if (j && j->n && j->frame != nw_frame + 1) {
                float(*copy)[16] = nw_packet_joints(nw_packet_building(), j->n, &j->index);
                if (copy) {
                    memcpy(copy, j->mtx, j->n * sizeof(j->mtx[0]));
                    j->frame = nw_frame + 1;
                }
            }
            if (j) {
                c->first = j->index;
                c->count = j->n;
            }
            uint32_t n = r->blend ? r->blend[5] : 0;
            float *w = n ? nw_packet_weights(nw_packet_building(), n, &c->weights) : NULL;
            c->nweights = w ? n : 0;
            if (w) {
                memcpy(w, r->blend + 6, n * sizeof(*w));
            }
        }
    }
    if (stream->base.voidptr && stream->ptr.voidptr != stream->base.voidptr) {
        nw_rndr_raw(stream);
    }
}

void NuRndrStreamResetStream(struct rndrstream_s *stream, int flag)
{
    stream->base.u8 = 0;
    stream->ptr.u8 = 0;
    stream->t_base.u32 = 0;
    stream->t_ptr.u32 = 0;
    if (flag) {
        stream->gs_ptr = gs_top;
        stream->local_mtx_id = -2;
        stream->view_mtx_id = -2;
        stream->scissor_id = -2;
        stream->misc_addr.voidptr = 0;
        stream->tid = 0;
        stream->xyoffset_id = -2;
    }
}

void NuRndrStreamResetStreams(int flag)
{
    for (int i = 0; i < rndrstream_count; i++) {
        rndrstream_pool[i].mtl = 0;
        NuRndrStreamResetStream(&rndrstream_pool[i], flag);
    }
}

/* Retail also opens the system stream with a PRMODECONT; the port draws by records. */
void NuRndrInitStreams(void)
{
    rndrstream_free = stream_outbuf[rndrstream_buffer_index];
    nw_arena[rndrstream_buffer_index].cur = NULL;
    nw_view_snap = NULL;
    nw_light_snap = NULL;
    nw_spec_snap = NULL;
    nw_fx_snap = NULL;
    nw_joint_snap = NULL;
    nw_blend = NULL;
    nw_packet_reset(nw_packet_building());
    nw_frame++;
    NuRndrStreamResetStreams(1);
}

struct rndrstream_s *NuRndrStreamAllocStream(unsigned int sortkey, void *mpgtag, unsigned int flags)
{
    if (rndrstream_count >= NW_STREAMS) {
        return 0;
    }
    struct rndrstream_s *stream = &rndrstream_pool[rndrstream_count];
    memset(stream, 0, sizeof(*stream));
    stream->mpg = mpgtag;
    stream->attr = flags;
    stream->drawpri = sortkey;
    NuRndrStreamResetStream(stream, 1);
    rndrstream_sorted[rndrstream_count] = stream;
    for (int i = 0; i < rndrstream_count; i++) {
        if (sortkey >= rndrstream_sorted[i]->drawpri) {
            memmove(&rndrstream_sorted[i + 1], &rndrstream_sorted[i], (rndrstream_count - i) * sizeof(stream));
            rndrstream_sorted[i] = stream;
            break;
        }
    }
    rndrstream_count++;
    return stream;
}

void NuRndrStreamInit(int size)
{
    rndrstream_count = 0;
    stream_outbuf[0] = NuMemAllocFn(size, nustream_file, 0x207);
    stream_outbuf[1] = NuMemAllocFn(size, nustream_file, 0x208);
    stream_outbuf_size = size;
    rndrstream_system = NuRndrStreamAllocStream(0x7FFFFFFF, 0, 0);
    rndrstream_mtl_sort = NuRndrStreamAllocStream(0x1000000, 0, 0x3D);
    rndrstream_faceon = NuRndrStreamAllocStream(0x1000000, vu_load_faceon_mpg, 0x3C);
    rndrstream_3d_u = NuRndrStreamAllocStream(0x10000000, vu_load_tricu_mpg, 0x3C);
    rndrstream_3d_l = NuRndrStreamAllocStream(0x10000000, vu_load_tric_mpg, 0x3D);
    rndrstream_2d = NuRndrStreamAllocStream(0x100000, 0, 0x10);
    rndrstream_ptl = NuRndrStreamAllocStream(0xFFFFFF, vu_load_ptl_mpg, 0x3C);
    rndrstream_fx = NuRndrStreamAllocStream(0xFFFFFF, vu_load_fx_mpg, 0x3C);
    rndrstream_ot_sort = NuRndrStreamAllocStream(0xFFFFFF, 0, 0x3D);
    rndrstream_buffer_index = 0;
    memset(stream_outbuf[0], 0, size);
    memset(stream_outbuf[1], 0, size);
    NuRndrInitStreams();
}

/* Each flush appends its streams to the frame in drawpri order (retail's sortstreams_enabled is 1); the pool streams
   start over. */
void NuRndrStreamFlush(void)
{
    if (rndrstream_free > stream_outbuf[rndrstream_buffer_index] + stream_outbuf_size) {
        NuErrorProlog(nustream_file, 0x324)("stream buffer overflow");
    }
    for (int i = 0; i < rndrstream_count; i++) {
        nw_rndr_emit(rndrstream_sorted[i]);
    }
    NuRndrStreamResetStreams(1);
}

void NuRndrSwapStreamBuffers(void)
{
    rndrstream_buffer_index = (rndrstream_buffer_index + 1) % 2;
    NuRndrInitStreams();
}

/* The packet's materials as their state blocks read now, as the frame's DMA reads them after the frame is built;
   each once, through an open-addressed table by address. */
static void nw_rndr_snap(NwPacket *p)
{
    uint32_t cap = 64;
    while (cap < p->ncmds * 2) {
        cap *= 2;
    }
    if (cap > nw_snap_cap) {
        SDL_free(nw_snap_key);
        SDL_free(nw_snap_index);
        nw_snap_key = SDL_malloc(cap * sizeof(*nw_snap_key));
        nw_snap_index = SDL_malloc(cap * sizeof(*nw_snap_index));
        nw_snap_cap = nw_snap_key && nw_snap_index ? cap : 0;
        if (!nw_snap_cap) {
            NuErrorProlog(nustream_file, 0)("out of memory for the frame's materials");
        }
    }
    cap = nw_snap_cap;
    memset(nw_snap_key, 0, cap * sizeof(*nw_snap_key));
    for (uint32_t i = 0; i < p->ncmds; i++) {
        NwCmd *c = &p->cmds[i];
        const struct numtl_s *m = c->material;
        c->mtl = NW_MTL_NONE;
        if (!m) {
            continue;
        }
        uint32_t h = (uint32_t)(((uintptr_t)m >> 4) * 2654435761u) & (cap - 1);
        while (nw_snap_key[h] && nw_snap_key[h] != m) {
            h = (h + 1) & (cap - 1);
        }
        if (!nw_snap_key[h]) {
            NwMtlSnap *s = nw_packet_mtl(p, &nw_snap_index[h]);
            if (!s) {
                continue;
            }
            const NwStateBlock *sb = nw_rndr_state(m);
            nw_snap_key[h] = m;
            s->gs = sb->gs;
            memcpy(s->diffuse, sb->diffuse, sizeof(s->diffuse));
            memcpy(s->uvoff, sb->vuattr, sizeof(s->uvoff));
            memcpy(s->row, sb->vuattr + 9, sizeof(s->row));
            s->tex = nw_rndr_texture(m->tid);
            s->surf = nw_ps2tx_surf(m->tid);
        }
        c->mtl = nw_snap_index[h];
    }
}

/* Retail sends the frame's DMA chain to VU1 here; the port hands the frame packet to the renderer, stamped with the
   vblank count. */
void NuRndrVu1Draw(void)
{
    nw_rndr_snap(nw_packet_building());
    nw_packet_publish(nuvideo_global_vbcnt);
}

/* Retail carves qwords from the stream buffer; the room is doubled for the pointer arrays NuHGobjEvalDwa keeps in
   it, whose host pointers are twice as wide. */
void *NuRndrStreamAlloc(int qwords)
{
    size_t size = (size_t)(qwords > 0 ? qwords : 0) * 32;
    void *p = nw_rndr_alloc(size);
    memset(p, 0, size);
    return p;
}

int NuRndrStreamAddMtl(struct rndrstream_s *stream, struct numtl_s *mtl)
{
    if (!NuMtlValid(mtl)) {
        return 0;
    }
    stream->mtl = mtl;
    return 1;
}

/* rndrstream_zfix is 0 in retail (NuRndrSetZFix is unreachable), so vm goes as it is. */
int NuRndrSetViewMtx(struct numtx_s *vm, struct numtx_s *pm, struct numtx_s *clip_pm)
{
    (void)pm;
    (void)clip_pm;
    memcpy(nw_view.vs, vm, sizeof(nw_view.vs));
    nw_view_snap = NULL;
    return 1;
}

int NuRndrSetLocalMtx(struct numtx_s *mtx, int clip)
{
    (void)clip;
    memcpy(nw_local, mtx, sizeof(nw_local));
    return 1;
}

void NuRndrSetXYOffset(int x, int y)
{
    nw_view.xyoffset[0] = x;
    nw_view.xyoffset[1] = y;
    nw_view_snap = NULL;
}

void NuRndrSetScissor(int x0, int y0, int x1, int y1)
{
    nw_view.scissor[0] = x0;
    nw_view.scissor[1] = y0;
    nw_view.scissor[2] = x1;
    nw_view.scissor[3] = y1;
    nw_view_snap = NULL;
}

/* Retail keeps them transposed for the VU (dirx, diry, dirz) and gives the colours the alphas 0, 0, 1, which vu1init
   drops. */
int NuRndrSetDirectionalLights(struct nuvec_s *d0, struct nucolour3_s *c0, struct nuvec_s *d1, struct nucolour3_s *c1,
                               struct nuvec_s *d2, struct nucolour3_s *c2)
{
    const struct nuvec_s *d[3] = { d0, d1, d2 };
    const struct nucolour3_s *c[3] = { c0, c1, c2 };
    for (int i = 0; i < 3; i++) {
        nw_lights.dir[i][0] = d[i]->x;
        nw_lights.dir[i][1] = d[i]->y;
        nw_lights.dir[i][2] = d[i]->z;
        nw_lights.col[i][0] = c[i]->r;
        nw_lights.col[i][1] = c[i]->g;
        nw_lights.col[i][2] = c[i]->b;
    }
    nw_light_snap = NULL;
    return 1;
}

int NuRndrSetAmbientLight(struct nucolour3_s *colour)
{
    nw_lights.ambient[0] = colour->r;
    nw_lights.ambient[1] = colour->g;
    nw_lights.ambient[2] = colour->b;
    nw_light_snap = NULL;
    return 1;
}

/* Retail's UV matrix: aligned to the half vector of the light and the view in camera space, its offset 0.5. Its
   colour (VU 0x10) no program reads. */
int NuRndrSetSpecularLight(struct nuvec_s *dir, struct nucolour4_s *col)
{
    struct nuvec_s v;
    struct numtx_s m;
    const float half = 0.5f;
    (void)col;
    if (dir) {
        spec_light_dir = *dir;
    }
    NuVecInvMtxRotate(&v, &spec_light_dir, &global_camera.mtx);
    NuVecLerp(&v, &v, &spec_view_dir, half);
    NuVecMtxRotate(&v, &v, &global_camera.mtx);
    NuVecNorm(&v, &v);
    m = numtx_identity;
    NuMtxAlignZ(&m, &v);
    m._20 = -m._20;
    m._21 = -m._21;
    m._22 = -m._22;
    m._32 = m._03 = m._13 = m._23 = 0.0f;
    m._30 = half;
    m._31 = half;
    m._33 = 1.0f;
    memcpy(nw_spec.uv, &m, sizeof(nw_spec.uv));
    nw_spec_snap = NULL;
    return 1;
}

int NuRndrSetFxMtx(struct numtx_s *mtx)
{
    memcpy(nw_fx.uv, mtx, sizeof(nw_fx.uv));
    nw_fx_snap = NULL;
    return 1;
}

/* VU 0x16, which faceinit and faceon read: the camera's yaw for facey, the viewport's size for faceon. */
void FaceYDirStream(int angle)
{
    struct nuviewport_s *vp = NuVpGetCurrentViewport();
    nw_view.faceon[0] = NuTrigTable[(u16)(angle + 0x4000)];
    nw_view.faceon[1] = vp->width * 9.765625146e-05f;
    nw_view.faceon[2] = -NuTrigTable[(u16)angle];
    nw_view.faceon[3] = vp->height * 0.0002790178696f;
    nw_view_snap = NULL;
}
