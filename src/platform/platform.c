#include "platform/platform.h"

#include "app.h"
#include "gfx/gl.h"
#include "platform/log.h"

static bool nw_platform_debug;

static void nw_platform_fail(const char *what)
{
    char msg[512];
    SDL_snprintf(msg, sizeof(msg), "%s:\n%s", what, SDL_GetError());
    nw_log_error(NW_LOG_APP, "%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_APP_NAME, msg, NULL);
}

void nw_platform_gl_debug(bool on)
{
    nw_platform_debug = on;
}

typedef void(NW_GLAPI *NwGlDebugProc)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                      const GLchar *message, const void *user);
typedef void(NW_GLAPI *PFN_nw_glDebugMessageCallback)(NwGlDebugProc callback, const void *user);

static void NW_GLAPI nw_platform_gl_message(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                            const GLchar *message, const void *user)
{
    static struct {
        GLuint id;
        unsigned count;
    } seen[256];
    (void)source;
    (void)length;
    (void)user;
    unsigned count = 0;
    for (size_t i = 0; i < SDL_arraysize(seen); i++) {
        if (seen[i].count == 0 || seen[i].id == id) {
            seen[i].id = id;
            count = ++seen[i].count;
            break;
        }
    }
    unsigned n = count;
    while (n >= 10 && n % 10 == 0) {
        n /= 10;
    }
    if (count && n != 1) {
        return;
    }
    SDL_LogPriority priority = type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
                                   ? SDL_LOG_PRIORITY_ERROR
                               : severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM
                                   ? SDL_LOG_PRIORITY_WARN
                               : severity == GL_DEBUG_SEVERITY_NOTIFICATION ? SDL_LOG_PRIORITY_TRACE
                                                                            : SDL_LOG_PRIORITY_DEBUG;
    if (count > 1) {
        SDL_LogMessage(NW_LOG_GL, priority, "%s (id %u, %u times)", message, id, count);
    } else {
        SDL_LogMessage(NW_LOG_GL, priority, "%s (id %u)", message, id);
    }
}

#ifndef SDL_PLATFORM_WINDOWS
extern const unsigned char nw_icon_png[];
extern const size_t nw_icon_png_size;

/* Windows takes the icon from the exe resource. */
static void nw_platform_icon(SDL_Window *window)
{
    SDL_IOStream *io = SDL_IOFromConstMem(nw_icon_png, nw_icon_png_size);
    SDL_Surface *icon = io ? SDL_LoadPNG_IO(io, true) : NULL;
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
}
#endif

bool nw_platform_init(NwPlatform *p, const char *title, int width, int height)
{
    SDL_zerop(p);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        nw_platform_fail("SDL could not start");
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, nw_platform_debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);

    p->window = SDL_CreateWindow(title, width, height,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!p->window) {
        nw_platform_fail("Could not create the window");
        nw_platform_shutdown(p);
        return false;
    }
#ifndef SDL_PLATFORM_WINDOWS
    nw_platform_icon(p->window);
#endif

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
    nw_log_info(NW_LOG_GL, "OpenGL %s (%s, %s), GLSL %s", (const char *)glGetString(GL_VERSION),
                (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VENDOR),
                (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    if (samples > 0) {
        nw_log_info(NW_LOG_GL, "OpenGL window: %d samples, antialiasing forced by the driver", samples);
    }
    if (nw_platform_debug) {
        PFN_nw_glDebugMessageCallback callback =
            (PFN_nw_glDebugMessageCallback)SDL_GL_GetProcAddress("glDebugMessageCallback");
        if (!callback) {
            callback = (PFN_nw_glDebugMessageCallback)SDL_GL_GetProcAddress("glDebugMessageCallbackARB");
        }
        if (callback) {
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glEnable(GL_DEBUG_OUTPUT);
            callback(nw_platform_gl_message, NULL);
            nw_log_info(NW_LOG_GL, "debug output on");
        } else {
            nw_log_warn(NW_LOG_GL, "the driver has no debug output (KHR_debug, ARB_debug_output)");
        }
    }
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
    nw_log_restore();
}
