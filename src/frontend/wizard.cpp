#include "frontend/wizard.h"

#include <float.h>

#include "imgui.h"

#include "app.h"
#include "frontend/ui.h"

extern "C" {
#include "frontend/import.h"
#include "gfx/gl.h"
#include "platform/paths.h"
#include "vfs/extract.h"

extern const unsigned char nw_beta_menu_png[];
extern const size_t nw_beta_menu_png_size;
}

#define NW_WIZARD_RESCAN_MS 1000
#define NW_WIZARD_MARGIN 16.0f /* around the window, unscaled */
#define NW_WIZARD_FEEDBACK_URL "https://github.com/denzi-gh/crashwoc-nuworld/issues/new?template=0-beta_feedback.yml"
#define NW_WIZARD_DISCORD_URL "https://discord.gg/kmCPpW4KvJ"

typedef enum NwWizardPage {
    NW_PAGE_PICK,
    NW_PAGE_RUN,
    NW_PAGE_DONE,
    NW_PAGE_FAILED,
    NW_PAGE_WELCOME,
} NwWizardPage;

struct NwWizard {
    SDL_Window *window;
    char *root;
    char *import_dir;
    Uint32 dialog_event; /* file dialog results, pushed from the dialog's thread */
    NwWizardPage page;
    NwImport *job;
    char *image;     /* the image being or last imported */
    bool had_disc;   /* a disc was imported before the wizard opened */
    NwImageList list; /* import folder */
    Uint64 scanned;  /* SDL_GetTicks of the last scan, 0 = never */
    char notice[512];
    char error[512];
    GLuint shot; /* the welcome page's debug menu screenshot, 0 = not loaded */
    int shot_w, shot_h;
    bool zoom; /* the screenshot fills the window */
};

static const SDL_DialogFileFilter nw_wizard_filters[] = {
    { "Disc images (.iso, .cue, .bin)", "iso;cue;bin" },
    { "All files", "*" },
};

static const ImVec4 nw_wizard_ok(0.55f, 0.85f, 0.45f, 1.0f);
static const ImVec4 nw_wizard_warn(1.0f, 0.82f, 0.25f, 1.0f);
static const ImVec4 nw_wizard_bad(1.0f, 0.45f, 0.40f, 1.0f);

