#include "frontend/wizard.h"

#include <float.h>

#include "imgui.h"

#include "app.h"
#include "frontend/ui.h"

extern "C" {
#include "frontend/import.h"
#include "platform/paths.h"
#include "vfs/extract.h"
}

#define NW_WIZARD_RESCAN_MS 1000

typedef enum NwWizardPage {
    NW_PAGE_PICK,
    NW_PAGE_RUN,
    NW_PAGE_DONE,
    NW_PAGE_FAILED,
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
    static const char *const both[] = { "Close", "Quit" }, *const quit[] = { "Quit" };
    nw_wizard_align_right(w->had_disc ? both : quit, w->had_disc ? 2 : 1);
    if (w->had_disc) {
        if (ImGui::Button("Close")) {
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

static NwWizardResult nw_wizard_done(NwWizard *w)
{
    nw_wizard_title("Disc imported");
    nw_wizard_wrapped(nw_wizard_ok, "Every file matches PAL v" NW_DISC_VER);
    ImGui::TextWrapped("The game files are in %s" NW_DISC_DIR ". You will not need the disc image again, you can delete it if you want!",
                       w->root);
    ImGui::Spacing();
    static const char *const next[] = { "Continue" };
    nw_wizard_align_right(next, 1);
    return ImGui::Button("Continue") ? NW_WIZARD_CLOSED : NW_WIZARD_OPEN;
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
    float margin = 16.0f * scale;
    float width = SDL_max(SDL_min(vp->WorkSize.x - 2.0f * margin, 760.0f * scale), 240.0f);
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
        result = nw_wizard_done(w);
        break;
    case NW_PAGE_FAILED:
        nw_wizard_failed(w);
        break;
    }
    ImGui::End();
    return result;
}
