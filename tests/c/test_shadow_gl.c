/* Render the real backend into a hidden GL window: overlaps, missing receivers, bone movement and MSAA. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "data/image_tables.h"
#include "frontend/debug.h"
#include "gfx/backend.h"
#include "gfx/gl.h"

/* Only the debug UI is replaced; the rendering, uploads, shaders and packet exchange are production code. */
static bool *disabled;
void nw_debug_toggle(const char *menu, const char *label, bool *value)
{
    (void)menu;
    if (strcmp(label, "Disable native shadows") == 0) disabled = value;
}
void nw_debug_panel(const char *m, const char *t, NwDebugFn f, void *u, bool o)
{ (void)m; (void)t; (void)f; (void)u; (void)o; }
void nw_debug_action(const char *m, const char *l, SDL_Scancode k, NwDebugFn f, void *u)
{ (void)m; (void)l; (void)k; (void)f; (void)u; }
void nw_debug_text(const char *fmt, ...) { (void)fmt; }
char *nw_debug_file(const char *f, const char *e) { (void)f; (void)e; return NULL; }

static int failures;
static float receiver_slope;
static NwGpuTex *caster_texture;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
static const NwVuProg unlit = { "vu_load_tricu_mpg", 0, 0 }, lit = { "vu_load_tric_mpg", 0, 0 };

static void identity(float m[16])
{
    memset(m, 0, 16 * sizeof(*m));
    m[0] = m[5] = m[10] = m[15] = 1;
}

static void vertex(NwVuSpriteVertex *v, float x, float y, float z)
{
    memset(v, 0, sizeof(*v));
    v->pos[0] = x;
    v->pos[1] = y;
    v->pos[2] = z;
    memset(v->col, 255, 3);
    v->col[3] = 128;
}

static void quad(NwVuSpriteVertex *v, float x0, float x1, float y)
{
    static const int order[6] = { 0, 1, 2, 1, 2, 3 };
    for (int k = 0; k < 6; k++) {
        int i = order[k];
        vertex(&v[k], i & 1 ? x1 : x0, y, i & 2 ? 1 : -1);
    }
}