static const char *nw_wizard_basename(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

static void nw_wizard_start(NwWizard *w, const char *image)
{
    char *copy = SDL_strdup(image);
    SDL_free(w->image);
    w->image = copy;
    w->notice[0] = '\0';
    w->job = copy ? nw_import_start(copy, w->root, &nw_disc_manifest) : NULL;
    if (w->job) {
        w->page = NW_PAGE_RUN;
    } else {
        SDL_strlcpy(w->error, SDL_GetError(), sizeof(w->error));
        w->page = NW_PAGE_FAILED;
    }
}

/* Runs on the dialog's thread: hands the result to the main thread as a user event. */
static void SDLCALL nw_wizard_picked(void *user, const char *const *files, int filter)
{
    SDL_Event e;
    (void)filter;
    if (files && !files[0]) {
        return; /* cancelled */
    }
    SDL_zero(e);
    e.type = (Uint32)(uintptr_t)user;
    e.user.data1 = files ? SDL_strdup(files[0]) : NULL;
    e.user.data2 = files ? NULL : SDL_strdup(SDL_GetError());
    if (!SDL_PushEvent(&e)) {
        SDL_free(e.user.data1);
        SDL_free(e.user.data2);
    }
}

/* Opens dir in the file manager through a file:// URL. */
static void nw_wizard_open_folder(const char *dir)
{
    size_t n = SDL_strlen(dir);
    char *url = (char *)SDL_malloc(n * 3 + 9), *out = url;
    if (!url) {
        return;
    }
    out += SDL_snprintf(out, 9, "file://%s", dir[0] == '/' ? "" : "/");
    for (const unsigned char *p = (const unsigned char *)dir; *p; p++) {
        if (*p == '\\') {
            *out++ = '/';
        } else if (SDL_isalnum(*p) || SDL_strchr("/:-_.~", *p)) {
            *out++ = (char)*p;
        } else {
            out += SDL_snprintf(out, 4, "%%%02X", *p);
        }
    }
    *out = '\0';
    if (!SDL_OpenURL(url)) {
        SDL_Log("cannot open %s: %s", url, SDL_GetError());
    }
    SDL_free(url);
}

NwWizard *nw_wizard_create(SDL_Window *window, const char *root)
{
    NwWizard *w = (NwWizard *)SDL_calloc(1, sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->window = window;
    w->root = SDL_strdup(root);
    w->import_dir = nw_paths_dir(root, NW_PATHS_IMPORT, false);
    w->dialog_event = SDL_RegisterEvents(1);
    w->had_disc = nw_extract_has_disc(root);
    if (!w->root || !w->import_dir) {
        nw_wizard_destroy(w);
        return NULL;
    }
    return w;
}

void nw_wizard_destroy(NwWizard *w)
{
    if (!w) {
        return;
    }
    nw_import_free(w->job);
    nw_import_list_free(&w->list);
    if (w->shot) {
        glDeleteTextures(1, &w->shot);
    }
    SDL_free(w->image);
    SDL_free(w->import_dir);
    SDL_free(w->root);
    SDL_free(w);
}

void nw_wizard_event(NwWizard *w, const SDL_Event *e)
{
    if (e->type == SDL_EVENT_DROP_FILE && e->drop.data) {
        if (w->page != NW_PAGE_RUN) {
            nw_wizard_start(w, e->drop.data);
        }
    } else if (w->dialog_event && e->type == w->dialog_event) {
        char *path = (char *)e->user.data1, *why = (char *)e->user.data2;
        if (path && w->page == NW_PAGE_PICK) {
            nw_wizard_start(w, path);
        } else if (why) {
            SDL_snprintf(w->notice, sizeof(w->notice),
                         "No file dialog available (%s). Drop the image here or use the import folder.",
                         why);
        }
        SDL_free(path);
        SDL_free(why);
    }
}

static void nw_wizard_heading(const char *text)
{
    nw_ui_push_font(NW_UI_BOLD, 1.1f);
    ImGui::SeparatorText(text);
    nw_ui_pop_font();
}

static void nw_wizard_title(const char *text)
{
    nw_ui_push_font(NW_UI_BOLD, 1.35f);
    ImGui::TextUnformatted(text);
    nw_ui_pop_font();
}

static void nw_wizard_wrapped(const ImVec4 &color, const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/* Starts the next button so that the buttons labelled in labels end flush right. */
static void nw_wizard_align_right(const char *const *labels, int count)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    float width = 0.0f;
    for (int i = 0; i < count; i++) {
        width += ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2.0f + (i ? style.ItemSpacing.x : 0.0f);
    }
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
    }
}

static NwWizardResult nw_wizard_pick(NwWizard *w)
{
    NwWizardResult result = NW_WIZARD_OPEN;
    Uint64 now = SDL_GetTicks();
    if (!w->scanned || now - w->scanned >= NW_WIZARD_RESCAN_MS) {
        nw_import_list_free(&w->list);
        if (!nw_import_scan(w->import_dir, &w->list)) {
            SDL_Log("cannot list %s: %s", w->import_dir, SDL_GetError());
        }
        w->scanned = now ? now : 1;
    }

    nw_wizard_title("Welcome");
    ImGui::TextWrapped("NuWorld needs a disc image of your own copy of the game:");
    ImGui::BulletText("Crash Bandicoot: The Wrath of Cortex (PS2, PAL v1.03, SLES-50386)");
    ImGui::BulletText(".iso, or .bin with its .cue");

    if (w->had_disc) {
        nw_wizard_wrapped(nw_wizard_ok, "A disc is already imported. It is only replaced if the new import works.");
    }
    if (w->notice[0]) {
        nw_wizard_wrapped(nw_wizard_warn, w->notice);
    }

    nw_wizard_heading("Disc image");
    ImGui::BeginDisabled(!w->dialog_event);
    if (ImGui::Button("Choose file...")) {
        SDL_ShowOpenFileDialog(nw_wizard_picked, (void *)(uintptr_t)w->dialog_event, w->window, nw_wizard_filters,
                               (int)SDL_arraysize(nw_wizard_filters), NULL, false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("or drop it here");

    nw_wizard_heading("Import folder");
    ImGui::TextWrapped("Or copy it into this folder and pick it below (Steam Deck: use Desktop Mode).");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", w->import_dir);
    ImGui::PopStyleColor();
    if (ImGui::Button("Open folder")) {
        nw_wizard_open_folder(w->import_dir);
    }

    const ImGuiTableFlags table = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("images", 2, table)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed);
        for (size_t i = 0; i < w->list.count; i++) {
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("%s", w->list.names[i]);
            ImGui::TableNextColumn();
            if (ImGui::Button("Import")) {
                char *path = NULL;
                if (SDL_asprintf(&path, "%s%s", w->import_dir, w->list.names[i]) >= 0) {
                    nw_wizard_start(w, path);
                    SDL_free(path);
                }
            }
            ImGui::PopID();
        }
        if (!w->list.count) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("No disc images found.");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Data folder: %s", w->root);
    ImGui::PopStyleColor();
    static const char *const both[] = { "Start game", "Quit" }, *const quit[] = { "Quit" };
    nw_wizard_align_right(w->had_disc ? both : quit, w->had_disc ? 2 : 1);
    if (w->had_disc) {
        if (ImGui::Button("Start game")) {
            result = NW_WIZARD_CLOSED;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Quit")) {
        result = NW_WIZARD_QUIT;
    }
    return result;
}

static void nw_wizard_run(NwWizard *w)
{
    uint64_t done, total;
    NwImportState state = nw_import_poll(w->job, &done, &total);
    if (state != NW_IMPORT_RUNNING) {
        if (state == NW_IMPORT_FAILED) {
            SDL_strlcpy(w->error, nw_import_error(w->job), sizeof(w->error));
        }
        nw_import_free(w->job);
        w->job = NULL;
        w->page = state == NW_IMPORT_DONE ? NW_PAGE_DONE : state == NW_IMPORT_FAILED ? NW_PAGE_FAILED : NW_PAGE_PICK;
        if (state == NW_IMPORT_CANCELLED) {
            SDL_strlcpy(w->notice, "Import cancelled. Nothing was changed.", sizeof(w->notice));
        }
        return;
    }

    nw_wizard_title("Importing");
    ImGui::TextWrapped("%s", nw_wizard_basename(w->image));
    ImGui::Spacing();
    if (!total) {
        ImGui::TextUnformatted("Checking the disc...");
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-FLT_MIN, 0.0f), "");
    } else {
        char overlay[32];
        float fraction = (float)((double)done / (double)total);
        SDL_snprintf(overlay, sizeof(overlay), "%d%%", (int)(done * 100 / total));
        ImGui::TextUnformatted("Copying and checking files...");
        ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), overlay);
        ImGui::TextDisabled("%llu of %llu MB", (unsigned long long)(done >> 20), (unsigned long long)(total >> 20));
    }
    ImGui::Spacing();
    static const char *const cancel[] = { "Cancel" };
    nw_wizard_align_right(cancel, 1);
    if (ImGui::Button("Cancel")) {
        nw_import_cancel(w->job);
    }
}

