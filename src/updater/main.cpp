#include <float.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app.h"
#include "frontend/ui.h"
#include "platform/update.h"
#include "updater/job.h"

extern "C" {
#include "gfx/gl.h"
}

#define NW_UPDATER_TITLE NW_APP_NAME " Updater"
#define NW_UPDATER_WIDTH 560
#define NW_UPDATER_HEIGHT 250

struct NwUpdaterArgs {
    const char *install;
    const char *from;
    const char *to;
    bool wait;
    char **restart;
    int restarts;
};

static NwUpdaterArgs nw_updater_args(int argc, char **argv)
{
    NwUpdaterArgs a = {};
    for (int i = 1; i < argc; i++) {
        bool value = i + 1 < argc;
        if (SDL_strcmp(argv[i], "--restart") == 0) {
            a.restart = argv + i + 1;
            a.restarts = argc - i - 1;
            break;
        } else if (SDL_strcmp(argv[i], "--wait") == 0) {
            a.wait = true;
        } else if (value && SDL_strcmp(argv[i], "--install-dir") == 0) {
            a.install = argv[++i];
        } else if (value && SDL_strcmp(argv[i], "--from") == 0) {
            a.from = argv[++i];
        } else if (value && SDL_strcmp(argv[i], "--to") == 0) {
            a.to = argv[++i];
        }
    }
    if (!a.install) {
        a.install = SDL_GetBasePath();
    }
    return a;
}

static bool nw_updater_restart(const NwUpdaterArgs &a)
{
    char *game = NULL;
    const char **args = (const char **)SDL_calloc((size_t)a.restarts + 2, sizeof(*args));
    bool ok = args && SDL_asprintf(&game, "%s" NW_UPDATER_PROGRAM, a.install) >= 0;
    if (ok) {
        args[0] = game;
        for (int i = 0; i < a.restarts; i++) {
            args[i + 1] = a.restart[i];
        }
        SDL_Process *process = SDL_CreateProcess(args, false);
        ok = process != NULL;
        SDL_DestroyProcess(process);
    }
    SDL_free(game);
    SDL_free(args);
    return ok;
}

static void nw_updater_title(const char *text)
{
    nw_ui_push_font(NW_UI_BOLD, 1.35f);
    ImGui::TextUnformatted(text);
    nw_ui_pop_font();
    ImGui::Spacing();
}

static void nw_updater_running(NwUpdater *job, NwUpdaterState state, const NwUpdaterArgs &a, bool closing)
{
    uint64_t done = 0, total = 0;
    nw_updater_poll(job, &done, &total);
    nw_updater_title("Updating NuWorld");
    if (state == NW_UPDATER_DOWNLOADING && total && !closing) {
        char overlay[32];
        SDL_snprintf(overlay, sizeof(overlay), "%d%%", (int)(done * 100 / total));
        ImGui::Text("Downloading version %s...", a.to + (*a.to == 'v'));
        ImGui::ProgressBar((float)((double)done / (double)total), ImVec2(-FLT_MIN, 0.0f), overlay);
        ImGui::TextDisabled("%.1f of %.1f MB", (double)done / 1048576.0, (double)total / 1048576.0);
    } else {
        ImGui::TextUnformatted(closing                        ? "Cancelling..."
                               : state == NW_UPDATER_WAITING  ? "Waiting for NuWorld to close..."
                               : state == NW_UPDATER_INSTALLING ? "Installing..."
                                                                : "Connecting to GitHub...");
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-FLT_MIN, 0.0f), "");
    }
}

static bool nw_updater_button(const char *label, bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::SameLine();
    return pressed;
}

int main(int argc, char **argv)
{
    NwUpdaterArgs args = nw_updater_args(argc, argv);
    NwPlatform platform = {};
    bool started = SDL_Init(SDL_INIT_VIDEO);
    if (started) {
        float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        scale = scale > 0.0f ? scale : 1.0f;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        platform.window = SDL_CreateWindow(NW_UPDATER_TITLE, (int)(NW_UPDATER_WIDTH * scale),
                                           (int)(NW_UPDATER_HEIGHT * scale),
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        platform.gl = platform.window ? SDL_GL_CreateContext(platform.window) : NULL;
    }
    if (!platform.gl || !nw_gl_load() || !nw_ui_init(&platform, false)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_UPDATER_TITLE, SDL_GetError(), platform.window);
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    char error[512] = "Start NuWorld: it runs the updater when a new version is out.";
    const char *failed = "NuWorld Updater";
    NwUpdater *job = NULL;
    if (args.from && args.to) {
        NwUpdaterConfig config = {};
        config.install = args.install;
        config.from = args.from;
        config.to = args.to;
        config.wait = args.wait;
        job = nw_updater_start(&config);
        if (!job) {
            SDL_strlcpy(error, SDL_GetError(), sizeof(error));
            failed = "Update failed";
        }
    }

    bool open = true, closing = false, cancelling = false, restart = false;
    while (open) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            nw_ui_event(&event);
            closing = closing || event.type == SDL_EVENT_QUIT;
        }
        uint64_t done = 0, total = 0;
        NwUpdaterState state = job ? nw_updater_poll(job, &done, &total) : NW_UPDATER_FAILED;
        bool running = job && state < NW_UPDATER_DONE;
        if (job && !running) {
            if (state == NW_UPDATER_FAILED) {
                SDL_strlcpy(error, nw_updater_error(job), sizeof(error));
                failed = "Update failed";
            } else if (state == NW_UPDATER_CANCELLED) {
                SDL_strlcpy(error, "Nothing was changed.", sizeof(error));
                failed = "Update cancelled";
            }
            restart = state == NW_UPDATER_DONE;
            open = !restart && !closing;
            nw_updater_free(job);
            job = NULL;
        } else if (running && closing) {
            nw_updater_cancel(job);
            open = state != NW_UPDATER_WAITING;
        } else if (closing) {
            open = false;
        }

        int width, height;
        SDL_GetWindowSizeInPixels(platform.window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.05f, 0.08f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        nw_ui_begin();
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##updater", NULL,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (job) {
            nw_updater_running(job, state, args, closing || cancelling);
            ImGui::Spacing();
            if (nw_updater_button("Cancel", state != NW_UPDATER_INSTALLING && !closing && !cancelling)) {
                closing = state == NW_UPDATER_WAITING;
                cancelling = !closing;
                nw_updater_cancel(job);
            }
        } else if (open) {
            nw_updater_title(failed);
            ImGui::TextWrapped("%s", error);
            ImGui::Spacing();
            if (nw_updater_button("Start NuWorld", true)) {
                restart = true;
                open = false;
            }
            if (args.to && nw_updater_button("Open release page", true)) {
                char url[160];
                SDL_snprintf(url, sizeof(url), "https://github.com/" NW_UPDATE_REPO "/releases/tag/v%s",
                             args.to + (*args.to == 'v'));
                SDL_OpenURL(url);
            }
            if (nw_updater_button("Close", true)) {
                open = false;
            }
        }
        ImGui::End();
        nw_ui_end();
        SDL_GL_SwapWindow(platform.window);
    }

    nw_ui_shutdown();
    SDL_GL_DestroyContext(platform.gl);
    SDL_DestroyWindow(platform.window);
    if (restart && !nw_updater_restart(args)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_UPDATER_TITLE, SDL_GetError(), NULL);
    }
    SDL_Quit();
    return 0;
}
