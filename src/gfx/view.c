#include "gfx/view.h"

#include <math.h>

#include "frontend/debug.h"
#include "gfx/backend.h"
#include "port/game.h"
#include "port/scene.h"

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

int nw_view_scene(NwPlatform *p, const char *name, int max_frames)
{
    NwSceneInfo info;
    NwSceneCam cam = { { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f };
    float speed = 1.0f;
    bool ready = false, running = true;
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
                cam.yaw += e.motion.xrel * 0.004f;
                cam.pitch = fmaxf(-1.5f, fminf(1.5f, cam.pitch - e.motion.yrel * 0.004f));
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                speed *= powf(1.25f, e.wheel.y);
            }
        }
        uint64_t now = SDL_GetTicksNS();
        if (!ready && nw_scene_info(&info)) {
            ready = true;
            cam = info.cam;
            speed = info.speed;
            char title[256];
            SDL_snprintf(title, sizeof(title), "%s - %u instances, %llu triangles (WASD QE, right mouse, wheel)", name,
                         info.instances, (unsigned long long)info.tris);
            SDL_SetWindowTitle(p->window, title);
        }
        if (ready) {
            if (!nw_debug_wants_keyboard()) {
                nw_view_move(&cam, speed, (float)(now - last) * 1e-9f);
            }
            nw_scene_set_camera(&cam);
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
