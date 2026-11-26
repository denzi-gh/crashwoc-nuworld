#include "gfx/backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "data/image_tables.h"
#include "frontend/debug.h"
#include "gfx/cloudvol.h"
#include "gfx/gem.h"
#include "gfx/gl.h"
#include "gfx/interp.h"
#include "gfx/vu.h"
#include "platform/crash.h"
#include "platform/log.h"
#include "platform/paths.h"

#define NW_BE_SURFS 16
#define NW_BE_SILHOUETTES 64
#define NW_BE_SILHOUETTE_SIZES 4

/* A GS surface as a power-of-two texture at scale n, GS row y at rows from n * (th - 1 - y). */
typedef struct NwBeSurf {
    int32_t addr; /* NW_SURF_BACK, NW_SURF_FRONT, a GS address, 0 for the targets the port does not draw */
    int tw, th;   /* GS pixels */
    int n;
    GLuint tex, depth, fbo;
    GLuint ms_fbo, ms_col, ms_depth; /* MSAA: where the draws go, resolved into tex and depth before a read */
    int ms_w, ms_h, samples;
    bool ms;
    bool dirty;                      /* ms_fbo holds draws tex and depth lack */
} NwBeSurf;

static struct {
    NwBeSurf surf[NW_BE_SURFS];
    uint32_t nsurf;
    NwBeSurf copy;
    NwBeSurf zcopy;
    GLuint zclut;
    NwBeSurf *target;
    bool alpha;            /* the target keeps alpha (not PSMCT24) */
    int width, height;
    int shown_w, shown_h;  /* the back buffer's size */
    uint64_t drawn;
    float drawn_t;
    float seconds;         /* the clock's edges at the frame drawn, as seconds at 60 a second, wrapped at 10000 */
    bool storm_on;         /* the intro's storm runs in the frame drawn */
    NwCloudSky sky;        /* how far, blended as the frame is, and its sun */
    NwGpuJob *jobs;
    uint32_t jobcap;
    GLuint rect_vao, rect_vbo, rect_ibo;
    GLuint ot_ibo;
    GLuint pool, pool_tex;               /* joint palettes, a texture buffer on unit 3 */
    GLuint weights, weights_tex;         /* blend-shape weights, on unit 4 */
    GLuint verts_vao, verts_vbo;
    GLuint ptl_vao, ptl_vbo;
    NwVifVertex *rect_verts;
    uint32_t rect_cap;
    bool wireframe;
    NwRenderOptions opts;
    uint64_t runs;
    const NwBeSurf *copy_of;
    uint64_t copy_run;
    uint32_t held[512];      /* the world's [video] gems draws, in NwPacket.cmds, till the scene behind them is drawn */
    uint32_t nheld;
    bool flushing;
    int scale;
    int samples;
    GLint max_tex;      /* GL_MAX_TEXTURE_SIZE */
    GLint max_samples;  /* GL_MAX_SAMPLES */
    GLuint present_prog, present_vao;
    GLint present_rect, present_level;
    bool checked; /* glGetError after the first packet run */
    GLuint sil_tex[NW_BE_SILHOUETTES][NW_BE_SILHOUETTE_SIZES], sil_fbo[NW_BE_SILHOUETTES][NW_BE_SILHOUETTE_SIZES];
    uint32_t sil_id[NW_BE_SILHOUETTES];
    int sil_level[NW_BE_SILHOUETTES];
    uint32_t nsil;
} nw_be;

static const NwVuMaterial nw_be_default = { .state = { .ztst = 2 } };

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
        nw_log_info(NW_LOG_GFX, "screenshot: %s", path);
    } else {
        nw_log_warn(NW_LOG_GFX, "screenshot: %s", SDL_GetError());
    }
    SDL_free(path);
}


static const char nw_be_present_vs[] =
    "#version 330 core\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_pos;\n"
    "void main()\n"
    "{\n"
    "    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    v_pos = mix(u_rect.xy, u_rect.zw, c);\n"
    "    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char nw_be_present_fs[] =
    "#version 330 core\n"
    "uniform sampler2D u_surf;\n"
    "uniform vec4 u_rect;\n"
    "uniform int u_level;\n"
    "in vec2 v_pos;\n"
    "out vec4 o_col;\n"
    "vec3 texel(ivec2 p)\n"
    "{\n"
    "    return texelFetch(u_surf, clamp(p, ivec2(u_rect.xy), ivec2(u_rect.zw) - 1), u_level).rgb;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec2 p = v_pos - 0.5;\n"
    "    ivec2 i = ivec2(floor(p));\n"
    "    vec2 f = p - vec2(i);\n"
    "    o_col = vec4(mix(mix(texel(i), texel(i + ivec2(1, 0)), f.x),\n"
    "                     mix(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x), f.y), 1.0);\n"
    "}\n";

static bool nw_be_present_init(void)
{
    static const char *const src[2] = { nw_be_present_vs, nw_be_present_fs };
    static const GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    char log[1024] = "";
    GLint ok = 1;
    GLuint p = nw_be.present_prog = glCreateProgram();
    for (int i = 0; i < 2 && ok; i++) {
        GLuint s = glCreateShader(type[i]);
        glShaderSource(s, 1, &src[i], NULL);
        glCompileShader(s);
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(s, sizeof(log), NULL, log);
        }
        glAttachShader(p, s);
        glDeleteShader(s);
    }
    if (ok) {
        glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            glGetProgramInfoLog(p, sizeof(log), NULL, log);
        }
    }
    if (!ok) {
        return SDL_SetError("present: %s", log);
    }
    nw_be.present_rect = glGetUniformLocation(p, "u_rect");
    nw_be.present_level = glGetUniformLocation(p, "u_level");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_surf"), 2);
    glGenVertexArrays(1, &nw_be.present_vao);
    return true;
}

bool nw_backend_init(void)
{
    nw_be.opts.scale = 1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &nw_be.max_tex);
    glGetIntegerv(GL_MAX_SAMPLES, &nw_be.max_samples);
    return nw_vu_init() && nw_be_present_init();
}

void nw_backend_debug_menu(void)
{
    nw_debug_toggle("Debug", "Wireframe", &nw_be.wireframe);
    nw_debug_action("Debug", "Screenshot", SDL_SCANCODE_F12, nw_be_screenshot, NULL);
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

static GLuint nw_be_fbo(const NwBeSurf *s)
{
    return s->ms ? s->ms_fbo : s->fbo;
}

/* Same-size blit, as a resolving blit needs; rebinds the target's framebuffer after. */
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

/* s's multisampled buffers, black, under its own Z or shared's (the back buffer's); leaves them bound. */
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
    glDeleteProgram(nw_be.present_prog);
    glDeleteVertexArrays(1, &nw_be.present_vao);
    glDeleteFramebuffers(NW_BE_SILHOUETTES * NW_BE_SILHOUETTE_SIZES, nw_be.sil_fbo[0]);
    glDeleteTextures(NW_BE_SILHOUETTES * NW_BE_SILHOUETTE_SIZES, nw_be.sil_tex[0]);
    nw_cloudvol_shutdown();
    nw_vu_shutdown();
    SDL_free(nw_be.jobs);
    SDL_free(nw_be.rect_verts);
    memset(&nw_be, 0, sizeof(nw_be));
}

/* Each sprite as a quad of two triangles, s and t from (0, 0) to (1, 1), its size in tex.zw. */
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

static const char *const nw_be_job_names[] = { "upload mesh",  "upload texture", "upload sprites",
                                               "upload skin",  "free mesh",      "free texture",
                                               "free sprites", "free skin",      "free host memory" };
_Static_assert(SDL_arraysize(nw_be_job_names) == NW_GPU_FREE_HOST + 1, "a name per NwGpuOp");
static const char *const nw_be_cmd_names[] = { "draw",  "draw ordering table", "clear",     "rect",
                                               "sprites", "target",            "skin",      "verts",
                                               "particles", "volumes",         "waves" };
_Static_assert(SDL_arraysize(nw_be_cmd_names) == NW_CMD_WAVES + 1, "a name per NwCmdOp");

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

/* 24-bit Z like the GS's, readable by a depth CLUT; a rescale keeps the picture; leaves the FBO bound. */
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

/* Made at tw x th if it is new or its size changed; NULL with none left. */
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

/* Under the back buffer's Z at its size a target shares it and its MSAA (not the front buffer); else its own. */
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
        bool ms = s == back ? nw_be.samples > 1 : shared && back->ms && s->addr != NW_SURF_FRONT;
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
            nw_be_ms(s, shared ? back : NULL);
        }
        s->ms = ms;
        nw_be_bind(s, c->target.width, c->target.height);
        nw_be.alpha = surf.tcc || !surf.addr;
        nw_vu_alpha_writes(nw_be.alpha);
    }
}

/* The target itself is read from a copy, as a texture cannot be both. */
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
        nw_be.copy_of = s;
        nw_be.copy_run = nw_be.runs;
        s = &nw_be.copy;
    } else {
        nw_be_resolve(s);
    }
    tex = (NwVuTexture){ .index = s->tex, .tcc = tcc, .surface = true };
    return &tex;
}

