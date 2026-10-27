#include "frontend/debug.h"

#include <float.h>
#include <stdarg.h>

#include "app.h"
#include "frontend/ui.h"
#include "imgui.h"

extern "C" {
#include "platform/paths.h"
}

#define NW_DEBUG_ITEMS 64

typedef enum NwDebugKind {
    NW_DEBUG_PANEL,
    NW_DEBUG_TOGGLE,
    NW_DEBUG_ACTION,
} NwDebugKind;

typedef struct NwDebugItem {
    NwDebugKind kind;
    const char *menu, *label;
    NwDebugFn fn;
    void *user;
    bool open;
    bool *value;
    SDL_Scancode key;
} NwDebugItem;

static struct {
    bool init;
    bool shown;
    char *root;
    NwDebugItem items[NW_DEBUG_ITEMS];
    uint32_t nitems;
} nw_debug;

static NwDebugItem *nw_debug_add(NwDebugKind kind, const char *menu, const char *label)
{
    SDL_assert(nw_debug.nitems < NW_DEBUG_ITEMS);
    if (nw_debug.nitems == NW_DEBUG_ITEMS) {
        return NULL;
    }
    NwDebugItem *item = &nw_debug.items[nw_debug.nitems++];
    *item = NwDebugItem{ kind, menu, label, NULL, NULL, false, NULL, SDL_SCANCODE_UNKNOWN };
    return item;
}

void nw_debug_panel(const char *menu, const char *title, NwDebugFn draw, void *user, bool open)
{
    NwDebugItem *item = nw_debug_add(NW_DEBUG_PANEL, menu, title);
    if (item) {
        item->fn = draw;
        item->user = user;
        item->open = open;
    }
}

void nw_debug_toggle(const char *menu, const char *label, bool *value)
{
    NwDebugItem *item = nw_debug_add(NW_DEBUG_TOGGLE, menu, label);
    if (item) {
        item->value = value;
    }
}

void nw_debug_action(const char *menu, const char *label, SDL_Scancode key, NwDebugFn run, void *user)
{
    NwDebugItem *item = nw_debug_add(NW_DEBUG_ACTION, menu, label);
    if (item) {
        item->fn = run;
        item->user = user;
        item->key = key;
    }
}

char *nw_debug_file(const char *folder, const char *ext)
{
    char *dir = nw_debug.root ? nw_paths_dir(nw_debug.root, folder, true) : NULL;
    char *path = NULL;
    SDL_Time now;
    SDL_DateTime t;
    if (dir && SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true)) {
        SDL_asprintf(&path, "%s%04d%02d%02d-%02d%02d%02d-%03d.%s", dir, t.year, t.month, t.day, t.hour, t.minute,
                     t.second, t.nanosecond / 1000000, ext);
    }
    SDL_free(dir);
    return path;
}

void nw_debug_text(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ImGui::TextV(fmt, ap);
    va_end(ap);
}

bool nw_debug_button(const char *label)
{
    return ImGui::Button(label);
}

bool nw_debug_slider(const char *label, float *value, float lo, float hi, bool log)
{
    return ImGui::SliderFloat(label, value, lo, hi, "%.3g", log ? ImGuiSliderFlags_Logarithmic : 0);
}

bool nw_debug_slider_int(const char *label, int *value, int lo, int hi)
{
    return ImGui::SliderInt(label, value, lo, hi);
}

bool nw_debug_filter(const char *hint, char *buf, size_t size)
{
    ImGui::PushID(hint);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputTextWithHint("##filter", hint, buf, size);
    ImGui::PopID();
    return changed;
}

void nw_debug_list_begin(const char *id, int rows)
{
    float height = ImGui::GetTextLineHeightWithSpacing() * (float)rows + ImGui::GetStyle().WindowPadding.y * 2;
    ImGui::BeginChild(id, ImVec2(ImGui::GetFontSize() * 20, height), ImGuiChildFlags_Borders);
}

void nw_debug_list_end(void)
{
    ImGui::EndChild();
}

bool nw_debug_selectable(const char *label, bool selected)
{
    return ImGui::Selectable(label, selected);
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
            if (SDL_strcmp(item->menu, menu) != 0) {
                continue;
            }
            if (item->kind != NW_DEBUG_ACTION) {
                ImGui::MenuItem(item->label, NULL, item->kind == NW_DEBUG_TOGGLE ? item->value : &item->open);
            } else if (ImGui::MenuItem(item->label, item->key ? SDL_GetScancodeName(item->key) : NULL)) {
                item->fn(item->user);
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
        if (item->kind != NW_DEBUG_PANEL || !item->open) {
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

bool nw_debug_init(NwPlatform *p, const char *data_root)
{
    if (!nw_ui_init(p)) {
        return false;
    }
    nw_debug.root = SDL_strdup(data_root);
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
    SDL_free(nw_debug.root);
    nw_debug.root = NULL;
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
    for (uint32_t i = 0; e->type == SDL_EVENT_KEY_DOWN && i < nw_debug.nitems; i++) {
        NwDebugItem *item = &nw_debug.items[i];
        if (item->kind == NW_DEBUG_ACTION && item->key && item->key == e->key.scancode) {
            if (!e->key.repeat) {
                item->fn(item->user);
            }
            return true;
        }
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

void nw_debug_show(bool shown)
{
    nw_debug.shown = shown;
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
