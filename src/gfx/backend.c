#include "gfx/backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "data/image_tables.h"
#include "frontend/debug.h"
#include "gfx/gl.h"
#include "gfx/interp.h"
#include "gfx/vu.h"
#include "platform/paths.h"

#define NW_BE_SURFS 16

/* GS memory the packets draw into and read back: the back and front buffers and the render targets, by GS address.
   Each is a texture of the power-of-two size the GS addresses it at, n pixels across a GS pixel, drawn with GS row y
   at rows from n * (th - 1 - y). */
typedef struct NwBeSurf {
    int32_t addr; /* NW_SURF_BACK, NW_SURF_FRONT, a GS address, 0 for the targets the port does not draw */
    int tw, th;   /* GS pixels */
    int n;        /* the internal scale it was made at */
    GLuint tex, depth, fbo;
    GLuint ms_fbo, ms_col, ms_depth; /* MSAA: where the draws go, resolved into tex and depth before a read */
    int ms_w, ms_h, samples;         /* their storage */
    bool ms;                         /* the draws go to ms_fbo */
    bool dirty;                      /* ms_fbo holds draws tex and depth lack */
} NwBeSurf;

static struct {
    NwBeSurf surf[NW_BE_SURFS];
    uint32_t nsurf;
    NwBeSurf copy;         /* a target a draw also reads, copied */
    NwBeSurf zcopy;        /* the target's Z a depth CLUT reads, copied */
    GLuint zclut;          /* a depth CLUT's entries */
    NwBeSurf *target;      /* what the draws go to */
    bool alpha;            /* the target keeps alpha (not PSMCT24) */
    int width, height;     /* the target's pixels */
    int shown_w, shown_h;  /* those of the back buffer */
    uint64_t drawn;        /* the last packet run */
    float drawn_t;         /* at that blend */
    NwGpuJob *jobs;
    uint32_t jobcap;
    GLuint rect_vao, rect_vbo, rect_ibo; /* the packet's sprites, four vertices each */
    GLuint ot_ibo;                       /* the packet's OT triangles */
    GLuint pool, pool_tex;               /* the packet's joint palettes, a texture buffer on unit 3 */
    GLuint weights, weights_tex;         /* its blend-shape weights, on unit 4 */
    GLuint verts_vao, verts_vbo;         /* its VERTS triangles */
    GLuint ptl_vao, ptl_vbo;             /* its particles */
    NwVifVertex *rect_verts;
    uint32_t rect_cap;
    bool wireframe;
    NwRenderOptions opts;
    int scale;          /* the frame's */
    int samples;        /* the frame's MSAA, 0 off */
    GLint max_tex;      /* GL_MAX_TEXTURE_SIZE */
    GLint max_samples;  /* GL_MAX_SAMPLES */
    GLuint present_fbo; /* a mip level of the back buffer, to shrink it from */
} nw_be;

/* A command with no material: GS defaults, untextured. */
static const NwVuMaterial nw_be_default = { { 0 }, { 0 }, { 0 }, NULL, { .ztst = 2 }, NULL, NULL };

static void nw_be_resolve(NwBeSurf *s);

bool nw_backend_save(const char *path)
{
    NwBeSurf *back = NULL;
    for (uint32_t i = 0; i < nw_be.nsurf && !back; i++) {
        back = nw_be.surf[i].addr == NW_SURF_BACK ? &nw_be.surf[i] : NULL;
    }
    SDL_Surface *shot = NULL;
    if (!back) {
        SDL_SetError("nothing drawn yet");
    } else if ((shot = SDL_CreateSurface(nw_be.shown_w * back->n, nw_be.shown_h * back->n, SDL_PIXELFORMAT_RGBX32))) {
        nw_be_resolve(back);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
        for (int y = 0; y < shot->h; y++) {
            glReadPixels(0, back->th * back->n - 1 - y, shot->w, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                         (uint8_t *)shot->pixels + (size_t)y * (size_t)shot->pitch);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    bool ok = shot && SDL_SavePNG(shot, path);
    SDL_DestroySurface(shot);
    return ok;
}

static void nw_be_screenshot(void *user)
{
    (void)user;
    char *path = nw_debug_file(NW_PATHS_SCREENSHOTS, "png");
    if (path && nw_backend_save(path)) {
        printf("screenshot: %s\n", path);
    } else {
        fprintf(stderr, "screenshot: %s\n", SDL_GetError());
    }
    SDL_free(path);
}

bool nw_backend_init(void)
{
    nw_be.opts.scale = 1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &nw_be.max_tex);
    glGetIntegerv(GL_MAX_SAMPLES, &nw_be.max_samples);
    nw_debug_toggle("Render", "Wireframe", &nw_be.wireframe);
    nw_debug_action("Render", "Screenshot", SDL_SCANCODE_F12, nw_be_screenshot, NULL);
    return nw_vu_init();
}

static void nw_be_ms_free(NwBeSurf *s)
{
    glDeleteFramebuffers(1, &s->ms_fbo);
    glDeleteRenderbuffers(1, &s->ms_col);
    glDeleteRenderbuffers(1, &s->ms_depth);
    s->ms_fbo = s->ms_col = s->ms_depth = 0;
    s->ms_w = s->ms_h = s->samples = 0;
}

static void nw_be_surf_free(NwBeSurf *s)
{
    glDeleteFramebuffers(1, &s->fbo);
    glDeleteTextures(1, &s->tex);
    glDeleteTextures(1, &s->depth);
    nw_be_ms_free(s);
    memset(s, 0, sizeof(*s));
}

/* The framebuffer s's draws go to. */
static GLuint nw_be_fbo(const NwBeSurf *s)
{
    return s->ms ? s->ms_fbo : s->fbo;
}

/* s's multisampled draws into its texture and Z, the same size, so the blit resolves them; then the target's
   framebuffer again. */
static void nw_be_resolve(NwBeSurf *s)
{
    if (!s->dirty) {
        return;
    }
    int w = s->tw * s->n, h = s->th * s->n;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s->ms_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->fbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                      GL_NEAREST);
    s->dirty = false;
    glBindFramebuffer(GL_FRAMEBUFFER, nw_be.target ? nw_be_fbo(nw_be.target) : 0);
}

