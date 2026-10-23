#include "frontend/ui.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#define NW_UI_FONT_SIZE 20.0f

/* build/gen/fonts.c, from assets/fonts/ (cmake/embed.cmake). */
extern "C" const unsigned char nw_font_regular[], nw_font_bold[];
extern "C" const size_t nw_font_regular_size, nw_font_bold_size;

static SDL_Window *nw_ui_window;
static ImFont *nw_ui_fonts[2];
static float nw_ui_scale;

static ImFont *nw_ui_add_font(const unsigned char *data, size_t size, const char *name)
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; /* static data */
    SDL_strlcpy(cfg.Name, name, sizeof(cfg.Name));
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void *)data, (int)size, NW_UI_FONT_SIZE, &cfg);
}

static ImVec4 nw_ui_rgb(unsigned rgb, float a = 1.0f)
{
    return ImVec4((float)(rgb >> 16 & 0xff) / 255.0f, (float)(rgb >> 8 & 0xff) / 255.0f, (float)(rgb & 0xff) / 255.0f, a);
}

/* Dark blue panels with an orange accent, sized for scale (the display's content scale). */
static void nw_ui_style(float scale)
{
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(28.0f, 24.0f);
    style.FramePadding = ImVec2(14.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.WindowRounding = 10.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.SeparatorTextBorderSize = 2.0f;

    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text] = nw_ui_rgb(0xf2f4f8);
    c[ImGuiCol_TextDisabled] = nw_ui_rgb(0x8d97ad);
    c[ImGuiCol_WindowBg] = nw_ui_rgb(0x141c30);
    c[ImGuiCol_ChildBg] = nw_ui_rgb(0x0d1324);
    c[ImGuiCol_PopupBg] = nw_ui_rgb(0x141c30);
    c[ImGuiCol_MenuBarBg] = nw_ui_rgb(0x141c30);
    c[ImGuiCol_Border] = nw_ui_rgb(0x2c3858);
    c[ImGuiCol_FrameBg] = nw_ui_rgb(0x0d1324);
    c[ImGuiCol_FrameBgHovered] = nw_ui_rgb(0x1d2742);
    c[ImGuiCol_FrameBgActive] = nw_ui_rgb(0x243052);
    c[ImGuiCol_Button] = nw_ui_rgb(0xe8751a);
    c[ImGuiCol_ButtonHovered] = nw_ui_rgb(0xf58d38);
    c[ImGuiCol_ButtonActive] = nw_ui_rgb(0xc95f0e);
    c[ImGuiCol_Header] = nw_ui_rgb(0x243052);
    c[ImGuiCol_HeaderHovered] = nw_ui_rgb(0x2e3c66);
    c[ImGuiCol_HeaderActive] = nw_ui_rgb(0x36477a);
    c[ImGuiCol_Separator] = nw_ui_rgb(0x2c3858);
    c[ImGuiCol_PlotHistogram] = nw_ui_rgb(0xe8751a);
    c[ImGuiCol_ScrollbarBg] = nw_ui_rgb(0x0d1324);
    c[ImGuiCol_NavCursor] = nw_ui_rgb(0xffd23f);
    c[ImGuiCol_TextLink] = nw_ui_rgb(0xffb35c);

    style.FontSizeBase = NW_UI_FONT_SIZE;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ImGui::GetStyle() = style;
    nw_ui_scale = scale;
}

static float nw_ui_content_scale(void)
{
    float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(nw_ui_window));
    return scale > 0.0f ? scale : 1.0f;
}

bool nw_ui_init(NwPlatform *p)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    nw_ui_window = p->window;
    nw_ui_fonts[NW_UI_REGULAR] = nw_ui_add_font(nw_font_regular, nw_font_regular_size, "Atkinson Hyperlegible");
    nw_ui_fonts[NW_UI_BOLD] = nw_ui_add_font(nw_font_bold, nw_font_bold_size, "Atkinson Hyperlegible Bold");
    nw_ui_style(nw_ui_content_scale());

    if (!nw_ui_fonts[NW_UI_REGULAR] || !nw_ui_fonts[NW_UI_BOLD] || !ImGui_ImplSDL3_InitForOpenGL(p->window, p->gl)) {
        ImGui::DestroyContext();
        return SDL_SetError("Dear ImGui could not start");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return SDL_SetError("Dear ImGui could not start OpenGL");
    }
    return true;
}

void nw_ui_shutdown(void)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    nw_ui_window = NULL;
}

void nw_ui_event(const SDL_Event *e)
{
    ImGui_ImplSDL3_ProcessEvent(e);
    if (e->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED || e->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
        float scale = nw_ui_content_scale();
        if (scale != nw_ui_scale) {
            nw_ui_style(scale);
        }
    }
}

void nw_ui_begin(void)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void nw_ui_end(void)
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void nw_ui_push_font(NwUiFont font, float scale)
{
    ImGui::PushFont(nw_ui_fonts[font], ImGui::GetStyle().FontSizeBase * scale);
}

void nw_ui_pop_font(void)
{
    ImGui::PopFont();
}
