#include "gfx/backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "data/image_tables.h"
#include "frontend/debug.h"
#include "gfx/gl.h"
#include "gfx/vu.h"
#include "platform/paths.h"

#define NW_BE_SURFS 16

/* GS memory the packets draw into and read back: the back and front buffers and the render targets, by GS address.
   Each is a texture of the power-of-two size the GS addresses it at, drawn with GS row y at row th - 1 - y. */
typedef struct NwBeSurf {
    int32_t addr; /* NW_SURF_BACK, NW_SURF_FRONT, a GS address, 0 for the targets the port does not draw */
    int tw, th;
    GLuint tex, depth, fbo;
} NwBeSurf;

static struct {
    NwBeSurf surf[NW_BE_SURFS];
    uint32_t nsurf;
    NwBeSurf copy;         /* a target a draw also reads, copied */
    NwBeSurf *target;      /* what the draws go to */
    int width, height;     /* the target's pixels */
    int shown_w, shown_h;  /* those of the back buffer */
    uint64_t drawn;        /* the last packet run */
    NwGpuJob *jobs;
    uint32_t jobcap;
    GLuint rect_vao, rect_vbo, rect_ibo; /* the packet's sprites, four vertices each */
    GLuint ot_ibo;                       /* the packet's OT triangles */
    GLuint pool, pool_tex;               /* the packet's joint palettes, a texture buffer on unit 3 */
    NwVifVertex *rect_verts;
    uint32_t rect_cap;
    bool wireframe;
} nw_be;

/* A command with no material: GS defaults, untextured. */
static const NwVuMaterial nw_be_default = { { 0 }, { 0 }, NULL, { .ztst = 2 } };

/* The back buffer at its own size, without the overlay. */
static void nw_be_screenshot(void *user)
{
    (void)user;
    const NwBeSurf *back = NULL;
    for (uint32_t i = 0; i < nw_be.nsurf && !back; i++) {
        back = nw_be.surf[i].addr == NW_SURF_BACK ? &nw_be.surf[i] : NULL;
    }
    char *path = NULL;
    SDL_Surface *shot = NULL;
    if (!back) {
        SDL_SetError("nothing drawn yet");
    } else if ((path = nw_debug_file(NW_PATHS_SCREENSHOTS, "png")) &&
               (shot = SDL_CreateSurface(nw_be.shown_w, nw_be.shown_h, SDL_PIXELFORMAT_RGBX32))) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
        for (int y = 0; y < shot->h; y++) {
            glReadPixels(0, back->th - 1 - y, shot->w, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                         (uint8_t *)shot->pixels + (size_t)y * (size_t)shot->pitch);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    if (shot && SDL_SavePNG(shot, path)) {
        printf("screenshot: %s\n", path);
    } else {
        fprintf(stderr, "screenshot: %s\n", SDL_GetError());
    }
    SDL_DestroySurface(shot);
    SDL_free(path);
}

bool nw_backend_init(void)
{
    nw_debug_toggle("Render", "Wireframe", &nw_be.wireframe);
    nw_debug_action("Render", "Screenshot", SDL_SCANCODE_F12, nw_be_screenshot, NULL);
    return nw_vu_init();
}

static void nw_be_surf_free(NwBeSurf *s)
{
    glDeleteFramebuffers(1, &s->fbo);
    glDeleteTextures(1, &s->tex);
    glDeleteRenderbuffers(1, &s->depth);
    memset(s, 0, sizeof(*s));
}

void nw_backend_shutdown(void)
{
    for (uint32_t i = 0; i < nw_be.nsurf; i++) {
        nw_be_surf_free(&nw_be.surf[i]);
    }
    nw_be_surf_free(&nw_be.copy);
    glDeleteVertexArrays(1, &nw_be.rect_vao);
    glDeleteBuffers(1, &nw_be.rect_vbo);
    glDeleteBuffers(1, &nw_be.rect_ibo);
    glDeleteBuffers(1, &nw_be.ot_ibo);
    glDeleteBuffers(1, &nw_be.pool);
    glDeleteTextures(1, &nw_be.pool_tex);
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
        nw_vif_skin_free(&sk->skin);
        SDL_free(sk);
        break;
    case NW_GPU_FREE_HOST:
        SDL_free(job->obj);
        break;
    }
}