/* s's multisampled buffers at its size and the frame's samples, black, under its own Z or shared's (the back
   buffer's); leaves them bound. */
static void nw_be_ms(NwBeSurf *s, const NwBeSurf *shared)
{
    int w = s->tw * s->n, h = s->th * s->n;
    if (!s->ms_fbo) {
        glGenFramebuffers(1, &s->ms_fbo);
        glGenRenderbuffers(1, &s->ms_col);
        glGenRenderbuffers(1, &s->ms_depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->ms_fbo);
    if (s->ms_w != w || s->ms_h != h || s->samples != nw_be.samples) {
        glBindRenderbuffer(GL_RENDERBUFFER, s->ms_col);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, nw_be.samples, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, s->ms_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, nw_be.samples, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->ms_col);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s->ms_depth);
        s->ms_w = w;
        s->ms_h = h;
        s->samples = nw_be.samples;
        s->dirty = false;
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              shared ? shared->ms_depth : s->ms_depth);
}

void nw_backend_shutdown(void)
{
    for (uint32_t i = 0; i < nw_be.nsurf; i++) {
        nw_be_surf_free(&nw_be.surf[i]);
    }
    nw_be_surf_free(&nw_be.copy);
    nw_be_surf_free(&nw_be.zcopy);
    glDeleteTextures(1, &nw_be.zclut);
    glDeleteVertexArrays(1, &nw_be.rect_vao);
    glDeleteBuffers(1, &nw_be.rect_vbo);
    glDeleteBuffers(1, &nw_be.rect_ibo);
    glDeleteBuffers(1, &nw_be.ot_ibo);
    glDeleteBuffers(1, &nw_be.pool);
    glDeleteTextures(1, &nw_be.pool_tex);
    glDeleteBuffers(1, &nw_be.weights);
    glDeleteTextures(1, &nw_be.weights_tex);
    glDeleteVertexArrays(1, &nw_be.verts_vao);
    glDeleteBuffers(1, &nw_be.verts_vbo);
    glDeleteVertexArrays(1, &nw_be.ptl_vao);
    glDeleteBuffers(1, &nw_be.ptl_vbo);
    glDeleteFramebuffers(1, &nw_be.present_fbo);
    nw_vu_shutdown();
    SDL_free(nw_be.jobs);
    SDL_free(nw_be.rect_verts);
    memset(&nw_be, 0, sizeof(nw_be));
}

/* A face-on geom's sprites as quads of corners (two triangles each), their s and t from (0, 0) to (1, 1). */
static void nw_be_sprites_upload(NwGpuSprites *s)
{
    uint32_t n = s->sprites.count;
    NwVuSpriteVertex *v = SDL_malloc((size_t)n * 4 * sizeof(*v) + 1);
    uint32_t *idx = SDL_malloc((size_t)n * 6 * sizeof(*idx) + 1);
    if (!v || !idx) {
        SDL_free(v);
        SDL_free(idx);
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        static const uint32_t quad[6] = { 0, 1, 2, 1, 3, 2 };
        const NwVifSprite *sp = &s->sprites.sprites[i];
        for (int k = 0; k < 4; k++) {
            NwVuSpriteVertex *c = &v[i * 4 + k];
            const float pos[4] = { sp->pos[0], sp->pos[1], sp->pos[2], 0.0f };
            const float tex[4] = { (float)(k & 1), (float)(k >> 1), sp->size[0], sp->size[1] };
            memcpy(c->pos, pos, sizeof(pos));
            memcpy(c->tex, tex, sizeof(tex));
            memcpy(c->col, sp->col, 4);
        }
        for (int k = 0; k < 6; k++) {
            idx[i * 6 + k] = i * 4 + quad[k];
        }
    }
    glGenVertexArrays(1, &s->vao);
    glBindVertexArray(s->vao);
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * 4 * (GLsizeiptr)sizeof(*v), v, GL_STATIC_DRAW);
    glGenBuffers(1, &s->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)n * 6 * (GLsizeiptr)sizeof(*idx), idx, GL_STATIC_DRAW);
    nw_vu_sprite_attribs();
    SDL_free(v);
    SDL_free(idx);
}

static void nw_be_skin_upload(NwGpuSkin *s)
{
    glGenVertexArrays(1, &s->vao);
    glBindVertexArray(s->vao);
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)s->skin.nverts * (GLsizeiptr)sizeof(NwVifSkinVertex), s->skin.verts,
                 GL_STATIC_DRAW);
    glGenBuffers(1, &s->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)s->skin.ntris * 12, s->skin.tris, GL_STATIC_DRAW);
    nw_vu_skin_attribs();
    if (s->skin.ndeltas) {
        glGenBuffers(1, &s->deltas);
        glBindBuffer(GL_TEXTURE_BUFFER, s->deltas);
        glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)s->skin.ndeltas * (GLsizeiptr)sizeof(*s->skin.deltas),
                     s->skin.deltas, GL_STATIC_DRAW);
        glGenTextures(1, &s->delta_tex);
        glBindTexture(GL_TEXTURE_BUFFER, s->delta_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, s->deltas);
    }
}