static void nw_wizard_done(NwWizard *w)
{
    nw_wizard_title("Disc imported");
    nw_wizard_wrapped(nw_wizard_ok, "Every file matches PAL v" NW_DISC_VER);
    ImGui::TextWrapped("The game files are in %s" NW_DISC_DIR ". You will not need the disc image again, you can delete it if you want!",
                       w->root);
    ImGui::Spacing();
    static const char *const next[] = { "Continue" };
    nw_wizard_align_right(next, 1);
    if (ImGui::Button("Continue")) {
        w->page = NW_PAGE_WELCOME;
    }
}

/* Uploads the embedded screenshot once; the page goes on without it if that fails. */
static void nw_wizard_load_shot(NwWizard *w)
{
    SDL_IOStream *io = SDL_IOFromConstMem(nw_beta_menu_png, nw_beta_menu_png_size);
    SDL_Surface *png = io ? SDL_LoadPNG_IO(io, true) : NULL;
    SDL_Surface *rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : NULL;
    SDL_DestroySurface(png);
    if (!rgba || rgba->pitch != rgba->w * 4) {
        SDL_Log("wizard: screenshot: %s", SDL_GetError());
        SDL_DestroySurface(rgba);
        return;
    }
    glGenTextures(1, &w->shot);
    glBindTexture(GL_TEXTURE_2D, w->shot);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    w->shot_w = rgba->w;
    w->shot_h = rgba->h;
    SDL_DestroySurface(rgba);
}

