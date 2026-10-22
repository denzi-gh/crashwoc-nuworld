#include "gfx/view.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/gl.h"
#include "gfx/vif.h"
#include "gfx/vu.h"

/* One geom's triangles in the scene's index buffer, with its material's program and what the programs read of it. */
typedef struct NwViewDraw {
    uint32_t first, count;
    NwVuProgram program;
    NwVuMaterial mtl;
} NwViewDraw;

/* An object's draws and the bounds of its vertices. */
typedef struct NwViewObject {
    uint32_t first, count;
    float min[3], max[3];
} NwViewObject;

typedef struct NwView {
    NwVifMesh mesh;
    NwViewDraw *draws;
    NwViewObject *objects;
    NwVuTexture *textures; /* by tid; zero where a texture did not decode */
    GLuint vao, vbo, ibo;
    float pos[3], yaw, pitch, speed, near, far;
} NwView;

static bool nw_view_load(NwView *v, const NwGsc *g)
{
    v->draws = SDL_calloc(g->nblobs + 1, sizeof(*v->draws));
    v->objects = SDL_calloc(g->nobjects + 1, sizeof(*v->objects));
    v->textures = SDL_calloc(g->ntextures + 1, sizeof(*v->textures));
    if (!v->draws || !v->objects || !v->textures) {
        return false;
    }
    uint32_t ndraws = 0;
    for (uint32_t o = 0; o < g->nobjects; o++) {
        NwViewObject *obj = &v->objects[o];
        obj->first = ndraws;
        for (int k = 0; k < 3; k++) {
            obj->min[k] = INFINITY;
            obj->max[k] = -INFINITY;
        }
        for (uint32_t i = g->objects[o].first; i < g->objects[o].first + g->objects[o].count; i++) {
            const NwGscBlob *b = &g->blobs[i];
            uint32_t tris = v->mesh.ntris, verts = v->mesh.nverts;
            if (b->ot || b->faceon) {
                continue;
            }
            if (!nw_vif_decode(&v->mesh, g->data + b->offset, b->size)) {
                return false;
            }
            NwGscMtl m = nw_gsc_mtl(g, b->mtl);
            NwViewDraw *d = &v->draws[ndraws++];
            *d = (NwViewDraw){ tris, v->mesh.ntris - tris, nw_vu_program(nw_gsc_mtl_package(&m)),
                               { { m.diffuse[0] * 128.0f, m.diffuse[1] * 128.0f, m.diffuse[2] * 128.0f,
                                   m.alpha * 128.0f },
                                 { 0.0f, 0.0f },
                                 NULL,
                                 nw_mtl_state(m.attrib) } };
            if (m.tid >= 0 && (uint32_t)m.tid < g->ntextures) {
                d->mtl.tex = &v->textures[m.tid];
            }
            for (uint32_t n = verts; n < v->mesh.nverts; n++) {
                const float *p = v->mesh.verts[n].pos;
                for (int k = 0; k < 3 && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]); k++) {
                    obj->min[k] = fminf(obj->min[k], p[k]);
                    obj->max[k] = fmaxf(obj->max[k], p[k]);
                }
            }
        }
        obj->count = ndraws - obj->first;
    }
    return true;
}

static void nw_view_upload(NwView *v, const NwGsc *g, const char *name)
{
    for (uint32_t i = 0; i < g->ntextures; i++) {
        NwTex t;
        if (nw_gsc_texture(g, i, &t)) {
            nw_vu_texture(&v->textures[i], &t);
            nw_tex_free(&t);
        } else {
            fprintf(stderr, "%s: texture %u: %s\n", name, i, SDL_GetError());
        }
    }
    glGenVertexArrays(1, &v->vao);
    glBindVertexArray(v->vao);
    glGenBuffers(1, &v->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, v->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)v->mesh.nverts * (GLsizeiptr)sizeof(NwVifVertex), v->mesh.verts,
                 GL_STATIC_DRAW);
    glGenBuffers(1, &v->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)v->mesh.ntris * 12, v->mesh.tris, GL_STATIC_DRAW);
    nw_vu_attribs();
}