static void nw_be_job(const NwGpuJob *job)
{
    NwGpuMesh *m = job->obj;
    NwGpuTex *t = job->obj;
    NwGpuSkin *sk = job->obj;
    switch (job->op) {
    case NW_GPU_UPLOAD_MESH:
        glGenVertexArrays(1, &m->vao);
        glBindVertexArray(m->vao);
        glGenBuffers(1, &m->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m->mesh.nverts * (GLsizeiptr)sizeof(NwVifVertex), m->mesh.verts,
                     GL_STATIC_DRAW);
        glGenBuffers(1, &m->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)m->mesh.ntris * 12, m->mesh.tris, GL_STATIC_DRAW);
        nw_vu_attribs();
        break;
    case NW_GPU_UPLOAD_TEX:
        nw_vu_texture(&t->gl, &t->tex);
        break;
    case NW_GPU_FREE_MESH:
        glDeleteVertexArrays(1, &m->vao);
        glDeleteBuffers(1, &m->vbo);
        glDeleteBuffers(1, &m->ibo);
        nw_vif_free(&m->mesh);
        SDL_free(m);
        break;
    case NW_GPU_FREE_TEX:
        nw_vu_texture_free(&t->gl);
        nw_tex_free(&t->tex);
        SDL_free(t);
        break;
    case NW_GPU_UPLOAD_SPRITES:
        nw_be_sprites_upload(job->obj);
        break;
    case NW_GPU_FREE_SPRITES:
        glDeleteVertexArrays(1, &((NwGpuSprites *)job->obj)->vao);
        glDeleteBuffers(1, &((NwGpuSprites *)job->obj)->vbo);
        glDeleteBuffers(1, &((NwGpuSprites *)job->obj)->ibo);
        nw_vif_sprites_free(&((NwGpuSprites *)job->obj)->sprites);
        SDL_free(job->obj);
        break;
    case NW_GPU_UPLOAD_SKIN:
        nw_be_skin_upload(sk);
        break;
    case NW_GPU_FREE_SKIN:
        glDeleteVertexArrays(1, &sk->vao);
        glDeleteBuffers(1, &sk->vbo);
        glDeleteBuffers(1, &sk->ibo);
        glDeleteBuffers(1, &sk->deltas);
        glDeleteTextures(1, &sk->delta_tex);
        nw_vif_skin_free(&sk->skin);
        SDL_free(sk);
        break;
    case NW_GPU_FREE_HOST:
        SDL_free(job->obj);
        break;
    }
}

/* (Re)makes a surface tw x th at the frame's scale, black: colour and a 24-bit Z, as the GS has, the Z a texture a
   depth CLUT reads (with a stencil); one that only changes scale keeps its picture. Leaves its framebuffer bound. */
static void nw_be_surf_make(NwBeSurf *s, int32_t addr, int tw, int th)
{
    int n = nw_be.scale;
    if (s->tw != tw || s->th != th || s->n != n) {
        nw_be_resolve(s);
    }
    NwBeSurf old = *s;
    bool rescale = s->fbo && s->tw == tw && s->th == th && s->n != n;
    old.ms_fbo = old.ms_col = old.ms_depth = 0; /* s keeps them, remade at its size when drawn */
    if (!s->fbo || rescale) {
        glGenFramebuffers(1, &s->fbo);
        glGenTextures(1, &s->tex);
        glGenTextures(1, &s->depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    if (s->tw != tw || s->th != th || s->n != n) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw * n, th * n, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, s->depth);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, tw * n, th * n, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, NULL);
        glActiveTexture(GL_TEXTURE0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s->depth, 0);
        s->tw = tw;
        s->th = th;
        s->n = n;
        s->dirty = false;
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (rescale) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, old.fbo);
            glBlitFramebuffer(0, 0, tw * old.n, th * old.n, 0, 0, tw * n, th * n, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            nw_be_surf_free(&old);
            glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        }
    }
    s->addr = addr;
}

/* The surface at a GS address, made at tw x th if it is new or its size changed; NULL with none left. */
static NwBeSurf *nw_be_surf(int32_t addr, int tw, int th)
{
    NwBeSurf *s = NULL;
    for (uint32_t i = 0; i < nw_be.nsurf && !s; i++) {
        s = nw_be.surf[i].addr == addr ? &nw_be.surf[i] : NULL;
    }
    if (!s && nw_be.nsurf < NW_BE_SURFS) {
        s = &nw_be.surf[nw_be.nsurf++];
    }
    if (s) {
        nw_be_surf_make(s, addr, tw, th);
    }
    return s;
}

/* The draws go to s, width x height GS pixels at its top left. */
static void nw_be_bind(NwBeSurf *s, int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, nw_be_fbo(s));
    s->dirty |= s->ms;
    glViewport(0, (s->th - height) * s->n, width * s->n, height * s->n);
    nw_be.target = s;
    nw_be.width = width;
    nw_be.height = height;
    if (s->addr == NW_SURF_BACK) {
        nw_be.shown_w = width;
        nw_be.shown_h = height;
    }
}

/* A render target takes the back buffer's Z when it is drawn under it and its size, else its own; under a
   multisampled back buffer's it is multisampled too, the front buffer aside (the back buffer is copied into its
   texture). */
static void nw_be_target(const NwCmd *c)
{
    NwSurf surf = c->target.surf;
    if (!surf.addr) {
        surf.tw = (uint16_t)c->target.width;
        surf.th = (uint16_t)c->target.height;
    }
    NwBeSurf *back = NULL;
    for (uint32_t i = 0; i < nw_be.nsurf && !back; i++) {
        back = nw_be.surf[i].addr == NW_SURF_BACK ? &nw_be.surf[i] : NULL;
    }
    NwBeSurf *s = nw_be_surf(surf.addr, surf.tw, surf.th);
    if (s) {
        bool shared =
            c->target.zback && back && back != s && back->tw == s->tw && back->th == s->th && back->n == s->n;
        bool ms = shared && back->ms && s->addr != NW_SURF_FRONT;
        if (!ms) {
            nw_be_resolve(s);
            if (!nw_be.samples) {
                nw_be_ms_free(s);
            }
            if (shared) {
                nw_be_resolve(back);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   shared ? back->depth : s->depth, 0);
        } else {
            nw_be_ms(s, back);
        }
        s->ms = ms;
        nw_be_bind(s, c->target.width, c->target.height);
        nw_be.alpha = surf.tcc || !surf.addr;
        nw_vu_alpha_writes(nw_be.alpha);
    }
}

