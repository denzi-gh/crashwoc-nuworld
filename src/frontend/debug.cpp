#include "frontend/debug.h"

#include "app.h"
#include "frontend/ui.h"
#include "imgui.h"

static struct {
    bool init;
    bool shown;
} nw_debug;

bool nw_debug_init(NwPlatform *p)
{
    if (!nw_ui_init(p)) {
        return false;
    }
    /* Mouse only: the keyboard and the pad are the game's. */
    ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    nw_debug.init = true;
    return true;
}

void nw_debug_shutdown(void)
{
    if (nw_debug.init) {
        nw_ui_shutdown();
    }
    nw_debug.init = false;
    nw_debug.shown = false;
}

bool nw_debug_event(const SDL_Event *e)
{
    if (!nw_debug.init) {
        return false;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F1) {
        if (!e->key.repeat) {
            nw_debug.shown = !nw_debug.shown;
            ImGui::GetIO().ClearInputKeys();
            ImGui::GetIO().ClearInputMouse();
        }
        return true;
    }
    /* Hidden, no ImGui frame drains its input queue. */
    if (!nw_debug.shown) {
        if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST) {
            nw_ui_event(e);
        }
        return false;
    }
    nw_ui_event(e);
    const ImGuiIO &io = ImGui::GetIO();
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return io.WantCaptureMouse;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
        return io.WantCaptureKeyboard;
    default:
        return false;
    }
}

bool nw_debug_wants_keyboard(void)
{
    return nw_debug.shown && ImGui::GetIO().WantCaptureKeyboard;
}

void nw_debug_frame(void)
{
    if (!nw_debug.shown) {
        return;
    }
    nw_ui_begin();
    if (ImGui::BeginMainMenuBar()) {
        char rate[32];
        SDL_snprintf(rate, sizeof(rate), "%.0f fps", ImGui::GetIO().Framerate);
        ImGui::TextDisabled(NW_APP_NAME);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(rate).x);
        ImGui::TextUnformatted(rate);
        ImGui::EndMainMenuBar();
    }
    nw_ui_end();
}