/* A point of an object placed by an instance (row vector times the row-major matrix), z flipped for GL. */
static void nw_view_place(const float m[16], const float p[3], float out[3])
{
    for (int c = 0; c < 3; c++) {
        out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
    }
    out[2] = -out[2];
}

static int nw_view_cmp(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Camera behind the median instance (outliers and skydomes only set the clip planes); counts for the title. */
static void nw_view_frame(NwView *v, const NwGsc *g, uint32_t *instances, uint64_t *tris)
{
    float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
    float *at = SDL_malloc((g->ninsts + 1) * 3 * sizeof(float));
    uint32_t n = 0;
    *tris = 0;
    for (uint32_t i = 0; i < g->ninsts && at; i++) {
        NwGscInst in = nw_gsc_inst(g, i);
        if (!(in.flags & 1) || in.objid < 0 || (uint32_t)in.objid >= g->nobjects) {
            continue;
        }
        const NwViewObject *obj = &v->objects[in.objid];
        const float origin[3] = { 0.0f, 0.0f, 0.0f };
        float w[3];
        for (uint32_t d = obj->first; d < obj->first + obj->count; d++) {
            *tris += v->draws[d].count;
        }
        nw_view_place(in.mtx, origin, w);
        for (int k = 0; k < 3; k++) {
            at[k * g->ninsts + n] = w[k];
        }
        n++;
        for (int corner = 0; corner < 8 && obj->min[0] <= obj->max[0]; corner++) {
            float p[3];
            for (int k = 0; k < 3; k++) {
                p[k] = corner >> k & 1 ? obj->max[k] : obj->min[k];
            }
            nw_view_place(in.mtx, p, w);
            for (int k = 0; k < 3; k++) {
                lo[k] = fminf(lo[k], w[k]);
                hi[k] = fmaxf(hi[k], w[k]);
            }
        }
    }
    float extent = 0.0f, spread = 0.0f, centre[3] = { 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < 3 && n > 0 && lo[0] <= hi[0]; k++) {
        float *axis = at + k * g->ninsts;
        qsort(axis, n, sizeof(float), nw_view_cmp);
        centre[k] = axis[n / 2];
        spread = fmaxf(spread, (axis[n * 9 / 10] - axis[n / 10]) * 0.5f);
        extent += (hi[k] - lo[k]) * (hi[k] - lo[k]) * 0.25f;
    }
    SDL_free(at);
    *instances = n;
    extent = fmaxf(sqrtf(extent), 10.0f);
    spread = fmaxf(spread, extent * 0.02f);
    v->pos[0] = centre[0];
    v->pos[1] = centre[1] + spread * 0.3f;
    v->pos[2] = centre[2] + spread * 0.8f;
    v->yaw = 0.0f;
    v->pitch = -0.3f;
    v->speed = spread * 0.25f;
    v->near = extent / 20000.0f;
    v->far = extent * 4.0f;
}

/* The screen matrix in the VU's place (row vectors): z flip, camera, GL perspective. */
static void nw_view_matrix(const NwView *v, float aspect, float out[16])
{
    float cy = cosf(v->yaw), sy = sinf(v->yaw), cp = cosf(v->pitch), sp = sinf(v->pitch);
    float basis[3][3] = {
        { cy, 0.0f, -sy },          /* right */
        { sy * sp, cp, cy * sp },   /* up */
        { sy * cp, -sp, cy * cp },  /* back */
    };
    float cam[16] = { 0 }, proj[16] = { 0 };
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            cam[r * 4 + c] = (r == 2 ? -1.0f : 1.0f) * basis[c][r];
        }
        cam[12 + c] = -(v->pos[0] * basis[c][0] + v->pos[1] * basis[c][1] + v->pos[2] * basis[c][2]);
    }
    cam[15] = 1.0f;
    float f = 1.0f / tanf(0.5f), n = v->near, fr = v->far;
    proj[0] = f / aspect;
    proj[5] = f;
    proj[10] = (n + fr) / (n - fr);
    proj[11] = -1.0f;
    proj[14] = 2.0f * n * fr / (n - fr);
    NwVuObject m;
    nw_vu1init(&m, cam, proj);
    memcpy(out, m.lv, sizeof(m.lv));
}

