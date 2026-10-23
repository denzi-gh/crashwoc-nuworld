#include "gfx/backend.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "data/image_tables.h"
#include "gfx/gl.h"
#include "gfx/vu.h"

static struct {
    GLuint fbo, colour, depth;
    int width, height; /* of the GS framebuffer */
    NwGpuJob *jobs;
    uint32_t jobcap;
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
    nw_vu_shutdown();
    SDL_free(nw_be.jobs);
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

/* Binds the GS framebuffer at the target's size: colour and a 24-bit Z, as the GS has. */
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
    }
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

/* The view's vs times the matrix from GS coordinates (12.4 pixels from XYOFFSET, Z 0 .. 2^23 - 1 with the nearer
   point larger) to GL clip space for a width x height target, where the nearer point is the smaller. */
static void nw_be_screen(const NwView *v, int width, int height, float out[16])
{
    float g[16] = { 0 };
    g[0] = 1.0f / (8.0f * (float)width);
    g[5] = -1.0f / (8.0f * (float)height);
    g[10] = -2.0f / 8388607.0f;
    g[12] = -(float)v->xyoffset[0] / (8.0f * (float)width) - 1.0f;
    g[13] = (float)v->xyoffset[1] / (8.0f * (float)height) + 1.0f;
    g[14] = 1.0f;
    g[15] = 1.0f;
    NwVuObject o;
    nw_vu1init(&o, v->vs, g);
    memcpy(out, o.lv, sizeof(o.lv));
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
    NwVuMaterial m = nw_be_default;
    if (c->mtl != NW_MTL_NONE) {
        const NwMtlSnap *s = &p->mtls[c->mtl];
        memcpy(m.diffuse, s->diffuse, sizeof(m.diffuse));
        m.tex = s->tex ? &s->tex->gl : NULL;
        m.state = s->gs;
    }
    const NwVuProg *mpg = c->mpg;
    NwVuProgram program = nw_vu_program(mpg ? mpg->name : NULL);
    NwMtlPass pass[2];
    uint32_t n = nw_vu_use(program, &o, &m, pass);
    glBindVertexArray(mesh->vao);
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, &m, &pass[k]);
        glDrawElements(GL_TRIANGLES, (GLsizei)(mesh->mesh.ntris * 3), GL_UNSIGNED_INT, NULL);
    }
}

const NwPacket *nw_backend_frame(int width, int height)
{
    const NwPacket *p = nw_packet_take();
    uint32_t njobs = nw_gpu_take(p ? p->seq : 0, &nw_be.jobs, &nw_be.jobcap);
    for (uint32_t i = 0; i < njobs; i++) {
        nw_be_job(&nw_be.jobs[i]);
    }
    int gw = 640, gh = 512;
    if (p && p->nviews) {
        gw = p->views[0].scissor[2] + 1;
        gh = p->views[0].scissor[3] + 1;
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); /* the last pass's masks, which glClear honours */
    glDepthMask(GL_TRUE);
    if (nw_be_target(gw, gh)) {
        glViewport(0, 0, gw, gh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        for (uint32_t i = 0; p && i < p->ncmds; i++) {
            nw_be_draw(p, &p->cmds[i]);
        }
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
