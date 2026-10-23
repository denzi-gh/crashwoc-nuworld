#include "gfx/backend.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "data/image_tables.h"
#include "gfx/gl.h"
#include "gfx/vu.h"

static struct {
    GLuint fbo, colour, depth;
    int width, height; /* of the GS framebuffer */
    uint64_t drawn;    /* the packet in it */
    NwGpuJob *jobs;
    uint32_t jobcap;
    GLuint rect_vao, rect_vbo, rect_ibo; /* the packet's sprites, four vertices each */
    NwVifVertex *rect_verts;
    uint32_t rect_cap;
} nw_be;

/* A command with no material: GS defaults, untextured. */
static const NwVuMaterial nw_be_default = { { 0 }, { 0 }, NULL, { .ztst = 2 } };

bool nw_backend_init(void)
{
    return nw_vu_init();
}

void nw_backend_shutdown(void)
{
    glDeleteFramebuffers(1, &nw_be.fbo);
    glDeleteRenderbuffers(1, &nw_be.colour);
    glDeleteRenderbuffers(1, &nw_be.depth);
    glDeleteVertexArrays(1, &nw_be.rect_vao);
    glDeleteBuffers(1, &nw_be.rect_vbo);
    glDeleteBuffers(1, &nw_be.rect_ibo);
    nw_vu_shutdown();
    SDL_free(nw_be.jobs);
    SDL_free(nw_be.rect_verts);
    memset(&nw_be, 0, sizeof(nw_be));
}

static void nw_be_job(const NwGpuJob *job)
{
    NwGpuMesh *m = job->obj;
    NwGpuTex *t = job->obj;
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
    }
}

/* Binds the GS framebuffer at the target's size, black when new: colour and a 24-bit Z, as the GS has. */
static bool nw_be_target(int width, int height)
{
    if (!nw_be.fbo) {
        glGenFramebuffers(1, &nw_be.fbo);
        glGenRenderbuffers(1, &nw_be.colour);
        glGenRenderbuffers(1, &nw_be.depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, nw_be.fbo);
    if (nw_be.width != width || nw_be.height != height) {
        glBindRenderbuffer(GL_RENDERBUFFER, nw_be.colour);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, nw_be.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, nw_be.colour);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, nw_be.depth);
        nw_be.width = width;
        nw_be.height = height;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
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
        m.tex = s->tex ? &s->tex->gl : NULL;
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
    glScissor(sc[0], nw_be.height - 1 - sc[3], sc[2] - sc[0] + 1, sc[3] - sc[1] + 1);
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

static void nw_be_draw(const NwPacket *p, const NwCmd *c)
{
    const NwGpuMesh *mesh = c->mesh;
    if (c->op != NW_CMD_DRAW || !mesh || !mesh->vao) {
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
    const NwVuProg *mpg = c->mpg;
    glBindVertexArray(mesh->vao);
    nw_be_passes(nw_vu_program(mpg ? mpg->name : NULL), &o, &m, (GLsizei)(mesh->mesh.ntris * 3), NULL);
}

/* A packet runs once into the GS framebuffer, which keeps what it drew as GS memory does. */
static void nw_be_run(const NwPacket *p)
{
    glViewport(0, 0, nw_be.width, nw_be.height);
    glEnable(GL_DEPTH_TEST);
    nw_be_rects(p);
    uint32_t rects = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        switch (c->op) {
        case NW_CMD_DRAW:
            nw_be_draw(p, c);
            break;
        case NW_CMD_CLEAR:
            nw_be_clear(p, c);
            break;
        case NW_CMD_RECT:
            nw_be_rect(p, c, rects++);
            break;
        }
    }
    nw_be.drawn = p->seq;
}

const NwPacket *nw_backend_frame(int width, int height)
{
    const NwPacket *p = nw_packet_take();
    uint32_t njobs = nw_gpu_take(p ? p->seq : 0, &nw_be.jobs, &nw_be.jobcap);
    for (uint32_t i = 0; i < njobs; i++) {
        nw_be_job(&nw_be.jobs[i]);
    }
    int gw = nw_be.width ? nw_be.width : 640, gh = nw_be.height ? nw_be.height : 512;
    if (p && p->nviews) {
        gw = p->views[0].scissor[2] + 1;
        gh = p->views[0].scissor[3] + 1;
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); /* the last pass's masks, which glClear honours */
    glDepthMask(GL_TRUE);
    if (nw_be_target(gw, gh) && p && p->seq != nw_be.drawn) {
        nw_be_run(p);
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
    glBindFramebuffer(GL_READ_FRAMEBUFFER, nw_be.fbo);
    glBlitFramebuffer(0, 0, nw_be.width, nw_be.height, (width - w) / 2, (height - h) / 2, (width + w) / 2,
                      (height + h) / 2, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return p;
}