/* A surface for a draw to read: the target itself is read from a copy, as a texture cannot be both. */
static const NwVuTexture *nw_be_surf_tex(const NwSurf *surf, bool tcc)
{
    static NwVuTexture tex;
    NwBeSurf *s = NULL;
    for (uint32_t i = 0; i < nw_be.nsurf && !s; i++) {
        s = nw_be.surf[i].addr == surf->addr && nw_be.surf[i].fbo ? &nw_be.surf[i] : NULL;
    }
    if (!s) {
        return NULL;
    }
    if (s == nw_be.target) {
        nw_be_surf_make(&nw_be.copy, 0, s->tw, s->th);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, nw_be_fbo(s));
        glBlitFramebuffer(0, 0, s->tw * s->n, s->th * s->n, 0, 0, s->tw * s->n, s->th * s->n, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, nw_be_fbo(s));
        s = &nw_be.copy;
    } else {
        nw_be_resolve(s);
    }
    tex = (NwVuTexture){ s->tex, 0, tcc, true };
    return &tex;
}

/* The target as a texture a draw blends over, copied. */
static const NwVuTexture *nw_be_dst(void)
{
    static NwVuTexture tex;
    NwBeSurf *s = nw_be.target;
    nw_be_surf_make(&nw_be.copy, 0, s->tw, s->th);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, nw_be_fbo(s));
    glBlitFramebuffer(0, 0, s->tw * s->n, s->th * s->n, 0, 0, s->tw * s->n, s->th * s->n, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, nw_be_fbo(s));
    tex = (NwVuTexture){ nw_be.copy.tex, 0, true, true };
    return &tex;
}

/* A depth CLUT for a draw on the target: its Z copied, as a texture cannot be both, and the entries uploaded. */
static const NwVuZClut *nw_be_zclut(const NwZClut *z)
{
    static NwVuZClut vz;
    NwBeSurf *s = nw_be.target;
    nw_be_surf_make(&nw_be.zcopy, 0, s->tw, s->th);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, nw_be_fbo(s));
    glBlitFramebuffer(0, 0, s->tw * s->n, s->th * s->n, 0, 0, s->tw * s->n, s->th * s->n, GL_DEPTH_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, nw_be_fbo(s));
    glActiveTexture(GL_TEXTURE0 + 7);
    if (!nw_be.zclut) {
        glGenTextures(1, &nw_be.zclut);
        glBindTexture(GL_TEXTURE_2D, nw_be.zclut);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, 256, 1, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);
    }
    glBindTexture(GL_TEXTURE_2D, nw_be.zclut);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, z->clut);
    glActiveTexture(GL_TEXTURE0);
    vz = (NwVuZClut){ nw_be.zcopy.depth, nw_be.zclut, z->shift, z->base, z->far, z->near,
                      { z->spot ? 2u : z->clear ? 1u : 0u, z->clear_z[0], z->clear_z[1] }, z->alpha };
    return &vz;
}

/* The matrix from GS coordinates (12.4 pixels from XYOFFSET, Z 0 .. 2^23 - 1 with the nearer point larger) to GL
   clip space for a width x height target, where the nearer point is the smaller. */
static void nw_be_gs(const NwView *v, int width, int height, float g[16])
{
    memset(g, 0, 16 * sizeof(float));
    g[0] = 1.0f / (8.0f * (float)width);
    g[5] = -1.0f / (8.0f * (float)height);
    g[10] = -2.0f / 8388607.0f;
    g[12] = -(float)v->xyoffset[0] / (8.0f * (float)width) - 1.0f;
    g[13] = (float)v->xyoffset[1] / (8.0f * (float)height) + 1.0f;
    g[14] = 1.0f;
    g[15] = 1.0f;
}

/* The view's vs, which ends in GS coordinates, then to GL clip space. */
static void nw_be_screen(const NwView *v, int width, int height, float out[16])
{
    float g[16];
    nw_be_gs(v, width, height, g);
    NwVuObject o;
    nw_vu1init(&o, v->vs, g);
    memcpy(out, o.lv, sizeof(o.lv));
}

/* The material a command draws with: its snapshot, or GS defaults (untextured) without one. */
static NwVuMaterial nw_be_material(const NwPacket *p, const NwCmd *c, bool uvrow)
{
    NwVuMaterial m = nw_be_default;
    if (c->mtl != NW_MTL_NONE) {
        const NwMtlSnap *s = &p->mtls[c->mtl];
        memcpy(m.diffuse, s->diffuse, sizeof(m.diffuse));
        memcpy(m.vu, s->vu, sizeof(m.vu));
        m.uvoff[0] = s->vu[0];
        m.uvoff[1] = s->vu[1];
        m.uvoff[2] = uvrow ? (float)s->row[0] : 0.0f;
        m.uvoff[3] = uvrow ? (float)s->row[1] : 0.0f;
        m.tex = s->surf.addr ? nw_be_surf_tex(&s->surf, s->surf.tcc) : s->tex ? &s->tex->gl : NULL;
        m.state = s->gs;
    }
    return m;
}

static void nw_be_passes(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, GLsizei count,
                         const void *first)
{
    NwMtlPass pass[2];
    uint32_t n = nw_vu_use(program, o, m, pass);
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, m, &pass[k]);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, first);
    }
}

/* The GS clears by drawing sprites over the scissor rectangle with ZTST ALWAYS and the alpha test keeping colour or
   Z alone; the alpha written is the colour's, 0x80 = 1.0. */
