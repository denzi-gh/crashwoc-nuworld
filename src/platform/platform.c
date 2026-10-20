#include "platform/platform.h"

#include "app.h"
#include "gfx/gl.h"

static void nw_platform_fail(const char *what)
{
    char msg[512];
    SDL_snprintf(msg, sizeof(msg), "%s:\n%s", what, SDL_GetError());
    SDL_Log("%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_APP_NAME, msg, NULL);
}

bool nw_platform_init(NwPlatform *p, const char *title, int width, int height)
{
    SDL_zerop(p);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        nw_platform_fail("SDL could not start");
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    p->window = SDL_CreateWindow(title, width, height,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!p->window) {
        nw_platform_fail("Could not create the window");
        nw_platform_shutdown(p);
        return false;
    }

    p->gl = SDL_GL_CreateContext(p->window);
    if (!p->gl) {
        nw_platform_fail("OpenGL 3.3 is required");
        nw_platform_shutdown(p);
        return false;
    }

    if (!nw_gl_load()) {
        nw_platform_fail("Could not load the OpenGL functions");
        nw_platform_shutdown(p);
        return false;
    }

    SDL_GL_SetSwapInterval(1);
    SDL_Log("OpenGL %s (%s)", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
    return true;
}

void nw_platform_shutdown(NwPlatform *p)
{
    if (p->gl) {
        SDL_GL_DestroyContext(p->gl);
    }
    if (p->window) {
        SDL_DestroyWindow(p->window);
    }
    SDL_zerop(p);
    SDL_Quit();
}