static unsigned render(NwGpuMesh *mesh, NwGpuSkin *skin, float shift, bool full_floor, float floor_y, int msaa,
                       int x, int y, const char *png)
{
    NwPacket *p = nw_packet_building();
    uint32_t index;
    NwView *view = nw_packet_view(p, &index);
    *view = (NwView){ 0 };
    view->vs[0] = 5120;
    view->vs[6] = 1000000;
    view->vs[9] = -4096;
    view->vs[12] = 5120;
    view->vs[13] = 4096;
    view->vs[14] = 4000000;
    view->vs[15] = 1;
    view->scissor[2] = 639;
    view->scissor[3] = 511;
    NwMtlSnap *m = nw_packet_mtl(p, &index);
    *m = (NwMtlSnap){ .gs = { .ztst = 2 } };
    m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 128;
    NwMtlSnap *caster_mtl = nw_packet_mtl(p, &index);
    *caster_mtl = *m;
    caster_mtl->tex = caster_texture;
    caster_mtl->gs.ate = caster_texture != NULL;
    caster_mtl->gs.atst = 6; /* GREATER than alpha zero */

    NwCmd *c = nw_packet_cmd(p);
    c->op = NW_CMD_TARGET;
    c->target.surf = (NwSurf){ NW_SURF_BACK, 1024, 512, true };
    c->target.width = 640;
    c->target.height = 512;
    c = nw_packet_cmd(p);
    c->op = NW_CMD_CLEAR;
    memset(c->clear.rgba, 255, 4);
    c->clear.mask = 3;
    c = nw_packet_cmd(p);
    c->op = NW_CMD_VERTS;
    c->mode = NW_VERTS_TRIS;
    c->mpg = &unlit;
    c->light = NW_LIGHT_NONE;
    identity(c->local);
    c->count = 6;
    NwVuSpriteVertex *v = nw_packet_verts(p, 6, &c->first);
    quad(v, -1, full_floor ? 1 : 0, floor_y);
    for (int k = 0; k < 6; k++) v[k].pos[1] += receiver_slope * v[k].pos[0];

    c = nw_packet_cmd(p);
    c->op = NW_CMD_SHADOW_BEGIN;
    c->light = NW_LIGHT_NONE;
    identity(c->local);
    c->local[5] = 0; /* ScaleFlatShadow's actual collapsed Y axis */
    c->local[1] = receiver_slope;
    c->local[13] = 0.025f;
    c->alpha = 127;
    c = nw_packet_cmd(p);
    c->op = skin ? NW_CMD_SKIN : NW_CMD_DRAW;
    c->mpg = skin ? &lit : &unlit;
    c->shadow = true;
    c->mtl = 1;
    c->light = NW_LIGHT_NONE;
    identity(c->local);
    if (skin) {
        c->skin = skin;
        float (*joints)[16] = nw_packet_joints(p, 1, &c->first);
        identity(joints[0]);
        joints[0][12] = shift;
        c->count = 1;
    } else {
        c->mesh = mesh;
    }
    c = nw_packet_cmd(p);
    c->op = NW_CMD_SHADOW_END;
    nw_packet_publish(0);
    nw_backend_set_options(&(NwRenderOptions){ .scale = 1, .msaa = msaa });
    CHECK(nw_backend_frame(640, 512, -1) != NULL);
    if (png) CHECK(nw_backend_save(png));
    unsigned char pixel[4] = { 0 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0];
}

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "GL unavailable: %s\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_Window *window = SDL_CreateWindow("Native shadow test", 640, 512, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context) {
        fprintf(stderr, "GL unavailable: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    CHECK(nw_gl_load());
    CHECK(nw_backend_init());
    printf("GL: %s\n", (const char *)glGetString(GL_RENDERER));
    NwVifVertex verts[4] = { 0 };
    uint32_t tris[12] = { 0, 1, 2, 1, 2, 3, 0, 1, 2, 1, 2, 3 }; /* overlapping geometry */
    for (int i = 0; i < 4; i++) {
        verts[i].pos[0] = i & 1 ? 0.7f : -0.7f;
        verts[i].pos[1] = 1;
        verts[i].pos[2] = i & 2 ? 0.6f : -0.6f;
        verts[i].tex[0] = i & 1 ? 4096 : 0;
        verts[i].tex[1] = i & 2 ? 4096 : 0;
        memset(verts[i].col, 128, sizeof(verts[i].col));
    }
    NwGpuMesh mesh = { .mesh = { .verts = verts, .tris = tris, .nverts = 4, .ntris = 4 } };
    nw_gpu_queue(NW_GPU_UPLOAD_MESH, &mesh);
    unsigned overlap = render(&mesh, NULL, 0, true, 0, 0, 200, 256, argc > 1 ? argv[1] : NULL);
    printf("overlap=%u\n", overlap);
    CHECK(overlap >= 189 && overlap <= 193); /* 25% once, despite four overlapping triangles */
    CHECK(render(&mesh, NULL, 0, false, 0, 0, 440, 256, NULL) >= 253); /* no shadow floating over a hole */
    CHECK(render(&mesh, NULL, 0, true, -1, 0, 200, 256, NULL) >= 253); /* no shadow on a lower storey */
    CHECK(render(&mesh, NULL, 0, true, 1, 0, 200, 256, NULL) >= 253); /* foreground occluder */
    receiver_slope = 0.35f;
    CHECK(render(&mesh, NULL, 0, true, 0, 0, 200, 256, NULL) <= 193);
    receiver_slope = 0;
    CHECK(disabled != NULL);
    *disabled = true;
    CHECK(render(&mesh, NULL, 0, true, 0, 0, 200, 256, NULL) >= 253);
    *disabled = false;
    CHECK(render(&mesh, NULL, 0, true, 0, 4, 200, 256, NULL) <= 193);

    NwVifSkinVertex skverts[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        memcpy(skverts[i].pos, verts[i].pos, sizeof(skverts[i].pos));
        skverts[i].weight[0] = 32768;
    }
    NwGpuSkin skin = { .skin = { .verts = skverts, .tris = tris, .nverts = 4, .ntris = 4 } };
    nw_gpu_queue(NW_GPU_UPLOAD_SKIN, &skin);
    CHECK(render(NULL, &skin, 0, true, 0, 0, 200, 256, NULL) <= 193);
    CHECK(render(NULL, &skin, 1, true, 0, 0, 200, 256, NULL) >= 253); /* shadow follows the animated joint */
    CHECK(render(NULL, &skin, 1, true, 0, 0, 500, 256, NULL) <= 193);
    uint8_t texels[8] = { 255, 255, 255, 0, 255, 255, 255, 128 };
    NwGpuTex cutout = { .tex = { .type = 3, .tcc = true, .nlevels = 1,
                                  .levels = { { 2, 1, texels } } } };
    caster_texture = &cutout;
    nw_gpu_queue(NW_GPU_UPLOAD_TEX, &cutout);
    CHECK(render(&mesh, NULL, 0, true, 0, 0, 200, 256, NULL) >= 253); /* transparent half casts no shadow */
    CHECK(render(&mesh, NULL, 0, true, 0, 0, 440, 256, NULL) <= 193);
    nw_backend_shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("native shadow rendering: %s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
