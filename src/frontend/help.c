#include "frontend/help.h"

#include "app.h"
#include "frontend/debug.h"
#include "gfx/gl.h"
#include "platform/buildid.h"
#include "platform/log.h"
#include "vfs/extract.h"

#define NW_HELP_ISSUES "https://github.com/denzi-gh/crashwoc-nuworld/issues/new"

static struct {
    SDL_Window *window;
    char *root;
} nw_help;

/* A fullscreen game is minimized, or the browser opens behind it. */
static void nw_help_open(void *url)
{
    if (!SDL_OpenURL(url)) {
        SDL_Log("help: cannot open %s: %s", (const char *)url, SDL_GetError());
        return;
    }
    if (SDL_GetWindowFlags(nw_help.window) & SDL_WINDOW_FULLSCREEN) {
        SDL_MinimizeWindow(nw_help.window);
    }
}

static const char *nw_help_gl(GLenum name)
{
    const GLubyte *s = glGetString(name);
    return s ? (const char *)s : "?";
}

static void nw_help_info(void *unused)
{
    (void)unused;
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(nw_help.window));
    char display[64] = "?";
    if (mode) {
        SDL_snprintf(display, sizeof(display), "%dx%d at %.0f Hz", mode->w, mode->h, mode->refresh_rate);
    }
    int w = 0, h = 0, sdl = SDL_GetVersion();
    SDL_GetWindowSizeInPixels(nw_help.window, &w, &h);
    bool fullscreen = SDL_GetWindowFlags(nw_help.window) & SDL_WINDOW_FULLSCREEN;
    const NwManifest *disc = nw_help.root ? nw_extract_manifest(nw_help.root, nw_disc_manifests) : NULL;
    char *folder = nw_log_redact(nw_help.root ? nw_help.root : "?", NULL), *text = NULL;
    SDL_asprintf(&text,
                 NW_APP_NAME " %s (decomp %s)\n"
                 "%s, %d CPU threads, %d MB RAM, SDL %d.%d.%d\n"
                 "OpenGL %s, %s, %s\n"
                 "Display %s, window %dx%d%s\n"
                 "Disc %s\n"
                 "Data folder %s\n",
                 nw_build_id, nw_build_decomp, SDL_GetPlatform(), SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM(),
                 SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl),
                 nw_help_gl(GL_VERSION), nw_help_gl(GL_RENDERER), nw_help_gl(GL_VENDOR), display, w, h,
                 fullscreen ? " fullscreen" : "", disc ? disc->name : "not imported", folder ? folder : "?");
    if (text && SDL_SetClipboardText(text)) {
        SDL_Log("help: system info copied to the clipboard");
    } else {
        SDL_Log("help: cannot copy the system info: %s", SDL_GetError());
    }
    SDL_free(text);
    SDL_free(folder);
}

void nw_help_start(SDL_Window *window, const char *root)
{
    nw_help.window = window;
    nw_help.root = root ? SDL_strdup(root) : NULL;
    if (nw_help.root) {
        nw_debug_action("Help", "Open data folder", SDL_SCANCODE_UNKNOWN, nw_help_open, nw_help.root);
    }
    nw_debug_action("Help", "Copy system info", SDL_SCANCODE_UNKNOWN, nw_help_info, NULL);
    nw_debug_action("Help", "Send beta feedback", SDL_SCANCODE_UNKNOWN, nw_help_open,
                    (void *)(NW_HELP_ISSUES "?template=0-beta_feedback.yml"));
    nw_debug_action("Help", "Report a bug", SDL_SCANCODE_UNKNOWN, nw_help_open, (void *)(NW_HELP_ISSUES "/choose"));
}

void nw_help_stop(void)
{
    SDL_free(nw_help.root);
    nw_help.root = NULL;
}
