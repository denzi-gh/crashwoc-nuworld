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
    bool placed; /* its window has a position */
    bool focus;  /* chosen in the menu: brought to the front */
    bool *value;
    SDL_Scancode key;
} NwDebugItem;

static struct {
    bool init;
    bool shown;
    char *root;
    NwDebugItem items[NW_DEBUG_ITEMS];
    uint32_t nitems;
    uint32_t placed; /* panels opened so far, the next one's cascade slot */
} nw_debug;

static NwDebugItem *nw_debug_add(NwDebugKind kind, const char *menu, const char *label)
{
    SDL_assert(nw_debug.nitems < NW_DEBUG_ITEMS);
    if (nw_debug.nitems == NW_DEBUG_ITEMS) {
        return NULL;
    }
    NwDebugItem *item = &nw_debug.items[nw_debug.nitems++];
    *item = NwDebugItem{ kind, menu, label, NULL, NULL, false, false, false, NULL, SDL_SCANCODE_UNKNOWN };
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

void nw_debug_panel_key(const char *menu, const char *title, SDL_Scancode key, NwDebugFn draw, void *user)
{
    NwDebugItem *item = nw_debug_add(NW_DEBUG_PANEL, menu, title);
    if (item) {
        item->fn = draw;
        item->user = user;
        item->key = key;
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
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    ImGui::TextV(fmt, ap);
    ImGui::PopTextWrapPos();
    va_end(ap);
}

void nw_debug_tooltip(const char *text)
{
    ImGui::SetItemTooltip("%s", text);
}

void nw_debug_label(const char *text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
}

bool nw_debug_button(const char *label)
{
    return ImGui::Button(label);
}

bool nw_debug_checkbox(const char *label, bool *value)
{
    return ImGui::Checkbox(label, value);
}

void nw_debug_indent(bool in)
{
    if (in) {
        ImGui::Indent();
    } else {
        ImGui::Unindent();
    }
}

void nw_debug_same_line(float column)
{
    ImGui::SameLine(column * ImGui::GetFontSize());
}

bool nw_debug_slider(const char *label, float *value, float lo, float hi, bool log)
{
    return ImGui::SliderFloat(label, value, lo, hi, "%.3g", log ? ImGuiSliderFlags_Logarithmic : 0);
}

bool nw_debug_slider_int(const char *label, int *value, int lo, int hi)
{
    return ImGui::SliderInt(label, value, lo, hi, "%d", ImGuiSliderFlags_AlwaysClamp);
}

bool nw_debug_combo(const char *label, int *index, const char *const *items, int count)
{
    return ImGui::Combo(label, index, items, count);
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

/* menu is the path's first len characters or a submenu of it. */
static bool nw_debug_under(const char *menu, const char *path, size_t len)
{
    return len == 0 || (SDL_strncmp(menu, path, len) == 0 && (menu[len] == '\0' || menu[len] == '/'));
}

/* A toggle is an outlined checkbox, a panel ends in "..." and comes to the front when chosen. */
static void nw_debug_menu_item(NwDebugItem *item)
{
    if (item->kind == NW_DEBUG_TOGGLE) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Checkbox(item->label, item->value);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    } else if (item->kind == NW_DEBUG_PANEL) {
        char label[80];
        SDL_snprintf(label, sizeof(label), "%s...", item->label);
        if (ImGui::MenuItem(label, item->key ? SDL_GetScancodeName(item->key) : NULL)) {
            item->open = item->focus = true;
        }
    } else if (ImGui::MenuItem(item->label, item->key ? SDL_GetScancodeName(item->key) : NULL)) {
        item->fn(item->user);
    }
}

/* The items and submenus under path's first len characters, each where its first item was registered; menu names are
   bold. */
static void nw_debug_menu(const char *path, size_t len)
{
    for (uint32_t i = 0; i < nw_debug.nitems; i++) {
        NwDebugItem *item = &nw_debug.items[i];
        if (!nw_debug_under(item->menu, path, len)) {
            continue;
        }
        if (len && item->menu[len] == '\0') {
            nw_debug_menu_item(item);
            continue;
        }
        const char *name = item->menu + (len ? len + 1 : 0);
        const char *end = SDL_strchr(name, '/');
        size_t n = end ? (size_t)(end - name) : SDL_strlen(name), sub = (size_t)(name - item->menu) + n;
        bool first = true;
        for (uint32_t j = 0; j < i && first; j++) {
            first = !nw_debug_under(nw_debug.items[j].menu, item->menu, sub);
        }
        char label[64];
        SDL_strlcpy(label, name, SDL_min(n + 1, sizeof(label)));
        if (!first) {
            continue;
        }
        nw_ui_push_font(NW_UI_BOLD, 1.0f);
        bool open = ImGui::BeginMenu(label);
        nw_ui_pop_font();
        if (open) {
            nw_debug_menu(item->menu, sub);
            ImGui::EndMenu();
        }
    }
}

/* A panel first opens on a cascade from the top left, in the order panels open; every frame it is kept inside the
   work area (the screen below the menu bar), no larger than it, scrolling what does not fit. */
static void nw_debug_panels(void)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float margin = ImGui::GetStyle().WindowPadding.x, step = ImGui::GetFrameHeight() * 1.5f;
    ImVec2 lo = vp->WorkPos, hi(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
    ImVec2 room(SDL_max(vp->WorkSize.x - 2 * margin, step), SDL_max(vp->WorkSize.y - 2 * margin, step));
    uint32_t rows = SDL_max((uint32_t)(room.y / 3 / step), 1u);
    for (uint32_t i = 0; i < nw_debug.nitems; i++) {
        NwDebugItem *item = &nw_debug.items[i];
        if (item->kind != NW_DEBUG_PANEL || !item->open) {
            continue;
        }
        char name[128];
        SDL_snprintf(name, sizeof(name), "%s###%s/%s", item->label, item->menu, item->label);
        if (!item->placed) {
            uint32_t k = nw_debug.placed++;
            float x = step * (float)(k % rows + k / rows * 4), y = step * (float)(k % rows);
            ImGui::SetNextWindowPos(ImVec2(lo.x + margin + x, lo.y + margin + y));
            item->placed = true;
        }
        if (item->focus) {
            ImGui::SetNextWindowFocus();
            item->focus = false;
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), room);
        bool shown = ImGui::Begin(name, &item->open, ImGuiWindowFlags_AlwaysAutoResize);
        ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImVec2 fit(SDL_max(SDL_min(pos.x, hi.x - size.x), lo.x), SDL_max(SDL_min(pos.y, hi.y - size.y), lo.y));
        if (fit.x != pos.x || fit.y != pos.y) {
            ImGui::SetWindowPos(fit);
        }
        if (shown) {
            ImGui::PushItemWidth(ImGui::GetFontSize() * 12.0f);
            item->fn(item->user);
            ImGui::PopItemWidth();
        }
        ImGui::End();
    }
}

static void nw_debug_set_shown(bool shown)
{
    nw_debug.shown = shown;
    ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse();
}

/* Closing hides the overlay too when no other panel is open. */
static void nw_debug_key_panel(NwDebugItem *panel)
{
    panel->open = !nw_debug.shown || !panel->open;
    bool other = false;
    for (uint32_t i = 0; i < nw_debug.nitems; i++) {
        other = other || (nw_debug.items[i].kind == NW_DEBUG_PANEL && nw_debug.items[i].open);
    }
    if (panel->open != nw_debug.shown && (panel->open || !other)) {
        nw_debug_set_shown(panel->open);
    }
}

bool nw_debug_init(NwPlatform *p, const char *data_root)
{
    if (!nw_ui_init(p, true)) {
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
    nw_debug.placed = 0;
}

bool nw_debug_event(const SDL_Event *e)
{
    if (!nw_debug.init) {
        return false;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F1) {
        if (!e->key.repeat) {
            nw_debug_set_shown(!nw_debug.shown);
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
        if (item->kind == NW_DEBUG_PANEL && item->key && item->key == e->key.scancode) {
            if (!e->key.repeat) {
                nw_debug_key_panel(item);
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
    if (ImGui::BeginMainMenuBar()) {
        char rate[32];
        SDL_snprintf(rate, sizeof(rate), "%.0f fps", ImGui::GetIO().Framerate);
        ImGui::TextDisabled(NW_APP_NAME);
        nw_debug_menu("", 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(rate).x);
        ImGui::TextUnformatted(rate);
        ImGui::EndMainMenuBar();
    }
    nw_debug_panels();
    nw_ui_end();
}