static const NwVuTexture *nw_be_dst(void)
{
    static NwVuTexture tex;
    NwBeSurf *s = nw_be.target;
    nw_be_surf_make(&nw_be.copy, 0, s->tw, s->th);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, nw_be_fbo(s));
    glBlitFramebuffer(0, 0, s->tw * s->n, s->th * s->n, 0, 0, s->tw * s->n, s->th * s->n, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, nw_be_fbo(s));
    nw_be.copy_of = s;
    nw_be.copy_run = nw_be.runs;
    tex = (NwVuTexture){ .index = nw_be.copy.tex, .tcc = true, .surface = true };
    return &tex;
}

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
                      { z->spot ? 2u : z->clear ? 1u : 0u, z->clear_z[0], z->clear_z[1] }, z->alpha, z->ad };
    return &vz;
}

/* GS coordinates (12.4 from XYOFFSET, Z 0 .. 2^23 - 1, larger nearer) to GL clip space (smaller nearer). */
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

static void nw_be_screen(const NwView *v, int width, int height, float out[16])
{
    float g[16];
    nw_be_gs(v, width, height, g);
    NwVuObject o;
    nw_vu1init(&o, v->vs, g);
    memcpy(out, o.lv, sizeof(o.lv));
}

static NwVuMaterial nw_be_material(const NwPacket *p, const NwCmd *c, bool uvrow)
{
    NwVuMaterial m = nw_be_default;
    if (c->mtl != NW_MTL_NONE) {
        const NwMtlSnap *s = &p->mtls[c->mtl];
        memcpy(m.diffuse, s->diffuse, sizeof(m.diffuse));
        memcpy(m.fx, s->fx, sizeof(m.fx));
        memcpy(m.vu, s->vu, sizeof(m.vu));
        m.uvoff[0] = s->vu[0];
        m.uvoff[1] = s->vu[1];
        m.uvoff[2] = uvrow ? (float)s->row[0] : 0.0f;
        m.uvoff[3] = uvrow ? (float)s->row[1] : 0.0f;
        m.tex = s->surf.addr ? nw_be_surf_tex(&s->surf, s->surf.tcc) : s->tex ? &s->tex->gl : NULL;
        m.layer = s->layer;
        m.uv = s->uv;
        m.state = s->gs;
    }
    return m;
}

/* A part's glow (rndr.h NW_MTL_FX_HALO): after the part, its shell added over the frame, under what is nearer, the
   frame's alpha kept. Begin sets it up for the part's draw (false: it has none); end puts the state back. */
#define NW_BE_HALO_SHELLS 6

#define NW_BE_HALO_PUSH 0.04f /* how far back (the part's units) the shells go behind it, along the eye's rays */

/* Shell k of NW_BE_HALO_SHELLS, from the nearest: wider each, the colour shared out, so the glow fades outwards. */
static bool nw_be_halo_begin(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, int k)
{
    /* none for the gradient letters (gloss with a negative mode), whose [12-15] are their gradient's */
    if (m->fx[0] == 0.0f || (m->fx[0] == 3.0f && m->fx[11] < 0.0f) || m->fx[15] <= 0.0f ||
        (m->fx[12] <= 0.0f && m->fx[13] <= 0.0f && m->fx[14] <= 0.0f)) {
        return false;
    }
    NwVuMaterial h = *m;
    h.fx[0] = 4.0f; /* NW_MTL_FX_HALO */
    h.fx[15] = m->fx[15] * (float)(k + 1) / NW_BE_HALO_SHELLS;
    h.fx[1] = NW_BE_HALO_PUSH;


    for (int i = 12; i < 15; i++) {
        h.fx[i] = m->fx[i] * 2.0f / NW_BE_HALO_SHELLS;
    }
    h.state.abe = 0;
    h.dst = NULL;
    NwMtlPass pass[2];
    nw_vu_use(program, o, &h, pass);
    nw_vu_pass(program, &h, &pass[0]);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glDepthMask(GL_FALSE);
    return true;
}

static void nw_be_halo_end(void)
{
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

static void nw_be_passes(NwVuProgram program, const NwVuObject *o, const NwVuMaterial *m, GLsizei count,
                         const void *first, bool black)
{
    NwMtlPass pass[2];
    uint32_t n = nw_vu_use(program, o, m, pass);
    if (black) {
        nw_vu_black(program, m->state.abe); /* a silhouette against the light (NwCmd.heroes_black) */
    }
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, m, &pass[k]);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, first);
    }
    for (int k = 0; k < NW_BE_HALO_SHELLS && nw_be_halo_begin(program, o, m, k); k++) {
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, first);
        nw_be_halo_end();
    }
}

static void nw_sky_draw(int x, int y, int w, int h, int gs_w);
static void nw_clouds_draw(const NwPacket *p, const NwCmd *c);

/* As the GS clears: sprites over the scissor with ZTST ALWAYS, the alpha test masking colour or Z; 0x80 alpha = 1.0. */
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
    if (rgb && nw_be.target && nw_be.target->addr == NW_SURF_BACK) {
        nw_sky_draw(sc[0] * n, (nw_be.target->th - 1 - sc[3]) * n, (sc[2] - sc[0] + 1) * n, (sc[3] - sc[1] + 1) * n,
                    sc[2] - sc[0] + 1);
    }
}

/* GS coordinates for the unlit program, s and t x4096. */
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
    nw_be_passes(NW_VU_TRICU, &o, &m, 6, (const void *)(uintptr_t)(index * 6 * sizeof(uint32_t)), false);
}

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

static void nw_be_undrawn(const NwVuProg *mpg, bool skin)
{
    static const NwVuProg *seen[2][16];
    for (int i = 0; i < 16 && seen[skin][i] != mpg; i++) {
        if (!seen[skin][i]) {
            seen[skin][i] = mpg;
            nw_log_warn(NW_LOG_GFX, "%s: its %s program is not drawn", mpg ? mpg->name : "no package",
                        skin ? "skin" : "triangle");
            break;
        }
    }
}

/* vu1init's state; frag maps GS 12.4 (x, y) from XYOFFSET to GL (x, th - 1 - y), pixel centres, n x n pixels each. */
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

static bool nw_be_silhouette_proj(const NwCmd *q, float proj[16])
{
    const float *l = q->local;
    float x[3] = { l[0], l[1], l[2] }, z[3] = { l[8], l[9], l[10] };
    float y[3] = { x[1] * z[2] - x[2] * z[1], x[2] * z[0] - x[0] * z[2], x[0] * z[1] - x[1] * z[0] };
    float det = x[0] * (y[1] * z[2] - y[2] * z[1]) - x[1] * (y[0] * z[2] - y[2] * z[0]) +
                x[2] * (y[0] * z[1] - y[1] * z[0]);
    if (det == 0.0f || q->silhouette_scale <= 0.0f) {
        return false;
    }
    float f = q->silhouette_scale;
    float r[3][3] = { { x[0], x[1], x[2] }, { y[0], y[1], y[2] }, { z[0], z[1], z[2] } };
    float inv[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            inv[j][i] = (r[i1][j1] * r[i2][j2] - r[i1][j2] * r[i2][j1]) / det;
        }
    }
    memset(proj, 0, 16 * sizeof(float));
    for (int i = 0; i < 3; i++) {
        proj[i * 4 + 0] = f * inv[i][0];
        proj[i * 4 + 1] = f * inv[i][2];
    }
    proj[12] = -f * (l[12] * inv[0][0] + l[13] * inv[1][0] + l[14] * inv[2][0]);
    proj[13] = -f * (l[12] * inv[0][2] + l[13] * inv[1][2] + l[14] * inv[2][2]);
    proj[15] = 1.0f;
    return true;
}

static int nw_be_silhouette_level(const NwPacket *p, const NwCmd *q)
{
    static const float corner[3][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f } };
    NwVuObject o;
    nw_be_object(p, q, NW_VU_TRICU, &o);
    float px[3][2];
    for (int k = 0; k < 3; k++) {
        float clip[4];
        for (int c = 0; c < 4; c++) {
            clip[c] = corner[k][0] * o.lv[c] + corner[k][1] * o.lv[8 + c] + o.lv[12 + c];
        }
        if (clip[3] <= 1e-6f) {
            return NW_BE_SILHOUETTE_SIZES - 1;
        }
        px[k][0] = clip[0] / clip[3] * 0.5f * (float)(nw_be.width * nw_be.target->n);
        px[k][1] = clip[1] / clip[3] * 0.5f * (float)(nw_be.height * nw_be.target->n);
    }
    float extent = SDL_max(SDL_sqrtf((px[1][0] - px[0][0]) * (px[1][0] - px[0][0]) +
                                     (px[1][1] - px[0][1]) * (px[1][1] - px[0][1])),
                           SDL_sqrtf((px[2][0] - px[0][0]) * (px[2][0] - px[0][0]) +
                                     (px[2][1] - px[0][1]) * (px[2][1] - px[0][1])));
    int level = 0;
    while (level < NW_BE_SILHOUETTE_SIZES - 1 && (float)(64 << level) < extent) {
        level++;
    }
    return level;
}