/* (Re)makes a surface tw x th, black: colour and a 24-bit Z, as the GS has; leaves its framebuffer bound. */
static void nw_be_surf_make(NwBeSurf *s, int32_t addr, int tw, int th)
{
    if (!s->fbo) {
        glGenFramebuffers(1, &s->fbo);
        glGenTextures(1, &s->tex);
        glGenRenderbuffers(1, &s->depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    if (s->tw != tw || s->th != th) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glActiveTexture(GL_TEXTURE0);
        glBindRenderbuffer(GL_RENDERBUFFER, s->depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, tw, th);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s->depth);
        s->tw = tw;
        s->th = th;
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

/* The draws go to s, width x height pixels at its top left. */
static void nw_be_bind(NwBeSurf *s, int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glViewport(0, s->th - height, width, height);
    nw_be.target = s;
    nw_be.width = width;
    nw_be.height = height;
    if (s->addr == NW_SURF_BACK) {
        nw_be.shown_w = width;
        nw_be.shown_h = height;
    }
}

static void nw_be_target(const NwCmd *c)
{
    NwSurf surf = c->target.surf;
    if (!surf.addr) {
        surf.tw = (uint16_t)c->target.width;
        surf.th = (uint16_t)c->target.height;
    }
    NwBeSurf *s = nw_be_surf(surf.addr, surf.tw, surf.th);
    if (s) {
        nw_be_bind(s, c->target.width, c->target.height);
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
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbo);
        glBlitFramebuffer(0, 0, s->tw, s->th, 0, 0, s->tw, s->th, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        s = &nw_be.copy;
    }
    tex = (NwVuTexture){ s->tex, 0, tcc, true };
    return &tex;
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
        m.uvoff[0] = s->uvoff[0];
        m.uvoff[1] = s->uvoff[1];
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
    glEnable(GL_SCISSOR_TEST);
    glScissor(sc[0], nw_be.target->th - 1 - sc[3], sc[2] - sc[0] + 1, sc[3] - sc[1] + 1);
    bool rgb = c->clear.mask & 1, z = c->clear.mask & 2;
    glColorMask(rgb, rgb, rgb, rgb);
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
    float screen[16];
    nw_be_screen(&p->views[c->view], nw_be.width, nw_be.height, screen);
    NwVuObject o;
    nw_vu1init(&o, c->local, screen);
    if (c->light != NW_LIGHT_NONE) {
        nw_vu1init_lights(&o, c->local, &p->lights[c->light]);
    }
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

/* A skin geom under its draw's palette, lit in the local matrix's space as vu1init lights it. */
static void nw_be_skin(const NwPacket *p, const NwCmd *c)
{
    const NwGpuSkin *s = c->skin;
    const NwVuProg *mpg = c->mpg;
    NwVuProgram program = nw_vu_skin_program(mpg ? mpg->name : NULL);
    if (!s || !s->vao || c->first + c->count > p->njoints) {
        return;
    }
    if (program == NW_VU_PROGRAMS) {
        nw_be_undrawn(mpg, true);
        return;
    }
    float screen[16];
    nw_be_screen(&p->views[c->view], nw_be.width, nw_be.height, screen);
    NwVuObject o;
    nw_vu1init(&o, c->local, screen);
    if (c->light != NW_LIGHT_NONE) {
        nw_vu1init_lights(&o, c->local, &p->lights[c->light]);
    }
    NwVuMaterial m = nw_be_material(p, c, false);
    NwMtlPass pass[2];
    glBindVertexArray(s->vao);
    uint32_t n = nw_vu_use(program, &o, &m, pass);
    nw_vu_joints(program, c->first);
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, &m, &pass[k]);
        glDrawElements(GL_TRIANGLES, (GLsizei)(s->skin.ntris * 3), GL_UNSIGNED_INT, NULL);
    }
}

/* The packet's palettes as RGBA32F texels, four rows a matrix. */
static void nw_be_pool(const NwPacket *p)
{
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
    nw_be_bind(back, nw_be.shown_w, nw_be.shown_h);
    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, nw_be.wireframe ? GL_LINE : GL_FILL);
    nw_be_rects(p);
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
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    NwBeSurf *front = nw_be_surf(NW_SURF_FRONT, back->tw, back->th);
    if (front) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
        glBlitFramebuffer(0, 0, back->tw, back->th, 0, 0, back->tw, back->th, GL_COLOR_BUFFER_BIT, GL_NEAREST);
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

const NwPacket *nw_backend_frame(int width, int height)
{
    const NwPacket *p = nw_packet_take();
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
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); /* the last pass's masks, which glClear honours */
    glDepthMask(GL_TRUE);
    NwBeSurf *back = nw_be_surf(NW_SURF_BACK, 1 << nw_be_log2(bw), 1 << nw_be_log2(bh));
    if (back && p && p->seq != nw_be.drawn) {
        nw_be_run(p, back);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
    }

    /* The PS2 shows its frame at 4:3 whatever its pixel size. */
    int w = width, h = height;
    if (w * 3 > h * 4) {
        w = h * 4 / 3;
    } else {
        h = w * 3 / 4;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (back) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
        glBlitFramebuffer(0, back->th - bh, bw, back->th, (width - w) / 2, (height - h) / 2, (width + w) / 2,
                          (height + h) / 2, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return p;
}