static void nw_be_clear(const NwPacket *p, const NwCmd *c)
{
    const int32_t *sc = p->views[c->view].scissor;
    int n = nw_be.target->n;
    glEnable(GL_SCISSOR_TEST);
    glScissor(sc[0] * n, (nw_be.target->th - 1 - sc[3]) * n, (sc[2] - sc[0] + 1) * n, (sc[3] - sc[1] + 1) * n);
    bool rgb = c->clear.mask & 1, z = c->clear.mask & 2;
    glColorMask(rgb, rgb, rgb, rgb && nw_be.alpha);
    glDepthMask(z);
    glClearColor(c->clear.rgba[0] / 255.0f, c->clear.rgba[1] / 255.0f, c->clear.rgba[2] / 255.0f,
                 SDL_min(c->clear.rgba[3] / 128.0f, 1.0f));
    glClearDepth(1.0 - c->clear.z / 8388607.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

/* The packet's sprites into one vertex buffer, a quad of two triangles each, before they are drawn: GS coordinates
   as positions under the unlit program, s and t x4096 as texcoords. */
static void nw_be_rects(const NwPacket *p)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        n += p->cmds[i].op == NW_CMD_RECT;
    }
    if (!n) {
        return;
    }
    if (n > nw_be.rect_cap) {
        NwVifVertex *verts = SDL_realloc(nw_be.rect_verts, (size_t)n * 4 * sizeof(*verts));
        uint32_t *idx = SDL_malloc((size_t)n * 6 * sizeof(*idx));
        if (!verts || !idx) {
            SDL_free(idx);
            return;
        }
        for (uint32_t r = 0; r < n; r++) {
            static const uint32_t quad[6] = { 0, 1, 2, 1, 3, 2 };
            for (int k = 0; k < 6; k++) {
                idx[r * 6 + k] = r * 4 + quad[k];
            }
        }
        if (!nw_be.rect_vao) {
            glGenVertexArrays(1, &nw_be.rect_vao);
            glGenBuffers(1, &nw_be.rect_vbo);
            glGenBuffers(1, &nw_be.rect_ibo);
        }
        glBindVertexArray(nw_be.rect_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.rect_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)n * 6 * (GLsizeiptr)sizeof(*idx), idx, GL_STATIC_DRAW);
        SDL_free(idx);
        nw_be.rect_verts = verts;
        nw_be.rect_cap = n;
    }
    NwVifVertex *v = nw_be.rect_verts;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        if (c->op != NW_CMD_RECT) {
            continue;
        }
        for (int k = 0; k < 4; k++, v++) {
            memset(v, 0, sizeof(*v));
            v->pos[0] = (float)c->rect.xy[k & 1 ? 2 : 0];
            v->pos[1] = (float)c->rect.xy[k & 2 ? 3 : 1];
            v->tex[0] = (int16_t)(c->rect.st[k & 1 ? 2 : 0] * 4096.0f);
            v->tex[1] = (int16_t)(c->rect.st[k & 2 ? 3 : 1] * 4096.0f);
            memcpy(v->col, c->rect.rgba, 4);
        }
    }
    glBindVertexArray(nw_be.rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, nw_be.rect_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v - nw_be.rect_verts) * (GLsizeiptr)sizeof(*v), nw_be.rect_verts,
                 GL_STREAM_DRAW);
    nw_vu_attribs();
}

static void nw_be_rect(const NwPacket *p, const NwCmd *c, uint32_t index)
{
    NwVuObject o;
    float g[16];
    nw_be_gs(&p->views[c->view], nw_be.width, nw_be.height, g);
    memset(&o, 0, sizeof(o));
    memcpy(o.lv, g, sizeof(o.lv));
    NwVuMaterial m = nw_be_material(p, c, false);
    glBindVertexArray(nw_be.rect_vao);
    nw_be_passes(NW_VU_TRICU, &o, &m, 6, (const void *)(uintptr_t)(index * 6 * sizeof(uint32_t)));
}

/* The packet's VERTS triangles into one vertex buffer before they are drawn. */
static void nw_be_verts(const NwPacket *p)
{
    if (!p->nverts) {
        return;
    }
    if (!nw_be.verts_vao) {
        glGenVertexArrays(1, &nw_be.verts_vao);
        glGenBuffers(1, &nw_be.verts_vbo);
    }
    glBindVertexArray(nw_be.verts_vao);
    glBindBuffer(GL_ARRAY_BUFFER, nw_be.verts_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)p->nverts * (GLsizeiptr)sizeof(*p->verts), p->verts, GL_STREAM_DRAW);
    nw_vu_sprite_attribs();
}

/* The packet's particles into one vertex buffer before they are drawn. */
static void nw_be_ptls(const NwPacket *p)
{
    if (!p->nptls) {
        return;
    }
    if (!nw_be.ptl_vao) {
        glGenVertexArrays(1, &nw_be.ptl_vao);
        glGenBuffers(1, &nw_be.ptl_vbo);
    }
    glBindVertexArray(nw_be.ptl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, nw_be.ptl_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)p->nptls * (GLsizeiptr)sizeof(*p->ptls), p->ptls, GL_STREAM_DRAW);
}

/* A package whose program the port does not draw, once per package and kind of geom. */
static void nw_be_undrawn(const NwVuProg *mpg, bool skin)
{
    static const NwVuProg *seen[2][16];
    for (int i = 0; i < 16 && seen[skin][i] != mpg; i++) {
        if (!seen[skin][i]) {
            seen[skin][i] = mpg;
            fprintf(stderr, NW_APP_NAME ": %s: its %s program is not drawn\n", mpg ? mpg->name : "no package",
                    skin ? "skin" : "triangle");
            break;
        }
    }
}

/* What vu1init leaves for a draw: the local times screen matrix, and the stream's block at VU 0x0C, the lights or for
   the xuv and glass programs the UV matrix; and where the target's pixels are in GS coordinates, the GS pixel at
   12.4 (x, y) from XYOFFSET being GL's (x, th - 1 - y) about its centre, each n x n pixels at scale n. */