static void nw_view_move(NwView *v, float dt)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    float step = v->speed * dt * (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 4.0f : 1.0f);
    float fwd = (float)(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]);
    float side = (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    float rise = (float)(keys[SDL_SCANCODE_E] - keys[SDL_SCANCODE_Q]);
    float cy = cosf(v->yaw), sy = sinf(v->yaw), cp = cosf(v->pitch), sp = sinf(v->pitch);
    v->pos[0] += step * (fwd * -sy * cp + side * cy);
    v->pos[1] += step * (fwd * sp + rise);
    v->pos[2] += step * (fwd * -cy * cp - side * sy);
}

int nw_view_scene(NwPlatform *p, const NwGsc *g, const char *name, int max_frames)
{
    NwView v = { 0 };
    if (!nw_view_load(&v, g) || !nw_vu_init()) {
        fprintf(stderr, "%s: %s\n", name, SDL_GetError());
        nw_vif_free(&v.mesh);
        SDL_free(v.draws);
        SDL_free(v.objects);
        SDL_free(v.textures);
        return 1;
    }
    nw_view_upload(&v, g, name);
    uint32_t instances;
    uint64_t tris;
    nw_view_frame(&v, g, &instances, &tris);
    char title[256];
    SDL_snprintf(title, sizeof(title), "%s - %u instances, %llu triangles (WASD QE, right mouse, wheel)", name,
                 instances, (unsigned long long)tris);
    SDL_SetWindowTitle(p->window, title);

    bool running = true;
    int frames = 0;
    uint64_t last = SDL_GetTicksNS();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE)) {
                running = false;
            } else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_RMASK)) {
                v.yaw -= e.motion.xrel * 0.004f;
                v.pitch = fmaxf(-1.5f, fminf(1.5f, v.pitch - e.motion.yrel * 0.004f));
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                v.speed *= powf(1.25f, e.wheel.y);
            }
        }
        uint64_t now = SDL_GetTicksNS();
        nw_view_move(&v, (float)(now - last) * 1e-9f);
        last = now;

        int width, height;
        SDL_GetWindowSizeInPixels(p->window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.1f, 0.12f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        float view[16];
        nw_view_matrix(&v, height > 0 ? (float)width / (float)height : 1.0f, view);
        glBindVertexArray(v.vao);
        for (uint32_t i = 0; i < g->ninsts; i++) {
            NwGscInst in = nw_gsc_inst(g, i);
            if (!(in.flags & 1) || in.objid < 0 || (uint32_t)in.objid >= g->nobjects) {
                continue;
            }
            const NwViewObject *obj = &v.objects[in.objid];
            NwVuObject o;
            nw_vu1init(&o, in.mtx, view);
            for (uint32_t d = obj->first; d < obj->first + obj->count; d++) {
                nw_vu_use(v.draws[d].program, &o, &v.draws[d].mtl);
                glDrawElements(GL_TRIANGLES, (GLsizei)(v.draws[d].count * 3), GL_UNSIGNED_INT,
                               (const void *)(uintptr_t)(v.draws[d].first * 12u));
            }
        }
        SDL_GL_SwapWindow(p->window);
        if (max_frames > 0 && ++frames >= max_frames) {
            running = false;
        }
    }

    glDeleteBuffers(1, &v.ibo);
    glDeleteBuffers(1, &v.vbo);
    glDeleteVertexArrays(1, &v.vao);
    for (uint32_t i = 0; i < g->ntextures; i++) {
        nw_vu_texture_free(&v.textures[i]);
    }
    nw_vu_shutdown();
    nw_vif_free(&v.mesh);
    SDL_free(v.draws);
    SDL_free(v.objects);
    SDL_free(v.textures);
    return 0;
}