static void nw_be_silhouette_part(const NwPacket *p, const NwCmd *c, const float proj[16])
{
    float lv[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            lv[i * 4 + j] = c->local[i * 4] * proj[j] + c->local[i * 4 + 1] * proj[4 + j] +
                            c->local[i * 4 + 2] * proj[8 + j] + c->local[i * 4 + 3] * proj[12 + j];
        }
    }
    if (c->op == NW_CMD_SKIN) {
        const NwGpuSkin *s = c->skin;
        if (!s || !s->vao || c->first + c->count > p->njoints) {
            return;
        }
        nw_vu_silhouette(true, lv, c->first);
        glBindVertexArray(s->vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)(s->skin.ntris * 3), GL_UNSIGNED_INT, NULL);
        return;
    }
    const NwGpuMesh *mesh = c->mesh;
    if (!mesh || !mesh->vao) {
        return;
    }
    nw_vu_silhouette(false, lv, 0);
    glBindVertexArray(mesh->vao);
    if (c->op == NW_CMD_DRAW_OT) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.ot_ibo);
        glDrawElements(GL_TRIANGLES, (GLsizei)c->count, GL_UNSIGNED_INT,
                       (const void *)(uintptr_t)(c->first * sizeof(uint32_t)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    } else {
        glDrawElements(GL_TRIANGLES, (GLsizei)(mesh->mesh.ntris * 3), GL_UNSIGNED_INT, NULL);
    }
}

static void nw_be_silhouettes(const NwPacket *p)
{
    nw_be.nsil = 0;
    for (uint32_t i = 0; i < p->ncmds && nw_be.nsil < NW_BE_SILHOUETTES; i++) {
        const NwCmd *q = &p->cmds[i];
        float proj[16];
        if (q->op != NW_CMD_VERTS || !q->silhouette || !nw_be_silhouette_proj(q, proj)) {
            continue;
        }
        if (!nw_be.nsil) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        uint32_t slot = nw_be.nsil++;
        int level = nw_be_silhouette_level(p, q), size = 64 << level;
        GLuint *tex = &nw_be.sil_tex[slot][level], *fbo = &nw_be.sil_fbo[slot][level];
        if (!*tex) {
            glGenTextures(1, tex);
            glBindTexture(GL_TEXTURE_2D, *tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
        }
        nw_be.sil_id[slot] = q->silhouette;
        nw_be.sil_level[slot] = level;
        glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
        glViewport(0, 0, size, size);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        for (uint32_t j = 0; j < p->ncmds; j++) {
            const NwCmd *c = &p->cmds[j];
            if (c->silhouette == q->silhouette &&
                (c->op == NW_CMD_DRAW || c->op == NW_CMD_DRAW_OT || c->op == NW_CMD_SKIN)) {
                nw_be_silhouette_part(p, c, proj);
            }
        }
        glBindTexture(GL_TEXTURE_2D, *tex);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

static void nw_be_tris(const NwPacket *p, const NwCmd *c)
{
    NwVuObject o;
    NwVuProgram program = NW_VU_TRICU;
    if (c->mode == NW_VERTS_GS) {
        memset(&o, 0, sizeof(o));
        nw_be_gs(&p->views[c->view], nw_be.width, nw_be.height, o.lv);
        o.lv[12] += 1.0f / (float)nw_be.width;
        o.lv[13] -= 1.0f / (float)nw_be.height;
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
    NwVuTexture silhouette;
    if (c->silhouette) {
        uint32_t s = 0;
        while (s < nw_be.nsil && nw_be.sil_id[s] != c->silhouette) {
            s++;
        }
        if (s == nw_be.nsil) {
            return;
        }
        silhouette = (NwVuTexture){
            .index = nw_be.sil_tex[s][nw_be.sil_level[s]], .tcc = true, .surface = true, .filtered = true
        };
        m.tex = &silhouette;
    }
    if (c->alpha) {
        m.state.alpha = (uint8_t)c->alpha;
    }
    if (c->zclut && c->zclut <= p->nzcluts) {
        m.zclut = nw_be_zclut(&p->zcluts[c->zclut - 1]);
    }
    if (c->exact) {
        /* a draw that reads the target has its copy already */
        m.dst = m.tex && nw_be.copy.tex && m.tex->index == nw_be.copy.tex ? m.tex : nw_be_dst();
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
    for (int k = 0; c->mode != NW_VERTS_GS && c->mode != NW_VERTS_SPRITE && k < NW_BE_HALO_SHELLS &&
                    nw_be_halo_begin(program, &o, &m, k);
         k++) {
        glDrawArrays(GL_TRIANGLES, (GLint)c->first, (GLsizei)c->count);
        nw_be_halo_end();
    }
}

/* Retail's NuLgtHSurf[1]: back buffer Z squeezed to the volumes' width, stencil 0x7F as retail's 0x7F sprites. */
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

/* Corners in GS units before the divide, texel coordinates in 1/16 of the texture's level 0. */
static void nw_be_particles(const NwPacket *p, const NwCmd *c)
{
    _Static_assert(sizeof(NwPtlList) == 132 * 16, "NwPtlList is VU 0x3C-0xBF");
    NwVuObject o;
    nw_be_object(p, c, NW_VU_PTL, &o);
    /* a group port/opening draws in a frame of its own: the sprites as large as its matrix scales its points; retail's
       groups at their own size whatever their matrix (the intro volcano's smoke has a scaled one) */
    float ls = c->ptl_own ? SDL_sqrtf(c->local[0] * c->local[0] + c->local[1] * c->local[1] + c->local[2] * c->local[2])
                          : 1.0f;
    ls = ls > 0.01f ? ls : 1.0f;
    o.sprite[0] = ls / (8.0f * (float)nw_be.width);
    o.sprite[1] = -ls / (8.0f * (float)nw_be.height);
    NwVuMaterial m = nw_be_material(p, c, false);
    const NwGpuTex *tex = c->mtl != NW_MTL_NONE ? p->mtls[c->mtl].tex : NULL;
    if (tex && !nw_vu_texture_rgba((NwVuTexture *)&tex->gl, &tex->tex)) {
        return;
    }
    if (tex && tex->tex.nlevels) {
        o.sprite[2] = 1.0f / (16.0f * (float)tex->tex.levels[0].width);
        o.sprite[3] = 1.0f / (16.0f * (float)tex->tex.levels[0].height);
    }
    NwMtlPass pass[2];
    glBindVertexArray(nw_be.ptl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, nw_be.ptl_vbo);
    nw_vu_ptl_attribs((size_t)c->first * sizeof(NwPtl));
    NwVuProgram program = nw_vu_particle_program(m.tex, &m.state);
    uint32_t n = nw_vu_use(program, &o, &m, pass);
    nw_vu_ptl(program, (const uint32_t(*)[4])&p->ptllists[c->ptllist]);
    for (uint32_t k = 0; k < n; k++) {
        nw_vu_pass(program, &m, &pass[k]);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)c->count);
    }
}

static bool nw_be_inverse(const float m[16], float out[16])
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        out[i] = inv[i] / det;
    }
    return true;
}

static void nw_be_unproject(const float inv[16], float x, float y, float z, float w, float out[4])
{
    for (int c = 0; c < 4; c++) {
        out[c] = x * inv[c] + y * inv[4 + c] + z * inv[8 + c] + w * inv[12 + c];
    }
}

static void nw_be_unit(float v[3])
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; l > 0.0f && i < 3; i++) {
        v[i] /= l;
    }
}

/* Menus and the HUD draw under the identity camera (DrawPanel's pNuCam). */
static bool nw_be_panel_view(const NwView *v)
{
    for (int i = 0; i < 16; i++) {
        if (SDL_fabsf(v->cam[i] - (i % 5 == 0 ? 1.0f : 0.0f)) > 1e-4f) {
            return false;
        }
    }
    return true;
}

/* The target as the first gem since the last flush found it. */
static GLuint nw_be_gem_backdrop(void)
{
    if (nw_be.copy_of != nw_be.target || nw_be.copy_run != nw_be.runs) {
        nw_be_dst();
    }
    return nw_be.copy.tex;
}