static void nw_be_object(const NwPacket *p, const NwCmd *c, NwVuProgram program, NwVuObject *o)
{
    float screen[16];
    const NwView *v = &p->views[c->view];
    nw_be_screen(v, nw_be.width, nw_be.height, screen);
    nw_vu1init(o, c->local, screen);
    o->frag[0] = 16.0f / (float)nw_be.target->n;
    o->frag[1] = -16.0f / (float)nw_be.target->n;
    o->frag[2] = (float)v->xyoffset[0] - 8.0f;
    o->frag[3] = (float)(nw_be.target->th * 16 + v->xyoffset[1]) - 8.0f;
    if (c->light != NW_LIGHT_NONE && (program == NW_VU_TRIXUV || program == NW_VU_SKINXUV ||
                                      program == NW_VU_TRIGLASS || program == NW_VU_SKNGLASS)) {
        nw_vu1init_uv(o, c->local, &p->lights[c->light]);
    } else if (c->light != NW_LIGHT_NONE) {
        nw_vu1init_lights(o, c->local, &p->lights[c->light]);
    }
}

/* A VERTS command's triangles: GS coordinates under the GS matrix and unlit, sprites as faceon draws them with GS
   units, or points through the command's program. */
static void nw_be_tris(const NwPacket *p, const NwCmd *c)
{
    NwVuObject o;
    NwVuProgram program = NW_VU_TRICU;
    if (c->mode == NW_VERTS_GS) {
        memset(&o, 0, sizeof(o));
        nw_be_gs(&p->views[c->view], nw_be.width, nw_be.height, o.lv);
    } else if (c->mode == NW_VERTS_SPRITE) {
        program = NW_VU_FACEON;
        nw_be_object(p, c, program, &o);
        o.sprite[0] = 1.0f / (8.0f * (float)nw_be.width);
        o.sprite[1] = -1.0f / (8.0f * (float)nw_be.height);
    } else {
        const NwVuProg *mpg = c->mpg;
        program = nw_vu_program(mpg ? mpg->name : NULL);
        nw_be_object(p, c, program, &o);
    }
    NwVuMaterial m = nw_be_material(p, c, false);
    if (c->alpha) {
        m.state.alpha = (uint8_t)c->alpha;
    }
    if (c->zclut && c->zclut <= p->nzcluts) {
        m.zclut = nw_be_zclut(&p->zcluts[c->zclut - 1]);
    }
    if (c->exact) {
        m.dst = nw_be_dst();
    }
    NwVuTexture clut;
    if (c->clut && c->clut->gl.clut && m.tex && m.tex->clut) {
        clut = *m.tex;
        clut.clut = c->clut->gl.clut;
        m.tex = &clut;
    }
    NwMtlPass pass[2];
    glBindVertexArray(nw_be.verts_vao);
    uint32_t n = nw_vu_use(program, &o, &m, pass);
    if (c->stencil == NW_STENCIL_SHADOWED) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_LEQUAL, 0x80, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    } else if (c->stencil) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, c->stencil > 0 ? GL_INCR_WRAP : GL_DECR_WRAP);
        nw_vu_pass(program, &m, &pass[0]);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
        glDisable(GL_STENCIL_TEST);
        return;
    }
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, &m, &pass[k]);
        glDrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
    }
    glDisable(GL_STENCIL_TEST);
}

/* Retail's NuLgtHSurf[1], the back buffer's Z squeezed to width pixels as the volumes' viewport is, into the target's
   own Z; and the stencil the volumes count in at 0x7F, as retail's sprites of 0x7F start its colour. */
static void nw_be_volumes(const NwCmd *c)
{
    NwBeSurf *back = NULL, *s = nw_be.target;
    for (uint32_t i = 0; i < nw_be.nsurf && !back; i++) {
        back = nw_be.surf[i].addr == NW_SURF_BACK ? &nw_be.surf[i] : NULL;
    }
    if (back && back != s && !s->ms) { /* a scaling blit neither reads nor writes multisampled buffers */
        nw_be_resolve(back);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
        glBlitFramebuffer(0, (back->th - nw_be.shown_h) * back->n, nw_be.shown_w * back->n, back->th * back->n, 0,
                          (s->th - nw_be.height) * s->n, c->volumes.width * s->n, s->th * s->n, GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    }
    glStencilMask(0xFF);
    glClearStencil(0x7F);
    glClear(GL_STENCIL_BUFFER_BIT);
}

/* A particle group, a quad of each particle: its corners in GS units before the divide, its texel coordinates in
   1/16 of the material texture's level 0. */
static void nw_be_particles(const NwPacket *p, const NwCmd *c)
{
    _Static_assert(sizeof(NwPtlList) == 132 * 16, "NwPtlList is VU 0x3C-0xBF");
    NwVuObject o;
    nw_be_object(p, c, NW_VU_PTL, &o);
    o.sprite[0] = 1.0f / (8.0f * (float)nw_be.width);
    o.sprite[1] = -1.0f / (8.0f * (float)nw_be.height);
    NwVuMaterial m = nw_be_material(p, c, false);
    const NwGpuTex *tex = c->mtl != NW_MTL_NONE ? p->mtls[c->mtl].tex : NULL;
    if (tex && tex->tex.nlevels) {
        o.sprite[2] = 1.0f / (16.0f * (float)tex->tex.levels[0].width);
        o.sprite[3] = 1.0f / (16.0f * (float)tex->tex.levels[0].height);
    }
    NwMtlPass pass[2];
    glBindVertexArray(nw_be.ptl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, nw_be.ptl_vbo);
    nw_vu_ptl_attribs((size_t)c->first * sizeof(NwPtl));
    uint32_t n = nw_vu_use(NW_VU_PTL, &o, &m, pass);
    nw_vu_ptl((const uint32_t(*)[4])&p->ptllists[c->ptllist]);
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(NW_VU_PTL, &m, &pass[k]);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)c->count);
    }
}

