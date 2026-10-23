#include "frontend/debug.h"

#include <stdarg.h>

#include "app.h"
#include "frontend/ui.h"
#include "imgui.h"

#define NW_DEBUG_ITEMS 64

typedef struct NwDebugItem {
    const char *menu, *label;
    NwDebugFn fn;
    void *user;
    bool open;
} NwDebugItem;

static struct {
    bool init;
    bool shown;
    NwDebugItem items[NW_DEBUG_ITEMS];
    uint32_t nitems;
} nw_debug;

static NwDebugItem *nw_debug_add(const char *menu, const char *label)
{
    SDL_assert(nw_debug.nitems < NW_DEBUG_ITEMS);
    if (nw_debug.nitems == NW_DEBUG_ITEMS) {
        return NULL;
    }
    NwDebugItem *item = &nw_debug.items[nw_debug.nitems++];
    *item = NwDebugItem{ menu, label, NULL, NULL, false };
    return item;
}

void nw_debug_panel(const char *menu, const char *title, NwDebugFn draw, void *user, bool open)
{
    NwDebugItem *item = nw_debug_add(menu, title);
    if (item) {
        item->fn = draw;
        item->user = user;
        item->open = open;
    }
}

void nw_debug_text(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ImGui::TextV(fmt, ap);
    va_end(ap);
}

static void nw_debug_menus(void)
{
    for (uint32_t i = 0; i < nw_debug.nitems; i++) {
        const char *menu = nw_debug.items[i].menu;
        bool first = true;
        for (uint32_t j = 0; j < i && first; j++) {
            first = SDL_strcmp(nw_debug.items[j].menu, menu) != 0;
        }
        if (!first || !ImGui::BeginMenu(menu)) {
            continue;
        }
        for (uint32_t j = i; j < nw_debug.nitems; j++) {
            NwDebugItem *item = &nw_debug.items[j];
            if (SDL_strcmp(item->menu, menu) == 0) {
                ImGui::MenuItem(item->label, NULL, &item->open);
            }
        }
        ImGui::EndMenu();
    }
}

static void nw_debug_panels(void)
{
    float step = ImGui::GetFrameHeight();
    for (uint32_t i = 0; i < nw_debug.nitems; i++) {
        NwDebugItem *item = &nw_debug.items[i];
        if (!item->open) {
            continue;
        }
        char name[128];
        SDL_snprintf(name, sizeof(name), "%s###%s/%s", item->label, item->menu, item->label);
        ImGui::SetNextWindowPos(ImVec2(step * (1 + 2 * i), step * (2 + 2 * i)), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(name, &item->open, ImGuiWindowFlags_AlwaysAutoResize)) {
            item->fn(item->user);
        }
        ImGui::End();
    }
}

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
    nw_debug.nitems = 0;
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().FramePadding);
    if (ImGui::BeginMainMenuBar()) {
        char rate[32];
        SDL_snprintf(rate, sizeof(rate), "%.0f fps", ImGui::GetIO().Framerate);
        ImGui::TextDisabled(NW_APP_NAME);
        nw_debug_menus();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(rate).x);
        ImGui::TextUnformatted(rate);
        ImGui::EndMainMenuBar();
    }
    nw_debug_panels();
    ImGui::PopStyleVar();
    nw_ui_end();
}