/* False when the view gives no eye: draw it as the PS2. */
static bool nw_be_gem(const NwPacket *p, const NwCmd *c, NwVuProgram program, const NwGemInfo *gem, GLsizei count,
                      const void *first)
{
    NwVuGem g;
    memset(&g, 0, sizeof(g));
    nw_be_screen(&p->views[c->view], nw_be.width, nw_be.height, g.vp);
    float inv[16], e[4], a[4], b[4];
    if (!nw_be_inverse(g.vp, inv)) {
        return false;
    }
    /* The eye maps to w 0 on the axis; right and up from the screen's centre at some depth. */
    nw_be_unproject(inv, 0.0f, 0.0f, 1.0f, 0.0f, e);
    nw_be_unproject(inv, 0.0f, 0.0f, 0.5f, 1.0f, a);
    if (SDL_fabsf(e[3]) < 1e-12f || SDL_fabsf(a[3]) < 1e-12f) {
        return false;
    }
    float centre[3];
    for (int i = 0; i < 3; i++) {
        g.eye[i] = e[i] / e[3];
        centre[i] = a[i] / a[3];
        g.cam[6 + i] = centre[i] - g.eye[i];
    }
    for (int axis = 0; axis < 2; axis++) {
        nw_be_unproject(inv, axis ? 0.0f : 0.5f, axis ? 0.5f : 0.0f, 0.5f, 1.0f, b);
        for (int i = 0; i < 3; i++) {
            g.cam[axis * 3 + i] = b[i] / b[3] - centre[i];
        }
    }
    for (int axis = 0; axis < 3; axis++) {
        nw_be_unit(g.cam + axis * 3);
    }
    memcpy(g.world, c->local, sizeof(g.world));
    memcpy(g.tint, gem->tint, sizeof(g.tint));
    g.kind = gem->kind;
    g.role = gem->role;
    g.panel = nw_be_panel_view(&p->views[c->view]);
    glGetIntegerv(GL_VIEWPORT, g.viewport);

    GLuint backdrop = nw_be_gem_backdrop();
    NwVuObject o;
    nw_be_object(p, c, program, &o);
    NwVuMaterial m = nw_be_material(p, c, false);
    m.state.abe = false;
    NwMtlPass pass[2];
    nw_vu_use(program, &o, &m, pass);
    nw_vu_gem(program, &g);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_2D, backdrop);
    glActiveTexture(GL_TEXTURE0);
    if (program == NW_VU_SKNGEM) {
        const NwGpuSkin *s = c->skin;
        nw_vu_skin(program, c->first, c->weights, s->delta_tex ? c->nweights : 0);
        if (s->delta_tex) {
            glActiveTexture(GL_TEXTURE0 + 5);
            glBindTexture(GL_TEXTURE_BUFFER, s->delta_tex);
            glActiveTexture(GL_TEXTURE0);
        }
    }
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, first);
    return true;
}

static NwGemRole nw_be_gem_of(const NwPacket *p, const NwCmd *c, NwGemInfo *gem)
{
    if (!nw_be.opts.gems || c->mtl == NW_MTL_NONE) {
        return NW_GEM_KEEP;
    }
    *gem = p->mtls[c->mtl].gem;
    return gem->kind == NW_GEM_NONE ? NW_GEM_KEEP : (NwGemRole)gem->role;
}

/* The PS2 sorts the world's gems ahead of what lies behind them, which their stone would then not see. */
static bool nw_be_gem_hold(const NwPacket *p, const NwCmd *c)
{
    if (nw_be.flushing || !nw_be.opts.gems || c->mtl == NW_MTL_NONE || p->mtls[c->mtl].gem.kind == NW_GEM_NONE ||
        nw_be.nheld == SDL_arraysize(nw_be.held) || nw_be_panel_view(&p->views[c->view])) {
        return false;
    }
    nw_be.held[nw_be.nheld++] = (uint32_t)(c - p->cmds);
    return true;
}

static void nw_be_draw(const NwPacket *p, const NwCmd *c);
static void nw_be_skin(const NwPacket *p, const NwCmd *c);

/* Held gems over one scene copy, before c: a target change, clear, shadows' Z or identity camera; NULL: run end. */
static void nw_be_gems_flush(const NwPacket *p, const NwCmd *c)
{
    if (!nw_be.nheld) {
        return;
    }
    if (c) {
        switch (c->op) {
        case NW_CMD_TARGET:
        case NW_CMD_CLEAR:
        case NW_CMD_RECT:
        case NW_CMD_VOLUMES:
            break;
        case NW_CMD_WAVES:
            return;
        default:
            if (!nw_be_panel_view(&p->views[c->view])) {
                return;
            }
        }
    }
    nw_be.copy_of = NULL;
    nw_be.flushing = true;
    for (uint32_t i = 0; i < nw_be.nheld; i++) {
        const NwCmd *h = &p->cmds[nw_be.held[i]];
        if (h->op == NW_CMD_SKIN) {
            nw_be_skin(p, h);
        } else {
            nw_be_draw(p, h);
        }
    }
    nw_be.flushing = false;
    nw_be.nheld = 0;
    nw_be.copy_of = NULL;
}

/* An OT batch draws its triangles in the packet's order through the packet's index buffer. */
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
    if (c->heroes_black && !nw_be.opts.silhouettes) {
        return; /* out of sight, as on the PS2 */
    }
    NwGemInfo gem;
    NwGemRole role = nw_be_gem_of(p, c, &gem);
    if (role == NW_GEM_HIDE || nw_be_gem_hold(p, c)) {
        return;
    }
    if (role != NW_GEM_KEEP) {
        bool ot = c->op == NW_CMD_DRAW_OT;
        glBindVertexArray(mesh->vao);
        if (ot) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.ot_ibo);
        }
        bool drawn = nw_be_gem(p, c, NW_VU_GEM, &gem, ot ? (GLsizei)c->count : (GLsizei)(mesh->mesh.ntris * 3),
                               ot ? (const void *)(uintptr_t)(c->first * sizeof(uint32_t)) : NULL);
        if (ot) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
        }
        if (drawn) {
            return;
        }
    }
    NwVuObject o;
    nw_be_object(p, c, program, &o);
    if (mesh->heroes_picture && nw_be.opts.silhouettes) {
        return; /* their models stand there */
    }
    const NwGpuCloud *cloud = c->op == NW_CMD_DRAW && nw_be.opts.clouds ? &mesh->cloud : NULL;
    if (cloud && cloud->kind == NW_CLOUD_PUFF &&
        nw_cloudvol_draw(cloud, c->local, o.lv, nw_be.seconds, nw_be.storm_on ? nw_be.sky.storm : 0.0f)) {
        return;
    }
    if (cloud && cloud->kind == NW_CLOUD_SUN && nw_cloudvol_ready()) {
        return; /* the sky draws the sun */
    }
    NwVuMaterial m = nw_be_material(p, c, mesh->mesh.uvrow);
    glBindVertexArray(mesh->vao);
    if (c->op == NW_CMD_DRAW_OT) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.ot_ibo);
        nw_be_passes(program, &o, &m, (GLsizei)c->count, (const void *)(uintptr_t)(c->first * sizeof(uint32_t)),
                     c->heroes_black);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    } else {
        nw_be_passes(program, &o, &m, (GLsizei)(mesh->mesh.ntris * 3), NULL, c->heroes_black);
    }
    if (cloud && cloud->kind == NW_CLOUD_SKY) {
        NwCloudSky sky = nw_be.sky;
        sky.storm = nw_be.storm_on ? sky.storm : cloud->weather;
        nw_cloudvol_sky(mesh, c->local, o.lv, nw_be.seconds, &sky);
    }
}

/* The sky's sun this frame: the sun card's draw, if the packet has one, which the sky draws in its place. */
static void nw_be_sky_sun(const NwPacket *p)
{
    nw_be.sky.sun_on = false;
    for (uint32_t i = 0; nw_be.opts.clouds && i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        const NwGpuCloud *card = c->op == NW_CMD_DRAW && c->mesh ? &c->mesh->cloud : NULL;
        if (!card || card->kind != NW_CLOUD_SUN) {
            continue;
        }
        float centre[3], half = 0.0f;
        for (int k = 0; k < 3; k++) {
            centre[k] = (card->min[k] + card->max[k]) * 0.5f;
        }
        for (int k = 0; k < 3; k++) {
            nw_be.sky.sun_at[k] =
                centre[0] * c->local[k] + centre[1] * c->local[4 + k] + centre[2] * c->local[8 + k] + c->local[12 + k];
        }
        for (int r = 0; r < 2; r++) {
            const float *row = &c->local[r * 4];
            float len = sqrtf(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
            half = SDL_max(half, (card->max[r] - card->min[r]) * 0.5f * len);
        }
        nw_be.sky.sun_radius = half;
        nw_be.sky.sun_on = true;
    }
}

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
    if (c->heroes_black && !nw_be.opts.silhouettes) {
        return; /* out of sight, as on the PS2 */
    }
    NwGemInfo gem;
    NwGemRole role = nw_be_gem_of(p, c, &gem);
    if (role == NW_GEM_HIDE || nw_be_gem_hold(p, c)) {
        return;
    }
    if (role != NW_GEM_KEEP) {
        glBindVertexArray(s->vao);
        if (nw_be_gem(p, c, NW_VU_SKNGEM, &gem, (GLsizei)(s->skin.ntris * 3), NULL)) {
            return;
        }
    }
    NwVuObject o;
    nw_be_object(p, c, program, &o);
    NwVuMaterial m = nw_be_material(p, c, false);
    NwMtlPass pass[2];
    glBindVertexArray(s->vao);
    uint32_t n = nw_vu_use(program, &o, &m, pass);
    if (c->heroes_black) {
        nw_vu_black(program, m.state.abe); /* against the light */
    }
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
    for (int k = 0; k < NW_BE_HALO_SHELLS && nw_be_halo_begin(program, &o, &m, k); k++) {
        nw_vu_skin(program, c->first, c->weights, s->delta_tex ? c->nweights : 0);
        glDrawElements(GL_TRIANGLES, (GLsizei)(s->skin.ntris * 3), GL_UNSIGNED_INT, NULL);
        nw_be_halo_end();
    }
}

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