/* A geom's mesh; an OT batch draws its triangles in the packet's order through the packet's index buffer. */
static void nw_be_draw(const NwPacket *p, const NwCmd *c)
{
    const NwGpuMesh *mesh = c->mesh;
    const NwVuProg *mpg = c->mpg;
    NwVuProgram program = nw_vu_program(mpg ? mpg->name : NULL);
    if (!mesh || !mesh->vao) {
        return;
    }
    if (program == NW_VU_PROGRAMS) {
        nw_be_undrawn(mpg, false);
        return;
    }
    NwVuObject o;
    nw_be_object(p, c, program, &o);
    NwVuMaterial m = nw_be_material(p, c, mesh->mesh.uvrow);
    glBindVertexArray(mesh->vao);
    if (c->op == NW_CMD_DRAW_OT) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.ot_ibo);
        nw_be_passes(program, &o, &m, (GLsizei)c->count, (const void *)(uintptr_t)(c->first * sizeof(uint32_t)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    } else {
        nw_be_passes(program, &o, &m, (GLsizei)(mesh->mesh.ntris * 3), NULL);
    }
}

/* A skin geom under its draw's palette and blend-shape weights, in the local matrix's space as vu1init sets it up. */
static void nw_be_skin(const NwPacket *p, const NwCmd *c)
{
    const NwGpuSkin *s = c->skin;
    const NwVuProg *mpg = c->mpg;
    NwVuProgram program = nw_vu_skin_program(mpg ? mpg->name : NULL);
    if (!s || !s->vao || c->first + c->count > p->njoints || c->weights + c->nweights > p->nweights) {
        return;
    }
    if (program == NW_VU_PROGRAMS) {
        nw_be_undrawn(mpg, true);
        return;
    }
    NwVuObject o;
    nw_be_object(p, c, program, &o);
    NwVuMaterial m = nw_be_material(p, c, false);
    NwMtlPass pass[2];
    glBindVertexArray(s->vao);
    uint32_t n = nw_vu_use(program, &o, &m, pass);
    nw_vu_skin(program, c->first, c->weights, s->delta_tex ? c->nweights : 0);
    if (s->delta_tex) {
        glActiveTexture(GL_TEXTURE0 + 5);
        glBindTexture(GL_TEXTURE_BUFFER, s->delta_tex);
        glActiveTexture(GL_TEXTURE0);
    }
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, &m, &pass[k]);
        glDrawElements(GL_TRIANGLES, (GLsizei)(s->skin.ntris * 3), GL_UNSIGNED_INT, NULL);
    }
}

/* The packet's blend-shape weights as R32F texels. */
static void nw_be_weights(const NwPacket *p)
{
    if (!p->nweights) {
        return;
    }
    if (!nw_be.weights) {
        glGenBuffers(1, &nw_be.weights);
        glGenTextures(1, &nw_be.weights_tex);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, nw_be.weights);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)p->nweights * (GLsizeiptr)sizeof(*p->weights), p->weights,
                 GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_BUFFER, nw_be.weights_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, nw_be.weights);
    glActiveTexture(GL_TEXTURE0);
}

/* The packet's palettes as RGBA32F texels, four rows a matrix. */
static void nw_be_pool(const NwPacket *p)
{
    nw_be_weights(p);
    if (!p->njoints) {
        return;
    }
    if (!nw_be.pool) {
        glGenBuffers(1, &nw_be.pool);
        glGenTextures(1, &nw_be.pool_tex);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, nw_be.pool);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)p->njoints * (GLsizeiptr)sizeof(*p->joints), p->joints,
                 GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_BUFFER, nw_be.pool_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, nw_be.pool);
    glActiveTexture(GL_TEXTURE0);
}

/* faceon's corners, sizes in GS units before the divide scaled by VU 0x16; facey's quads, along VF15, the camera's
   right (cos yaw, 0, -sin yaw) dotted with the local matrix's rows and normalised (faceinit). */
static void nw_be_sprites(const NwPacket *p, const NwCmd *c)
{
    const NwGpuSprites *s = c->sprites;
    if (!s || !s->vao) {
        return;
    }
    const NwView *v = &p->views[c->view];
    float screen[16];
    nw_be_screen(v, nw_be.width, nw_be.height, screen);
    NwVuObject o;
    nw_vu1init(&o, c->local, screen);
    if (s->sprites.facey) {
        float len = 0.0f;
        for (int r = 0; r < 3; r++) {
            o.sprite[r] = c->local[r * 4] * v->faceon[0] + c->local[r * 4 + 2] * v->faceon[2];
            len += o.sprite[r] * o.sprite[r];
        }
        for (int r = 0; r < 3; r++) {
            o.sprite[r] /= sqrtf(len);
        }
    } else {
        o.sprite[0] = v->faceon[1] / (8.0f * (float)nw_be.width);
        o.sprite[1] = -v->faceon[3] / (8.0f * (float)nw_be.height);
    }
    NwVuMaterial m = nw_be_material(p, c, false);
    glBindVertexArray(s->vao);
    nw_be_passes(s->sprites.facey ? NW_VU_FACEY : NW_VU_FACEON, &o, &m, (GLsizei)(s->sprites.count * 6), NULL);
}

/* A packet runs once into GS memory, which keeps what it drew; it starts on the back buffer, and the front buffer
   is the back buffer as it ends. */
