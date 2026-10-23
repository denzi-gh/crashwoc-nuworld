#include "gfx/view.h"

#include <math.h>

#include "app.h"
#include "frontend/debug.h"
#include "gfx/backend.h"
#include "port/game.h"
#include "port/scene.h"
#include "vfs/manifest.h"

/* The fly camera: W/S along the view, A/D sideways, Q/E down and up, Shift four times as fast. */
static void nw_view_move(NwSceneCam *c, float speed, float dt)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    float step = speed * dt * (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 4.0f : 1.0f);
    float fwd = (float)(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]);
    float side = (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    float rise = (float)(keys[SDL_SCANCODE_E] - keys[SDL_SCANCODE_Q]);
    float cy = cosf(c->yaw), sy = sinf(c->yaw), cp = cosf(c->pitch), sp = sinf(c->pitch);
    c->pos[0] += step * (fwd * sy * cp + side * cy);
    c->pos[1] += step * (fwd * sp + rise);
    c->pos[2] += step * (fwd * cy * cp - side * sy);
}

typedef struct NwViewer {
    NwSceneInfo info;
    NwSceneCam cam;
    float speed;
    const char *scene; /* the last one asked for */
    bool loading;
    char filter[64];
} NwViewer;

static NwViewer nw_view;

static bool nw_view_drawn(void)
{
    if (!nw_view.info.drawn) {
        nw_debug_text(nw_view.info.seq ? "nothing to draw" : "loading");
    }
    return nw_view.info.drawn;
}

static void nw_view_stats(void *user)
{
    (void)user;
    if (!nw_view_drawn()) {
        return;
    }
    nw_debug_text("%u objects", nw_view.info.objects);
    nw_debug_text("%u instances", nw_view.info.instances);
    nw_debug_text("%llu triangles", (unsigned long long)nw_view.info.tris);
}

static void nw_view_scenes(void *user)
{
    (void)user;
    nw_debug_filter("filter", nw_view.filter, sizeof(nw_view.filter));
    nw_debug_list_begin("scenes", 16);
    for (size_t i = 0; i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
        size_t len = SDL_strlen(e->path);
        bool scene = len >= 4 && (SDL_strcasecmp(e->path + len - 4, ".GSC") == 0 ||
                                  SDL_strcasecmp(e->path + len - 4, ".GHG") == 0);
        if (!scene || (e->flags & NW_MANIFEST_SKIP) || !SDL_strcasestr(e->path, nw_view.filter)) {
            continue;
        }
        if (nw_debug_selectable(e->path, e->path == nw_view.scene)) {
            nw_view.scene = e->path;
            nw_view.loading = true;
            nw_scene_load(e->path);
        }
    }
    nw_debug_list_end();
    if (nw_view.loading) {
        nw_debug_text("loading %s", nw_view.scene);
    } else if (nw_view.info.seq && !nw_view.info.drawn) {
        nw_debug_text("%s: nothing to draw", nw_view.scene);
    }
}

static void nw_view_reset(void)
{
    nw_view.cam = nw_view.info.cam;
    nw_view.speed = nw_view.info.speed;
}

static void nw_view_camera(void *user)
{
    (void)user;
    if (!nw_view_drawn()) {
        return;
    }
    nw_debug_slider("Speed", &nw_view.speed, nw_view.info.speed * 0.01f, nw_view.info.speed * 100.0f, true);
    if (nw_debug_button("Reset")) {
        nw_view_reset();
    }
}

int nw_view_scene(NwPlatform *p, const char *name, int max_frames)
{
    nw_view = (NwViewer){ .speed = 1.0f, .scene = name };
    nw_debug_panel("Scene", "Scenes", nw_view_scenes, NULL, !name);
    nw_debug_panel("Scene", "Stats", nw_view_stats, NULL, false);
    nw_debug_panel("Scene", "Camera", nw_view_camera, NULL, false);
    if (!name) {
        nw_debug_show(true);
        SDL_SetWindowTitle(p->window, NW_APP_NAME " - scenes (F1)");
    }
    bool running = true;
    int frames = 0;
    uint64_t last = SDL_GetTicksNS();
    while (running && nw_game_state() == NW_GAME_RUNNING) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (nw_debug_event(&e)) {
                continue;
            }
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE)) {
                running = false;
            } else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_RMASK)) {
                nw_view.cam.yaw += e.motion.xrel * 0.004f;
                nw_view.cam.pitch = fmaxf(-1.5f, fminf(1.5f, nw_view.cam.pitch - e.motion.yrel * 0.004f));
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                nw_view.speed *= powf(1.25f, e.wheel.y);
            }
        }
        uint64_t now = SDL_GetTicksNS();
        NwSceneInfo info;
        if (nw_scene_info(&info) && info.seq != nw_view.info.seq) {
            nw_view.info = info;
            nw_view.loading = false;
            nw_view_reset();
            char title[256];
            SDL_snprintf(title, sizeof(title),
                         info.drawn ? "%s (WASD QE, right mouse, wheel; F1 debug overlay)" : "%s: nothing to draw",
                         nw_view.scene);
            SDL_SetWindowTitle(p->window, title);
        }
        if (nw_view.info.drawn) {
            if (!nw_debug_wants_keyboard()) {
                nw_view_move(&nw_view.cam, nw_view.speed, (float)(now - last) * 1e-9f);
            }
            nw_scene_set_camera(&nw_view.cam);
        }
        last = now;

        int width, height;
        SDL_GetWindowSizeInPixels(p->window, &width, &height);
        nw_backend_frame(width, height);
        nw_debug_frame();
        SDL_GL_SwapWindow(p->window);
        if (max_frames > 0 && ++frames >= max_frames) {
            running = false;
        }
    }
    return nw_game_state() == NW_GAME_FATAL ? 1 : 0;
}