/* RGBA32F texels, four rows a matrix. */
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

/* faceon: corner sizes in GS units scaled by VU 0x16; facey: along VF15, the camera's right in local space. */
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
    nw_be_passes(s->sprites.facey ? NW_VU_FACEY : NW_VU_FACEON, &o, &m, (GLsizei)(s->sprites.count * 6), NULL,
                 false);
}

/* Runs once into GS memory, which keeps what it drew; the front buffer is the back buffer as it ends. */
static void nw_be_run(const NwPacket *p, NwBeSurf *back)
{
    nw_be.runs++;
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
    nw_crash_phase("upload the frame's rects, verts, particles and palettes", 0, 0);
    nw_be_rects(p);
    nw_be_verts(p);
    nw_be_ptls(p);
    nw_be_pool(p);
    nw_be_sky_sun(p);
    if (p->nidx) {
        if (!nw_be.ot_ibo) {
            glGenBuffers(1, &nw_be.ot_ibo);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nw_be.ot_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)p->nidx * (GLsizeiptr)sizeof(uint32_t), p->idx,
                     GL_STREAM_DRAW);
    }
    nw_be_silhouettes(p);
    if (nw_be.nsil) {
        nw_be_bind(back, nw_be.shown_w, nw_be.shown_h);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, nw_be.wireframe ? GL_LINE : GL_FILL);
    }
    uint32_t rects = 0;
    nw_be.nheld = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        nw_crash_phase(c->op <= NW_CMD_WAVES ? nw_be_cmd_names[c->op] : "command", i, p->ncmds);
        nw_be_gems_flush(p, c);
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
        case NW_CMD_CLOUDS:
            nw_clouds_draw(p, c);
            break;
        case NW_CMD_WAVES:
            if (c->first < p->nwaves) {
                nw_vu_waves(p->waves[c->first].wave);
            }
            break;
        }
    }
    nw_be_gems_flush(p, NULL);
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

static int nw_be_samples(void)
{
    int n = 1;
    while (n * 2 <= nw_be.opts.msaa && n * 2 <= nw_be.max_samples) {
        n *= 2;
    }
    return n > 1 ? n : 0;
}

/* Auto (scale 0) is the least that covers h window rows with bh GS rows. */
static int nw_be_scale(int h, int bh, int tw, int th)
{
    int n = nw_be.opts.scale ? nw_be.opts.scale : (h + bh - 1) / bh;
    n = SDL_clamp(n, 1, NW_BACKEND_SCALE_MAX);
    while (n > 1 && (tw * n > nw_be.max_tex || th * n > nw_be.max_tex)) {
        n--;
    }
    return n;
}