static NwWizardResult nw_wizard_welcome(NwWizard *w)
{
    nw_wizard_title("Welcome to the NuWorld BETA");
    ImGui::TextWrapped("Thanks for testing! This beta has one goal: fine-tuning N. Hanced Movement, NuWorld's reworked "
                       "movement for Crash, Coco and the vehicles.");

    nw_wizard_heading("Debug menu");
    ImGui::Bullet();
    ImGui::TextWrapped("Press F1 at any time to open the debug menu. Under Settings and Gameplay you also find video, "
                       "audio, controls, game speed and camera, so you can set NuWorld up the way you like.");
    ImGui::Bullet();
    ImGui::TextWrapped("The debug menu is only for the beta. The first release will have native menus.");
    ImGui::Bullet();
    ImGui::TextWrapped("Under N. Hanced Movement you can change the parameters of Crash, Coco and every vehicle. Each "
                       "one also has its own key, shown next to it in the menu.");
    ImGui::Bullet();
    ImGui::TextWrapped("Retail and N. Hanced at the top of a panel switch between the original movement and the "
                       "beta's defaults. Changes are saved automatically.");

    if (!w->shot && w->shot_w >= 0) {
        nw_wizard_load_shot(w);
        if (!w->shot) {
            w->shot_w = -1; /* failed, do not retry every frame */
        }
    }
    ImGui::Spacing();
    if (w->shot) {
        /* Left of the feedback: 60 % of the width at most, and the room left above the button row. */
        const ImGuiStyle &style = ImGui::GetStyle();
        float limit = ImGui::GetMainViewport()->WorkSize.y - 2.0f * NW_WIZARD_MARGIN * style.FontScaleDpi;
        float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 3.0f + style.WindowPadding.y;
        float most = SDL_max(limit - ImGui::GetCursorPosY() - footer, 1.0f);
        float width = ImGui::GetContentRegionAvail().x * 0.6f;
        float height = width * (float)w->shot_h / (float)w->shot_w;
        if (height > most) {
            width = most * (float)w->shot_w / (float)w->shot_h;
            height = most;
        }
        ImGui::Image((ImTextureID)w->shot, ImVec2(width, height));
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click to enlarge");
        }
        if (ImGui::IsItemClicked()) {
            w->zoom = true;
        }
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
    }

    ImGui::BeginGroup();
    nw_wizard_heading("Feedback");
    ImGui::TextWrapped("Something not working, or a parameter you would like to have? Create an issue on GitHub with "
                       "the Beta Feedback template, or start a thread on the Discord server.");
    if (ImGui::Button("Beta Feedback on GitHub")) {
        SDL_OpenURL(NW_WIZARD_FEEDBACK_URL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Discord")) {
        SDL_OpenURL(NW_WIZARD_DISCORD_URL);
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    static const char *const start[] = { "Start game" };
    nw_wizard_align_right(start, 1);
    return ImGui::Button("Start game") ? NW_WIZARD_CLOSED : NW_WIZARD_OPEN;
}

/* The screenshot over the whole window; a click or Escape closes it. */
static void nw_wizard_zoom(NwWizard *w)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float scale = SDL_min(vp->WorkSize.x / (float)w->shot_w, vp->WorkSize.y / (float)w->shot_h);
    ImVec2 size((float)w->shot_w * scale, (float)w->shot_h * scale);
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    ImGui::Begin("##zoom", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetCursorPos(ImVec2((vp->WorkSize.x - size.x) * 0.5f, (vp->WorkSize.y - size.y) * 0.5f));
    ImGui::Image((ImTextureID)w->shot, size);
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        w->zoom = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        w->zoom = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

static void nw_wizard_failed(NwWizard *w)
{
    nw_wizard_title("Import failed");
    ImGui::TextWrapped("%s", nw_wizard_basename(w->image ? w->image : ""));
    nw_wizard_wrapped(nw_wizard_bad, w->error);
    ImGui::TextWrapped("NuWorld runs only the PAL version 1.03 (SLES-50386). The data folder is unchanged.");
    ImGui::Spacing();
    static const char *const buttons[] = { "Try again", "Back" };
    nw_wizard_align_right(buttons, 2);
    if (ImGui::Button("Try again") && w->image) {
        nw_wizard_start(w, w->image); /* copies the path before freeing the old one */
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) {
        w->page = NW_PAGE_PICK;
    }
}

NwWizardResult nw_wizard_draw(NwWizard *w)
{
    NwWizardResult result = NW_WIZARD_OPEN;
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float scale = ImGui::GetStyle().FontScaleDpi;
    float margin = NW_WIZARD_MARGIN * scale;
    float wide = w->page == NW_PAGE_WELCOME ? 960.0f : 760.0f; /* room for the screenshot */
    float width = SDL_max(SDL_min(vp->WorkSize.x - 2.0f * margin, wide * scale), 240.0f);
    float height = SDL_max(vp->WorkSize.y - 2.0f * margin, 120.0f);

    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, height));
    ImGui::Begin(NW_APP_NAME, NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextDisabled(NW_APP_NAME " - first-time setup");
    switch (w->page) {
    case NW_PAGE_PICK:
        result = nw_wizard_pick(w);
        break;
    case NW_PAGE_RUN:
        nw_wizard_run(w);
        break;
    case NW_PAGE_DONE:
        nw_wizard_done(w);
        break;
    case NW_PAGE_FAILED:
        nw_wizard_failed(w);
        break;
    case NW_PAGE_WELCOME:
        result = nw_wizard_welcome(w);
        break;
    }
    ImGui::End();
    if (w->zoom && w->shot) {
        nw_wizard_zoom(w);
    }
    return result;
}