static void nw_be_run(const NwPacket *p, NwBeSurf *back)
{
    back->ms = nw_be.samples > 1;
    if (back->ms) {
        nw_be_ms(back, NULL);
    } else {
        nw_be_resolve(back);
        nw_be_ms_free(back);
    }
    nw_be_bind(back, nw_be.shown_w, nw_be.shown_h);
    nw_be.alpha = true;
    nw_vu_alpha_writes(true);
    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, nw_be.wireframe ? GL_LINE : GL_FILL);
    nw_be_rects(p);
    nw_be_verts(p);
    nw_be_ptls(p);
    nw_be_pool(p);
    if (p->nidx) {
        if (!nw_be.ot_ibo) {
            glGenBuffers(1, &nw_be.ot_ibo);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.ot_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)p->nidx * (GLsizeiptr)sizeof(uint32_t), p->idx,
                     GL_STREAM_DRAW);
    }
    uint32_t rects = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        switch (c->op) {
        case NW_CMD_DRAW:
        case NW_CMD_DRAW_OT:
            nw_be_draw(p, c);
            break;
        case NW_CMD_CLEAR:
            nw_be_clear(p, c);
            break;
        case NW_CMD_RECT:
            nw_be_rect(p, c, rects++);
            break;
        case NW_CMD_SPRITES:
            nw_be_sprites(p, c);
            break;
        case NW_CMD_TARGET:
            nw_be_target(c);
            break;
        case NW_CMD_SKIN:
            nw_be_skin(p, c);
            break;
        case NW_CMD_VERTS:
            nw_be_tris(p, c);
            break;
        case NW_CMD_PARTICLES:
            nw_be_particles(p, c);
            break;
        case NW_CMD_VOLUMES:
            nw_be_volumes(c);
            break;
        case NW_CMD_WAVES:
            if (c->first < p->nwaves) {
                nw_vu_waves(p->waves[c->first].wave);
            }
            break;
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    nw_be_resolve(back);
    NwBeSurf *front = nw_be_surf(NW_SURF_FRONT, back->tw, back->th);
    if (front) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
        glBlitFramebuffer(0, 0, back->tw * back->n, back->th * back->n, 0, 0, front->tw * front->n,
                          front->th * front->n, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    nw_be.drawn = p->seq;
}

/* Ps2TxValToPowerOfTwo: the exponent of the power of two at or above n. */
static int nw_be_log2(int n)
{
    int shift = 0;
    while ((1 << shift) < n) {
        shift++;
    }
    return shift;
}

void nw_backend_set_options(const NwRenderOptions *o)
{
    nw_be.opts = *o;
    nw_vu_filter(o->filter);
}

int nw_backend_scale(void)
{
    return nw_be.scale;
}

int nw_backend_msaa(void)
{
    return nw_be.samples;
}

/* The options' MSAA as the GPU has it: a power of two, at most GL_MAX_SAMPLES; 0 below 2. */
static int nw_be_samples(void)
{
    int n = 1;
    while (n * 2 <= nw_be.opts.msaa && n * 2 <= nw_be.max_samples) {
        n *= 2;
    }
    return n > 1 ? n : 0;
}

/* The scale for a frame tw x th GS pixels (powers of two) shown h pixels high: the options', or auto the least that
   covers h with bh GS rows; at most what the GPU's textures hold. */
static int nw_be_scale(int h, int bh, int tw, int th)
{
    int n = nw_be.opts.scale ? nw_be.opts.scale : (h + bh - 1) / bh;
    n = SDL_clamp(n, 1, NW_BACKEND_SCALE_MAX);
    while (n > 1 && (tw * n > nw_be.max_tex || th * n > nw_be.max_tex)) {
        n--;
    }
    return n;
}

/* The back buffer's bw x bh into the window's w x h at (x, y): a mip level at least that size when it shrinks more
   than twice (glGenerateMipmap averages), then a linear blit. */
static void nw_be_present(NwBeSurf *back, int bw, int bh, int x, int y, int w, int h)
{
    int n = back->n, level = 0;
    nw_be_resolve(back);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    while (level < 12 && (bw * n) >> (level + 1) >= w && (bh * n) >> (level + 1) >= h) {
        level++;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
    if (level) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, back->tex);
        glGenerateMipmap(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0);
        if (!nw_be.present_fbo) {
            glGenFramebuffers(1, &nw_be.present_fbo);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, nw_be.present_fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, back->tex, level);
    }
    glBlitFramebuffer(0, ((back->th - bh) * n) >> level, (bw * n) >> level, (back->th * n) >> level, x, y, x + w,
                      y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

const NwPacket *nw_backend_frame(int width, int height, double now)
{
    const NwPacket *prev;
    const NwPacket *p = nw_packet_take_pair(&prev);
    const NwPacket *run = p;
    float t = 1.0f;
    if (p && now >= 0.0 && nw_interp_redraws(p)) {
        t = nw_interp_t(prev, p, now);
        run = nw_interp_blend(prev, p, t);
    }
    uint32_t njobs = nw_gpu_take(p ? p->seq : 0, &nw_be.jobs, &nw_be.jobcap);
    for (uint32_t i = 0; i < njobs; i++) {
        nw_be_job(&nw_be.jobs[i]);
    }
    /* The back buffer at the size of the packet's first back-buffer target, 640 x 512 before one. */
    int bw = nw_be.shown_w ? nw_be.shown_w : 640, bh = nw_be.shown_h ? nw_be.shown_h : 512;
    for (uint32_t i = 0; p && i < p->ncmds; i++) {
        if (p->cmds[i].op == NW_CMD_TARGET && p->cmds[i].target.surf.addr == NW_SURF_BACK) {
            bw = p->cmds[i].target.width;
            bh = p->cmds[i].target.height;
            break;
        }
    }
    nw_be.shown_w = bw;
    nw_be.shown_h = bh;

    /* The PS2 shows a 640-wide frame at 4:3 whatever its height; a wider one (port/wide.h) keeps those pixels' shape.
       Within 1% of the window's aspect it fills the window. */
    float aspect = 4.0f / 3.0f * (float)bw / 640.0f;
    int w = width, h = height;
    if (h > 0 && SDL_fabsf((float)w / (float)h - aspect) > 0.01f * aspect) {
        if ((float)w > (float)h * aspect) {
            w = (int)((float)h * aspect + 0.5f);
        } else {
            h = (int)((float)w / aspect + 0.5f);
        }
    }
    int tw = 1 << nw_be_log2(bw), th = 1 << nw_be_log2(bh);
    nw_be.scale = nw_be_scale(h, bh, tw, th);
    nw_be.samples = nw_be_samples();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); /* the last pass's masks, which glClear honours */
    glDepthMask(GL_TRUE);
    NwBeSurf *back = nw_be_surf(NW_SURF_BACK, tw, th);
    if (back && p && (p->seq != nw_be.drawn || t != nw_be.drawn_t)) {
        nw_be_run(run, back);
        nw_be.drawn_t = t;
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (back) {
        nw_be_present(back, bw, bh, (width - w) / 2, (height - h) / 2, w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return p;
}