/* Shrinking more than 2x reads a mip level at least the window's size, as glGenerateMipmap averages.*/
static void nw_be_present(NwBeSurf *back, int bw, int bh, int x, int y, int w, int h)
{
    int n = back->n, level = 0;
    nw_be_resolve(back);
    while (level < 12 && (bw * n) >> (level + 1) >= w && (bh * n) >> (level + 1) >= h) {
        level++;
    }
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, back->tex);
    if (level) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(x, y, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(nw_be.present_prog);
    glUniform4f(nw_be.present_rect, 0.0f, (float)(((back->th - bh) * n) >> level), (float)((bw * n) >> level),
                (float)((back->th * n) >> level));
    glUniform1i(nw_be.present_level, level);
    glBindVertexArray(nw_be.present_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* A cross-dissolve (port/opening.c): the first packet drawn of a new xfade_id keeps the frame shown before it (so a
   packet skipped on the way does not lose it), and every packet with xfade draws that frame over its own at that
   opacity. The kept frame is the back buffer's whole texture, drawn back
   1:1, so it lines up whatever part of it the frame uses. */
#define NW_XF_BLEND_SRC_RGB 0x80C9
#define NW_XF_BLEND_DST_RGB 0x80C8
#define NW_XF_BLEND_SRC_ALPHA 0x80CB
#define NW_XF_BLEND_DST_ALPHA 0x80CA
#define NW_XF_BLEND_EQUATION_RGB 0x8009
#define NW_XF_BLEND_EQUATION_ALPHA 0x883D
#define NW_XF_CURRENT_PROGRAM 0x8B8D
#define NW_XF_VERTEX_ARRAY_BINDING 0x85B5
#define NW_XF_TEXTURE_BINDING_2D 0x8069
#define NW_XF_ACTIVE_TEXTURE 0x84E0
#define NW_XF_VIEWPORT 0x0BA2
#define NW_XF_DEPTH_WRITEMASK 0x0B72

static struct {
    GLuint tex, fbo, prog, vao;
    GLint alpha;
    int w, h;
    uint32_t grabbed; /* the xfade_id it was kept for */
    bool failed;
} nw_xf;

static GLuint nw_xf_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        SDL_Log("cross-dissolve shader: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* A shader from the executable's shaders folder (assets/shaders), as gfx/vu.c reads its own; 0 if it is missing or
   does not compile (logged). */
static GLuint nw_be_file_shader(GLenum type, const char *name)
{
    const char *base = SDL_GetBasePath();
    char *path = NULL, *text = NULL;
    if (base && SDL_asprintf(&path, "%sshaders/%s", base, name) >= 0) text = SDL_LoadFile(path, NULL);
    if (!text) SDL_Log("shader %s: %s", path ? path : name, SDL_GetError());
    GLuint shader = text ? nw_xf_shader(type, text) : 0;
    SDL_free(text);
    SDL_free(path);
    return shader;
}

static bool nw_xf_program(void)
{
    if (nw_xf.prog || nw_xf.failed) {
        return nw_xf.prog != 0;
    }
    GLuint vs = nw_xf_shader(GL_VERTEX_SHADER,
                             "#version 330 core\nout vec2 uv;\nvoid main() {\n"
                             "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                             "    uv = p;\n    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}\n");
    GLuint fs = nw_xf_shader(GL_FRAGMENT_SHADER,
                             "#version 330 core\nuniform sampler2D kept;\nuniform vec4 alpha;\nin vec2 uv;\n"
                             "out vec4 colour;\nvoid main() { colour = vec4(texture(kept, uv).rgb, alpha.x); }\n");
    if (vs && fs) {
        GLuint pr = glCreateProgram();
        glAttachShader(pr, vs);
        glAttachShader(pr, fs);
        glLinkProgram(pr);
        GLint ok = 0;
        glGetProgramiv(pr, GL_LINK_STATUS, &ok);
        if (ok) {
            nw_xf.prog = pr;
            nw_xf.alpha = glGetUniformLocation(pr, "alpha");
            glUseProgram(pr);
            glUniform1i(glGetUniformLocation(pr, "kept"), 0);
            glUseProgram(0);
            glGenVertexArrays(1, &nw_xf.vao);
        } else {
            glDeleteProgram(pr);
        }
    }
    if (vs) {
        glDeleteShader(vs);
    }
    if (fs) {
        glDeleteShader(fs);
    }
    nw_xf.failed = nw_xf.prog == 0;
    return nw_xf.prog != 0;
}

/* The back buffer as shown now, before the packet that asks for it draws over it. */
static void nw_xf_grab(NwBeSurf *back)
{
    int w = back->tw * back->n, h = back->th * back->n;
    nw_be_resolve(back);
    if (!nw_xf.tex || nw_xf.w != w || nw_xf.h != h) {
        if (!nw_xf.tex) {
            glGenTextures(1, &nw_xf.tex);
            glGenFramebuffers(1, &nw_xf.fbo);
        }
        glBindTexture(GL_TEXTURE_2D, nw_xf.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, nw_xf.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nw_xf.tex, 0);
        nw_xf.w = w;
        nw_xf.h = h;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, back->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, nw_xf.fbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void nw_xf_draw(NwBeSurf *back, float alpha)
{
    if (!nw_xf.tex || nw_xf.w != back->tw * back->n || nw_xf.h != back->th * back->n || !nw_xf_program()) {
        return;
    }
    nw_be_resolve(back);
    GLint blend, depth, scissor, stencil, src_rgb, dst_rgb, src_a, dst_a, eq_rgb, eq_a, prog, vao, unit, tex, dmask;
    GLint vp[4];
    glGetIntegerv(GL_BLEND, &blend);
    glGetIntegerv(GL_DEPTH_TEST, &depth);
    glGetIntegerv(GL_SCISSOR_TEST, &scissor);
    glGetIntegerv(GL_STENCIL_TEST, &stencil);
    glGetIntegerv(NW_XF_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(NW_XF_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(NW_XF_BLEND_SRC_ALPHA, &src_a);
    glGetIntegerv(NW_XF_BLEND_DST_ALPHA, &dst_a);
    glGetIntegerv(NW_XF_BLEND_EQUATION_RGB, &eq_rgb);
    glGetIntegerv(NW_XF_BLEND_EQUATION_ALPHA, &eq_a);
    glGetIntegerv(NW_XF_CURRENT_PROGRAM, &prog);
    glGetIntegerv(NW_XF_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(NW_XF_ACTIVE_TEXTURE, &unit);
    glGetIntegerv(NW_XF_DEPTH_WRITEMASK, &dmask);
    glGetIntegerv(NW_XF_VIEWPORT, vp);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(NW_XF_TEXTURE_BINDING_2D, &tex);

    glBindFramebuffer(GL_FRAMEBUFFER, back->fbo);
    glViewport(0, 0, nw_xf.w, nw_xf.h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(nw_xf.prog);
    float a[4] = { alpha, 0.0f, 0.0f, 0.0f };
    glUniform4fv(nw_xf.alpha, 1, a);
    glBindTexture(GL_TEXTURE_2D, nw_xf.tex);
    glBindVertexArray(nw_xf.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray((GLuint)vao);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    glActiveTexture((GLenum)unit);
    glUseProgram((GLuint)prog);
    glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb, (GLenum)src_a, (GLenum)dst_a);
    glBlendEquationSeparate((GLenum)eq_rgb, (GLenum)eq_a);
    (blend ? glEnable : glDisable)(GL_BLEND);
    (depth ? glEnable : glDisable)(GL_DEPTH_TEST);
    (scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (stencil ? glEnable : glDisable)(GL_STENCIL_TEST);
    glDepthMask(dmask ? GL_TRUE : GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

/* The sky (port/opening.c, NwSky): the title's live space in space.gsc's own blues (its streaked dome's, about
   10 31 76 deep), drawn for each pixel: no bound, no texels, at any aspect. The nebula gathers round band (where that
   dome's streaks were), streaked across it as they were, folding and drifting as it evolves, lit from the upper left
   (the logo's light) at its edges; black elsewhere, as the original's open sky. Its stars as few as the dome's, sharp,
   twinkling, the field turning slowly against the nebula. Each pixel's direction comes from the camera's axes and the
   game's projection (tan of the half fields: 0.394 tall, 0.525 across 640 GS pixels). GLSL: shaders/sky.frag.glsl. */

static struct {
    GLuint prog;
    GLint right, up, fwd, lens, band;
    bool failed;
} nw_sky_gl;
static NwSky nw_sky_frame; /* the frame's sky, blended between its packets as the frame is */

static bool nw_sky_program(void)
{
    if (nw_sky_gl.prog || nw_sky_gl.failed) {
        return nw_sky_gl.prog != 0;
    }
    GLuint vs = nw_xf_shader(GL_VERTEX_SHADER,
                             "#version 330 core\nout vec2 uv;\nvoid main() {\n"
                             "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                             "    uv = p;\n    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}\n");
    GLuint fs = nw_be_file_shader(GL_FRAGMENT_SHADER, "sky.frag.glsl");
    if (vs && fs) {
        GLuint pr = glCreateProgram();
        glAttachShader(pr, vs);
        glAttachShader(pr, fs);
        glLinkProgram(pr);
        GLint ok = 0;
        glGetProgramiv(pr, GL_LINK_STATUS, &ok);
        if (ok) {
            nw_sky_gl.prog = pr;
            nw_sky_gl.right = glGetUniformLocation(pr, "uright");
            nw_sky_gl.up = glGetUniformLocation(pr, "uup");
            nw_sky_gl.fwd = glGetUniformLocation(pr, "ufwd");
            nw_sky_gl.lens = glGetUniformLocation(pr, "lens");
            nw_sky_gl.band = glGetUniformLocation(pr, "uband");
        } else {
            glDeleteProgram(pr);
        }
    }
    if (vs) {
        glDeleteShader(vs);
    }
    if (fs) {
        glDeleteShader(fs);
    }
    nw_sky_gl.failed = nw_sky_gl.prog == 0;
    return nw_sky_gl.prog != 0;
}

/* Over a cleared rectangle of the back buffer (GL pixels), as wide as w GS pixels. */
static void nw_sky_draw(int x, int y, int w, int h, int gs_w)
{
    if (!nw_sky_frame.on || !nw_sky_program()) {
        return;
    }
    GLint blend, depth, scissor, stencil, prog, vao, dmask;
    GLint vp[4];
    glGetIntegerv(GL_BLEND, &blend);
    glGetIntegerv(GL_DEPTH_TEST, &depth);
    glGetIntegerv(GL_SCISSOR_TEST, &scissor);
    glGetIntegerv(GL_STENCIL_TEST, &stencil);
    glGetIntegerv(NW_XF_CURRENT_PROGRAM, &prog);
    glGetIntegerv(NW_XF_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(NW_XF_DEPTH_WRITEMASK, &dmask);
    glGetIntegerv(NW_XF_VIEWPORT, vp);
    glViewport(x, y, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glUseProgram(nw_sky_gl.prog);
    const NwSky *k = &nw_sky_frame;
    float lens[4] = { 0.5246f * (float)gs_w / 640.0f, 0.394f, k->time, 2.0f * 0.394f / (float)(h > 0 ? h : 1) };
    float v[4] = { 0 };
    SDL_memcpy(v, k->right, sizeof(k->right));
    glUniform4fv(nw_sky_gl.right, 1, v);
    SDL_memcpy(v, k->up, sizeof(k->up));
    glUniform4fv(nw_sky_gl.up, 1, v);
    SDL_memcpy(v, k->fwd, sizeof(k->fwd));
    glUniform4fv(nw_sky_gl.fwd, 1, v);
    glUniform4fv(nw_sky_gl.lens, 1, lens);
    glUniform4fv(nw_sky_gl.band, 1, (const float[4]){ k->band[0], k->band[1], k->band[2], 0 });
    if (!nw_xf.vao) {
        glGenVertexArrays(1, &nw_xf.vao);
    }
    glBindVertexArray(nw_xf.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray((GLuint)vao);
    glUseProgram((GLuint)prog);
    (blend ? glEnable : glDisable)(GL_BLEND);
    (depth ? glEnable : glDisable)(GL_DEPTH_TEST);
    (scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (stencil ? glEnable : glDisable)(GL_STENCIL_TEST);
    glDepthMask(dmask ? GL_TRUE : GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

/* The clouds (NwClouds): for each pixel the ray through the shell round the planet, marched; the density a 3D noise
   on the sphere's directions that drifts and changes with time (puffs form and fade), lit from the sun with a short
   march towards it for the shade under each puff. Cartoon colours: white tops, soft blue shade. The first point
   where the clouds show gives the fragment's depth through the command's own view, so what stands in front of them
   (the station, the logo) keeps them out. GLSL: shaders/clouds.frag.glsl. */

static struct {
    GLuint prog;
    GLint lv, inv, c, r, sun;
    bool failed;
} nw_clouds_gl;

/* The opening's Universal globe (NwClouds' film fields), in two passes of one program. u_pass.x 0: the globe over
   the station earth's tiles: for each pixel the ray onto its sphere, the map read by the point's direction turned
   into the map's frame (it turns with the globe), lit as the Corporate logo's globe (corporate()), the film
   UNIVERSAL's shadow on it; the point's depth written, so what stands before the earth keeps it out and what comes
   after behind it stays hidden. u_pass.x 1: the halo, added round the station earth's tiles (u_pass.y their radius)
   at the depth of its middle, before the globe: the film's blue-white billowing corona, or the station earth's
   whiter, smoother one (u_e.z between them). GLSL: shaders/earth.frag.glsl. */

static struct {
    GLuint prog, tex, mask_tex, cap_tex;
    GLint lv, inv, c, e, rot, pass, map, mask, txt, arc, ty, tkey, look, cam, cap;
    const uint8_t *map_data, *mask_data, *cap_data; /* the images uploaded */
    bool failed;
} nw_earth_gl;
static NwClouds nw_clouds_frame;

/* The passers-by (NwClouds.fx), drawn before the clouds in the same view: each placed in the world, so the planets,
   the earth and the station stand in front of it as they should; drawn on the screen from its projection, cartoon
   style. A comet: a soft head and a long tail behind it; a shooting star: a short quick streak; a saucer: a little
   flying saucer (a dome on a disc) with lights going round its rim. GLSL: shaders/fx.frag.glsl. */

static struct {
    GLuint prog;
    GLint lv, fx, vp;
    bool failed;
} nw_fx_gl;

static bool nw_fx_program(void)
{
    if (nw_fx_gl.prog || nw_fx_gl.failed) {
        return nw_fx_gl.prog != 0;
    }
    GLuint vs = nw_xf_shader(GL_VERTEX_SHADER,
                             "#version 330 core\nout vec2 ndc;\nvoid main() {\n"
                             "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                             "    ndc = p * 2.0 - 1.0;\n    gl_Position = vec4(ndc, 0.0, 1.0);\n}\n");
    GLuint fs = nw_be_file_shader(GL_FRAGMENT_SHADER, "fx.frag.glsl");
    if (vs && fs) {
        GLuint pr = glCreateProgram();
        glAttachShader(pr, vs);
        glAttachShader(pr, fs);
        glLinkProgram(pr);
        GLint ok = 0;
        glGetProgramiv(pr, GL_LINK_STATUS, &ok);
        if (ok) {
            nw_fx_gl.prog = pr;
            nw_fx_gl.lv = glGetUniformLocation(pr, "u_lv");
            nw_fx_gl.fx = glGetUniformLocation(pr, "u_fx");
            nw_fx_gl.vp = glGetUniformLocation(pr, "u_vp");
        } else {
            glDeleteProgram(pr);
        }
    }
    if (vs) {
        glDeleteShader(vs);
    }
    if (fs) {
        glDeleteShader(fs);
    }
    nw_fx_gl.failed = nw_fx_gl.prog == 0;
    return nw_fx_gl.prog != 0;
}

static bool nw_earth_program(void)
{
    if (nw_earth_gl.prog || nw_earth_gl.failed) {
        return nw_earth_gl.prog != 0;
    }
    GLuint vs = nw_xf_shader(GL_VERTEX_SHADER,
                             "#version 330 core\nout vec2 ndc;\nvoid main() {\n"
                             "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                             "    ndc = p * 2.0 - 1.0;\n    gl_Position = vec4(ndc, 0.0, 1.0);\n}\n");
    GLuint fs = nw_be_file_shader(GL_FRAGMENT_SHADER, "earth.frag.glsl");
    if (vs && fs) {
        GLuint pr = glCreateProgram();
        glAttachShader(pr, vs);
        glAttachShader(pr, fs);
        glLinkProgram(pr);
        GLint ok = 0;
        glGetProgramiv(pr, GL_LINK_STATUS, &ok);
        if (ok) {
            nw_earth_gl.prog = pr;
            nw_earth_gl.lv = glGetUniformLocation(pr, "u_lv");
            nw_earth_gl.inv = glGetUniformLocation(pr, "u_inv");
            nw_earth_gl.c = glGetUniformLocation(pr, "u_c");
            nw_earth_gl.e = glGetUniformLocation(pr, "u_e");
            nw_earth_gl.rot = glGetUniformLocation(pr, "u_rot");
            nw_earth_gl.pass = glGetUniformLocation(pr, "u_pass");
            nw_earth_gl.map = glGetUniformLocation(pr, "u_map");
            nw_earth_gl.mask = glGetUniformLocation(pr, "u_mask");
            nw_earth_gl.txt = glGetUniformLocation(pr, "u_txt");
            nw_earth_gl.arc = glGetUniformLocation(pr, "u_arc");
            nw_earth_gl.ty = glGetUniformLocation(pr, "u_ty");
            nw_earth_gl.tkey = glGetUniformLocation(pr, "u_tkey");
            nw_earth_gl.look = glGetUniformLocation(pr, "u_look");
            nw_earth_gl.cam = glGetUniformLocation(pr, "u_cam");
            nw_earth_gl.cap = glGetUniformLocation(pr, "u_cap");
        } else {
            glDeleteProgram(pr);
        }
    }
    if (vs) {
        glDeleteShader(vs);
    }
    if (fs) {
        glDeleteShader(fs);
    }
    nw_earth_gl.failed = nw_earth_gl.prog == 0;
    return nw_earth_gl.prog != 0;
}

/* An RGBA image into tex the first time it comes: linear, wrapping across (wrap) or clamped. */
static void nw_earth_upload(GLuint *tex, const uint8_t **had, const uint8_t *px, int w, int h, bool wrap, bool mips)
{
    if (!px || px == *had) {
        return;
    }
    if (!*tex) {
        glGenTextures(1, tex);
    }
    glBindTexture(GL_TEXTURE_2D, *tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mips ? 1000 : 0);
    if (mips) {
        glGenerateMipmap(GL_TEXTURE_2D); /* the Corporate look's blurred land mask (its alpha a few levels down) */
    }
    *had = px;
}

/* The film earth and its halo (nw_earth_fs), its images uploaded the first time they come; on units 5, 6 and 7,
   given back. */
static void nw_earth_draw(const NwClouds *k, const float lv[16], const float inv[16])
{
    bool film = k->film_a > 0.0f && k->map && k->cap;
    if ((!film && k->halo <= 0.0f) || !nw_earth_program()) {
        return;
    }
    GLint unit, bound5, bound6, bound7;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glActiveTexture(GL_TEXTURE0 + 5);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound5);
    if (film) {
        nw_earth_upload(&nw_earth_gl.cap_tex, &nw_earth_gl.cap_data, k->cap, k->cap_w, k->cap_h, false, false);
    }
    glBindTexture(GL_TEXTURE_2D, nw_earth_gl.cap_tex);
    glActiveTexture(GL_TEXTURE0 + 6);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound6);
    nw_earth_upload(&nw_earth_gl.mask_tex, &nw_earth_gl.mask_data, k->mask, k->mask_w, k->mask_h, false, false);
    glBindTexture(GL_TEXTURE_2D, nw_earth_gl.mask_tex);
    glActiveTexture(GL_TEXTURE0 + 7);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound7);
    if (film) {
        nw_earth_upload(&nw_earth_gl.tex, &nw_earth_gl.map_data, k->map, k->map_w, k->map_h, true, true);
    }
    glBindTexture(GL_TEXTURE_2D, nw_earth_gl.tex);
    glUseProgram(nw_earth_gl.prog);
    glUniformMatrix4fv(nw_earth_gl.lv, 1, GL_FALSE, lv);
    glUniformMatrix4fv(nw_earth_gl.inv, 1, GL_FALSE, inv);
    float c[4] = { k->centre[0], k->centre[1], k->centre[2], k->film_r };
    float e[4] = { k->film_a, k->halo, k->halo_style, k->time };
    glUniform4fv(nw_earth_gl.c, 1, c);
    glUniform4fv(nw_earth_gl.e, 1, e);
    glUniform4fv(nw_earth_gl.rot, 3, &k->rot[0][0]);
    glUniform1i(nw_earth_gl.map, 7);
    glUniform1i(nw_earth_gl.mask, 6);
    glUniform4fv(nw_earth_gl.txt, 4, &k->text[0][0]);
    glUniform4fv(nw_earth_gl.arc, 1, k->text_arc);
    float ty[4] = { k->text_y[0], k->text_y[1], k->mask && nw_earth_gl.mask_tex ? k->text_y[2] : 0.0f, 0.0f };
    glUniform4fv(nw_earth_gl.ty, 1, ty);
    glUniform4fv(nw_earth_gl.tkey, 1, (const float[4]){ k->text_key[0], k->text_key[1], k->text_key[2], 0 });
    glUniform4fv(nw_earth_gl.look, 1, k->look);
    glUniform4fv(nw_earth_gl.cam, 3, &k->cam[0][0]);
    glUniform1i(nw_earth_gl.cap, 5);
    /* the halo first, from the tiles' edge, then the film earth over it: the film sphere's rim, just outside the
       tiles, fades with it over the halo there */
    if (k->halo > 0.0f) {
        glUniform4fv(nw_earth_gl.pass, 1, (const float[4]){ 1, k->r_earth, 0, 0 });
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    if (film) {
        glUniform4fv(nw_earth_gl.pass, 1, (const float[4]){ 0, k->r_earth, 0, 0 });
        glDepthMask(GL_TRUE);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_FALSE);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound7);
    glActiveTexture(GL_TEXTURE0 + 6);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound6);
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound5);
    glActiveTexture((GLenum)unit);
}

static bool nw_clouds_program(void)
{
    if (nw_clouds_gl.prog || nw_clouds_gl.failed) {
        return nw_clouds_gl.prog != 0;
    }
    GLuint vs = nw_xf_shader(GL_VERTEX_SHADER,
                             "#version 330 core\nout vec2 ndc;\nvoid main() {\n"
                             "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                             "    ndc = p * 2.0 - 1.0;\n    gl_Position = vec4(ndc, 0.0, 1.0);\n}\n");
    GLuint fs = nw_be_file_shader(GL_FRAGMENT_SHADER, "clouds.frag.glsl");
    if (vs && fs) {
        GLuint pr = glCreateProgram();
        glAttachShader(pr, vs);
        glAttachShader(pr, fs);
        glLinkProgram(pr);
        GLint ok = 0;
        glGetProgramiv(pr, GL_LINK_STATUS, &ok);
        if (ok) {
            nw_clouds_gl.prog = pr;
            nw_clouds_gl.lv = glGetUniformLocation(pr, "u_lv");
            nw_clouds_gl.inv = glGetUniformLocation(pr, "u_inv");
            nw_clouds_gl.c = glGetUniformLocation(pr, "u_c");
            nw_clouds_gl.r = glGetUniformLocation(pr, "u_r");
            nw_clouds_gl.sun = glGetUniformLocation(pr, "u_sun");
        } else {
            glDeleteProgram(pr);
        }
    }
    if (vs) {
        glDeleteShader(vs);
    }
    if (fs) {
        glDeleteShader(fs);
    }
    nw_clouds_gl.failed = nw_clouds_gl.prog == 0;
    return nw_clouds_gl.prog != 0;
}

/* m's inverse (4 x 4, Gauss-Jordan); false when singular. */
static bool nw_inverse4(const float m[16], float out[16])
{
    float a[4][8];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            a[r][c] = m[r * 4 + c];
            a[r][4 + c] = r == c ? 1.0f : 0.0f;
        }
    }
    for (int c = 0; c < 4; c++) {
        int piv = c;
        for (int r = c + 1; r < 4; r++) {
            if (SDL_fabsf(a[r][c]) > SDL_fabsf(a[piv][c])) {
                piv = r;
            }
        }
        if (SDL_fabsf(a[piv][c]) < 1e-12f) {
            return false;
        }
        for (int k = 0; k < 8; k++) {
            float t = a[c][k];
            a[c][k] = a[piv][k];
            a[piv][k] = t;
        }
        float inv = 1.0f / a[c][c];
        for (int k = 0; k < 8; k++) {
            a[c][k] *= inv;
        }
        for (int r = 0; r < 4; r++) {
            if (r != c && a[r][c] != 0.0f) {
                float f = a[r][c];
                for (int k = 0; k < 8; k++) {
                    a[r][k] -= f * a[c][k];
                }
            }
        }
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r][4 + c];
        }
    }
    return true;
}

static void nw_clouds_draw(const NwPacket *p, const NwCmd *c)
{
    const NwClouds *k = &nw_clouds_frame;
    if (!k->on || c->view >= p->nviews || !nw_clouds_program()) {
        return;
    }
    float lv[16], inv[16];
    nw_be_screen(&p->views[c->view], nw_be.width, nw_be.height, lv);
    if (!nw_inverse4(lv, inv)) {
        return;
    }
    GLint blend, depth, scissor, stencil, prog, vao, dmask, dfunc, src_rgb, dst_rgb, src_a, dst_a, eq_rgb, eq_a;
    glGetIntegerv(GL_BLEND, &blend);
    glGetIntegerv(GL_DEPTH_TEST, &depth);
    glGetIntegerv(GL_SCISSOR_TEST, &scissor);
    glGetIntegerv(GL_STENCIL_TEST, &stencil);
    glGetIntegerv(NW_XF_CURRENT_PROGRAM, &prog);
    glGetIntegerv(NW_XF_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(NW_XF_DEPTH_WRITEMASK, &dmask);
    glGetIntegerv(0x0B74 /* GL_DEPTH_FUNC */, &dfunc);
    glGetIntegerv(NW_XF_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(NW_XF_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(NW_XF_BLEND_SRC_ALPHA, &src_a);
    glGetIntegerv(NW_XF_BLEND_DST_ALPHA, &dst_a);
    glGetIntegerv(NW_XF_BLEND_EQUATION_RGB, &eq_rgb);
    glGetIntegerv(NW_XF_BLEND_EQUATION_ALPHA, &eq_a);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    if (!nw_xf.vao) {
        glGenVertexArrays(1, &nw_xf.vao);
    }
    glBindVertexArray(nw_xf.vao);
    nw_earth_draw(k, lv, inv);
    bool any = false;
    for (int i = 0; i < 4; i++) {
        any |= k->fx[i][0] > 0.5f;
    }
    if (any && nw_fx_program()) {
        GLint vpx[4];
        glGetIntegerv(NW_XF_VIEWPORT, vpx);
        float vp[4] = { (float)vpx[2], (float)vpx[3], k->time, 0.0f };
        glUseProgram(nw_fx_gl.prog);
        glUniformMatrix4fv(nw_fx_gl.lv, 1, GL_FALSE, lv);
        glUniform4fv(nw_fx_gl.fx, 8, &k->fx[0][0]);
        glUniform4fv(nw_fx_gl.vp, 1, vp);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(nw_clouds_gl.prog);
    glUniformMatrix4fv(nw_clouds_gl.lv, 1, GL_FALSE, lv);
    glUniformMatrix4fv(nw_clouds_gl.inv, 1, GL_FALSE, inv);
    float cc[4] = { k->centre[0], k->centre[1], k->centre[2], k->r_earth };
    float rr[4] = { k->r_in, k->r_out, k->time, k->opacity };
    float ss[4] = { k->sun[0], k->sun[1], k->sun[2], 0.0f };
    glUniform4fv(nw_clouds_gl.c, 1, cc);
    glUniform4fv(nw_clouds_gl.r, 1, rr);
    glUniform4fv(nw_clouds_gl.sun, 1, ss);
    if (!nw_xf.vao) {
        glGenVertexArrays(1, &nw_xf.vao);
    }
    glBindVertexArray(nw_xf.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray((GLuint)vao);
    glUseProgram((GLuint)prog);
    glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb, (GLenum)src_a, (GLenum)dst_a);
    glBlendEquationSeparate((GLenum)eq_rgb, (GLenum)eq_a);
    (blend ? glEnable : glDisable)(GL_BLEND);
    (depth ? glEnable : glDisable)(GL_DEPTH_TEST);
    (scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (stencil ? glEnable : glDisable)(GL_STENCIL_TEST);
    glDepthFunc((GLenum)dfunc);
    glDepthMask(dmask ? GL_TRUE : GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

/* The frame's sky from its packets: the axes blended by t as the frame's objects are. */
static void nw_sky_blend(const NwPacket *prev, const NwPacket *p, float t)
{
    nw_sky_frame.on = p && p->sky.on;
    if (!nw_sky_frame.on) {
        return;
    }
    nw_sky_frame = p->sky;
    if (!prev || !prev->sky.on || t >= 1.0f) {
        return;
    }
    float *dst[3] = { nw_sky_frame.right, nw_sky_frame.up, nw_sky_frame.fwd };
    const float *a[3] = { prev->sky.right, prev->sky.up, prev->sky.fwd };
    for (int k = 0; k < 3; k++) {
        float l = 0;
        for (int i = 0; i < 3; i++) {
            dst[k][i] = a[k][i] + (dst[k][i] - a[k][i]) * t;
            l += dst[k][i] * dst[k][i];
        }
        l = l > 0 ? 1.0f / SDL_sqrtf(l) : 1.0f;
        for (int i = 0; i < 3; i++) {
            dst[k][i] *= l;
        }
    }
    nw_sky_frame.time = prev->sky.time + (p->sky.time - prev->sky.time) * t;
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
    if (p) {
        double edge = run != p && prev ? (double)prev->edge + ((double)p->edge - (double)prev->edge) * t
                                       : (double)p->edge;
        nw_be.seconds = (float)fmod(edge / 60.0, 10000.0);
        nw_be.storm_on = p->storm_on;
        nw_be.sky.storm = run != p && prev && prev->storm_on && p->storm_on
                              ? prev->storm + (p->storm - prev->storm) * t
                              : p->storm;
    }
    nw_sky_blend(prev, p, t);
    nw_clouds_frame = p ? p->clouds : (NwClouds){ 0 };
    uint32_t njobs = nw_gpu_take(p ? p->seq : 0, &nw_be.jobs, &nw_be.jobcap);
    for (uint32_t i = 0; i < njobs; i++) {
        NwGpuOp op = nw_be.jobs[i].op;
        nw_crash_phase(op <= NW_GPU_FREE_HOST ? nw_be_job_names[op] : "job", i, njobs);
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

    /* The PS2 shows 640 pixels across at 4:3 whatever the height; within 1% of the window's aspect it fills it. */
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
        if (p->xfade > 0.0f && p->xfade_id != nw_xf.grabbed) {
            nw_xf.grabbed = p->xfade_id;
            nw_xf_grab(back);
        }
        nw_be_run(run, back);
        if (p->xfade > 0.0f) {
            nw_xf_draw(back, p->xfade);
        }
        nw_be.drawn_t = t;
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
    }
    nw_crash_phase("present", 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (back) {
        nw_be_present(back, bw, bh, (width - w) / 2, (height - h) / 2, w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (nw_be.runs && !nw_be.checked) {
        GLenum e = glGetError();
        if (e != GL_NO_ERROR) {
            nw_log_error(NW_LOG_GL, "OpenGL error 0x%04X by the first frame", e);
        }
        nw_be.checked = true;
    }
    nw_crash_phase("between frames", 0, 0);
    return p;
}
