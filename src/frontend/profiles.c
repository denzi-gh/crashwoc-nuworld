#include "frontend/profiles.h"

#include <stdint.h>

#include "frontend/debug.h"
#include "frontend/settings.h"
#include "platform/droptarget.h"
#include "platform/log.h"
#include "platform/paths.h"

#define NW_PROFILES_MAX 128
#define NW_PROFILES_FILE_MAX (256 * 1024)
#define NW_PROFILES_RESCAN_MS 2000

typedef enum NwProfilesAsk {
    NW_PROFILES_IMPORT,
    NW_PROFILES_APPLY,
    NW_PROFILES_REPLACE,
    NW_PROFILES_DELETE,
} NwProfilesAsk;

typedef enum NwProfilesDialog {
    NW_PROFILES_OPEN,
    NW_PROFILES_EXPORT,
    NW_PROFILES_DROP,
} NwProfilesDialog;

typedef struct NwProfileFile {
    char name[NW_PROFILE_NAME];
    char *path;
    unsigned parts;
    NwMovement movement;
} NwProfileFile;

static const char *const nw_profiles_parts[NW_MOVEMENT_PARTS] = {
    "Crash", "Coco", "Mech", "Submarine", "Jeep", "Atlasphere", "Firefly", "Plane", "Space Fighter",
};

static const char *const nw_profiles_kinds[] = { "Retail", "N. Hanced", "Custom" };

static const SDL_DialogFileFilter nw_profiles_filters[] = {
    { "Movement profiles (.toml)", "toml" },
    { "All files", "*" },
};

static struct {
    bool started;
    SDL_Window *window;
    char *root;
    NwMovement *movement;
    Uint32 dialog_event;
    char name[NW_PROFILE_NAME];
    NwProfileFile files[NW_PROFILES_MAX];
    int count;
    bool listed;
    uint64_t listed_at;
    NwProfilesAsk ask;
    char ask_name[NW_PROFILE_NAME];
    unsigned ask_parts;
    NwMovement ask_movement;
    char export_name[NW_PROFILE_NAME];
    char notice[512];
} nw_profiles;

static char *nw_profiles_dir(bool create)
{
    return nw_profiles.root ? nw_paths_dir(nw_profiles.root, NW_PATHS_PROFILES, create) : NULL;
}

static const char *nw_profiles_base(const char *path)
{
    const char *base = path;
    for (const char *c = path; *c; c++) {
        if (*c == '/' || *c == '\\') {
            base = c + 1;
        }
    }
    return base;
}

static void nw_profiles_stem(const char *path, char *name, size_t size)
{
    SDL_strlcpy(name, nw_profiles_base(path), size);
    char *dot = SDL_strrchr(name, '.');
    if (dot && dot != name) {
        *dot = '\0';
    }
    nw_settings_profile_name(name);
}

static unsigned nw_profiles_load(const char *path, NwMovement *m, char *name, size_t size)
{
    SDL_PathInfo info;
    size_t len = 0;
    char *text = NULL;
    name[0] = '\0';
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE && info.size <= NW_PROFILES_FILE_MAX) {
        text = SDL_LoadFile(path, &len);
    }
    *m = (NwMovement)NW_MOVEMENT_NHANCED;
    unsigned parts = text ? nw_settings_profile_read(text, len, m, name, size) : 0;
    SDL_free(text);
    if (parts && !name[0]) {
        nw_profiles_stem(path, name, size);
    }
    return parts;
}

static bool nw_profiles_toml(const char *path)
{
    size_t n = SDL_strlen(path);
    return n >= 5 && SDL_strcasecmp(path + n - 5, ".toml") == 0;
}

static bool nw_profiles_valid(const char *path)
{
    NwMovement m;
    char name[NW_PROFILE_NAME];
    return nw_profiles_toml(path) && nw_profiles_load(path, &m, name, sizeof(name)) != 0;
}

static int SDLCALL nw_profiles_order(const void *a, const void *b)
{
    return SDL_strcasecmp(((const NwProfileFile *)a)->name, ((const NwProfileFile *)b)->name);
}

static void nw_profiles_clear(void)
{
    for (int i = 0; i < nw_profiles.count; i++) {
        SDL_free(nw_profiles.files[i].path);
    }
    nw_profiles.count = 0;
}

static void nw_profiles_scan(void)
{
    nw_profiles_clear();
    nw_profiles.listed = true;
    nw_profiles.listed_at = SDL_GetTicks();
    char *dir = nw_profiles_dir(false);
    int n = 0;
    char **found = dir ? SDL_GlobDirectory(dir, "*.toml", SDL_GLOB_CASEINSENSITIVE, &n) : NULL;
    for (int i = 0; found && i < n && nw_profiles.count < NW_PROFILES_MAX; i++) {
        NwProfileFile *f = &nw_profiles.files[nw_profiles.count];
        char *path = NULL;
        if (SDL_asprintf(&path, "%s%s", dir, found[i]) < 0) {
            continue;
        }
        f->parts = nw_profiles_load(path, &f->movement, f->name, sizeof(f->name));
        if (f->parts && f->name[0]) {
            f->path = path;
            nw_profiles.count++;
        } else {
            SDL_free(path);
        }
    }
    SDL_free(found);
    SDL_free(dir);
    SDL_qsort(nw_profiles.files, (size_t)nw_profiles.count, sizeof(nw_profiles.files[0]), nw_profiles_order);
}

static const NwProfileFile *nw_profiles_find(const char *name)
{
    for (int i = 0; i < nw_profiles.count; i++) {
        if (SDL_strcasecmp(nw_profiles.files[i].name, name) == 0) {
            return &nw_profiles.files[i];
        }
    }
    return NULL;
}

static void nw_profiles_parts_text(unsigned parts, char *out, size_t size)
{
    out[0] = '\0';
    if (parts == NW_PARTS_ALL) {
        SDL_strlcpy(out, "every character and vehicle", size);
        return;
    }
    for (int p = 0; p < NW_MOVEMENT_PARTS; p++) {
        if (parts & (1u << p)) {
            if (out[0]) {
                SDL_strlcat(out, ", ", size);
            }
            SDL_strlcat(out, nw_profiles_parts[p], size);
        }
    }
}

static bool nw_profiles_write(const char *name, unsigned parts, const NwMovement *m)
{
    const NwProfileFile *had = nw_profiles_find(name);
    char *dir = nw_profiles_dir(true), *path = NULL, *text = NULL;
    size_t size = 0;
    if (had) {
        path = SDL_strdup(had->path);
    } else if (dir && SDL_asprintf(&path, "%s%s.toml", dir, name) < 0) {
        path = NULL;
    }
    text = path ? nw_settings_profile_text(m, parts, name, &size) : NULL;
    bool ok = text && SDL_SaveFile(path, text, size);
    if (!ok) {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Could not save %s: %s", name, SDL_GetError());
        nw_log_warn(NW_LOG_APP, "profiles: %s", nw_profiles.notice);
    }
    SDL_free(text);
    SDL_free(path);
    SDL_free(dir);
    nw_profiles_scan();
    return ok;
}

static void nw_profiles_delete(const char *name)
{
    const NwProfileFile *f = nw_profiles_find(name);
    if (f && SDL_RemovePath(f->path)) {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Deleted %s.", name);
    } else if (f) {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Could not delete %s: %s", name, SDL_GetError());
    }
    nw_profiles_scan();
}

static void nw_profiles_use(const NwMovement *m, unsigned parts, const char *name)
{
    nw_settings_profile_apply(nw_profiles.movement, m, parts);
    SDL_strlcpy(nw_profiles.name, name, sizeof(nw_profiles.name));
    SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Loaded %s.", name);
    nw_log_info(NW_LOG_APP, "profiles: loaded %s", name);
}

static bool nw_profiles_prompt(void *user)
{
    (void)user;
    const char *name = nw_profiles.ask_name;
    char parts[160];
    bool open = true;
    nw_profiles_parts_text(nw_profiles.ask_parts, parts, sizeof(parts));
    switch (nw_profiles.ask) {
    case NW_PROFILES_APPLY:
        nw_debug_text("Apply %s Movement Profile?", name);
        if (SDL_strcmp(parts, name) != 0) {
            nw_debug_text("For %s.", parts);
        }
        if (nw_debug_button("Apply")) {
            nw_settings_profile_apply(nw_profiles.movement, &nw_profiles.ask_movement, nw_profiles.ask_parts);
            SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Applied %s.", name);
            nw_log_info(NW_LOG_APP, "profiles: applied %s (%s)", name, parts);
            open = false;
        }
        break;
    case NW_PROFILES_IMPORT:
        nw_debug_text("Import %s Movement Profile?", name);
        if (SDL_strcmp(parts, name) != 0) {
            nw_debug_text("For %s.", parts);
        }
        if (nw_profiles_find(name)) {
            nw_debug_text("It replaces the saved %s.", name);
        }
        if (nw_debug_button("Import")) {
            if (nw_profiles_write(name, nw_profiles.ask_parts, &nw_profiles.ask_movement)) {
                SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Imported %s.", name);
            }
            open = false;
        }
        nw_debug_same_line(0.0f);
        if (nw_debug_button("Import and Load")) {
            nw_profiles_write(name, nw_profiles.ask_parts, &nw_profiles.ask_movement);
            nw_profiles_use(&nw_profiles.ask_movement, nw_profiles.ask_parts, name);
            open = false;
        }
        break;
    case NW_PROFILES_REPLACE:
        nw_debug_text("Replace the saved %s Movement Profile?", name);
        if (nw_debug_button("Replace")) {
            if (nw_profiles_write(name, NW_PARTS_ALL, nw_profiles.movement)) {
                SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Saved %s.", name);
            }
            open = false;
        }
        break;
    case NW_PROFILES_DELETE:
        nw_debug_text("Delete the %s Movement Profile?", name);
        if (nw_debug_button("Delete")) {
            nw_profiles_delete(name);
            open = false;
        }
        break;
    }
    nw_debug_same_line(0.0f);
    if (nw_debug_button("Cancel")) {
        open = false;
    }
    return open;
}

static void nw_profiles_ask_for(NwProfilesAsk ask, const char *name, unsigned parts, const NwMovement *m)
{
    SDL_strlcpy(nw_profiles.ask_name, name, sizeof(nw_profiles.ask_name));
    nw_profiles_scan();
    nw_profiles.ask = ask;
    nw_profiles.ask_parts = parts;
    if (m) {
        nw_profiles.ask_movement = *m;
    }
    nw_debug_prompt("Movement Profiles", nw_profiles_prompt, NULL);
}

static void nw_profiles_offer(const char *path, bool quiet)
{
    NwMovement m;
    char name[NW_PROFILE_NAME];
    unsigned parts = nw_profiles_load(path, &m, name, sizeof(name));
    if (!parts) {
        if (!quiet) {
            SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "%s is no movement profile.",
                         nw_profiles_base(path));
        }
        return;
    }
    nw_profiles_ask_for(NW_PROFILES_IMPORT, name[0] ? name : "Imported", parts, &m);
}

static void nw_profiles_paste(NwProfilesAsk ask)
{
    char *text = SDL_GetClipboardText();
    size_t len = text ? SDL_strlen(text) : 0;
    NwMovement m = NW_MOVEMENT_NHANCED;
    char name[NW_PROFILE_NAME];
    unsigned parts = 0;
    if (len && len <= NW_PROFILES_FILE_MAX && SDL_strstr(text, "[game.movement")) {
        parts = nw_settings_profile_read(text, len, &m, name, sizeof(name));
    }
    SDL_free(text);
    char *file = parts ? NULL : nw_droptarget_clipboard(nw_profiles.window, nw_profiles_valid);
    if (file) {
        parts = nw_profiles_load(file, &m, name, sizeof(name));
        SDL_free(file);
    }
    if (!parts) {
        if (ask == NW_PROFILES_IMPORT) {
            SDL_strlcpy(nw_profiles.notice, "The clipboard holds no movement profile.", sizeof(nw_profiles.notice));
        }
        return;
    }
    nw_profiles_ask_for(ask, name[0] ? name : "Pasted", parts, &m);
}

static bool nw_profiles_clip(const NwMovement *m, unsigned parts, const char *name)
{
    size_t size = 0;
    char *text = nw_settings_profile_text(m, parts, name, &size);
    bool ok = text && SDL_SetClipboardText(text);
    if (ok) {
        char what[160];
        nw_profiles_parts_text(parts, what, sizeof(what));
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Copied %s (%s) to the clipboard.", name, what);
    } else {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Could not copy %s: %s", name, SDL_GetError());
    }
    SDL_free(text);
    return ok;
}

bool nw_profiles_copy(unsigned parts)
{
    if (!nw_profiles.started) {
        return false;
    }
    char name[NW_PROFILE_NAME];
    SDL_strlcpy(name, nw_profiles.name, sizeof(name));
    nw_settings_profile_name(name);
    for (int p = 0; !name[0] && p < NW_MOVEMENT_PARTS; p++) {
        if (parts == 1u << p) {
            SDL_strlcpy(name, nw_profiles_parts[p], sizeof(name));
        }
    }
    if (!name[0]) {
        SDL_strlcpy(name, nw_profiles_kinds[nw_movement_profile(nw_profiles.movement)], sizeof(name));
    }
    return nw_profiles_clip(nw_profiles.movement, parts, name);
}

static void nw_profiles_export(const char *path)
{
    const NwProfileFile *f = nw_profiles_find(nw_profiles.export_name);
    size_t n = SDL_strlen(path), size = 0;
    char *full = NULL, *text = NULL;
    if (!f) {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "%s is no longer saved.",
                     nw_profiles.export_name);
        return;
    }
    if (n >= 5 && SDL_strcasecmp(path + n - 5, ".toml") == 0) {
        full = SDL_strdup(path);
    } else if (SDL_asprintf(&full, "%s.toml", path) < 0) {
        full = NULL;
    }
    text = full ? nw_settings_profile_text(&f->movement, f->parts, f->name, &size) : NULL;
    if (text && SDL_SaveFile(full, text, size)) {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Exported %s as %s.", f->name,
                     nw_profiles_base(full));
    } else {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Could not export %s: %s", f->name,
                     SDL_GetError());
    }
    SDL_free(text);
    SDL_free(full);
}

static void SDLCALL nw_profiles_picked(void *user, const char *const *files, int filter)
{
    (void)filter;
    SDL_Event e;
    if (!nw_profiles.dialog_event || (files && !files[0])) {
        return;
    }
    SDL_zero(e);
    e.type = nw_profiles.dialog_event;
    e.user.code = (Sint32)(intptr_t)user;
    if (files) {
        e.user.data1 = SDL_strdup(files[0]);
    } else {
        e.user.data2 = SDL_strdup(SDL_GetError());
    }
    if (!SDL_PushEvent(&e)) {
        SDL_free(e.user.data1);
        SDL_free(e.user.data2);
    }
}

static void nw_profiles_import_dialog(void)
{
    SDL_ShowOpenFileDialog(nw_profiles_picked, (void *)(intptr_t)NW_PROFILES_OPEN, nw_profiles.window,
                           nw_profiles_filters, (int)SDL_arraysize(nw_profiles_filters),
                           SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS), false);
}

static void nw_profiles_export_dialog(const char *name)
{
    const char *desktop = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
    char *where = NULL;
    SDL_strlcpy(nw_profiles.export_name, name, sizeof(nw_profiles.export_name));
    if (desktop && SDL_asprintf(&where, "%s%s.toml", desktop, name) < 0) {
        where = NULL;
    }
    SDL_ShowSaveFileDialog(nw_profiles_picked, (void *)(intptr_t)NW_PROFILES_EXPORT, nw_profiles.window,
                           nw_profiles_filters, (int)SDL_arraysize(nw_profiles_filters), where);
    SDL_free(where);
}

static void nw_profiles_folder(void)
{
    char *dir = nw_profiles_dir(true);
    if (dir && SDL_OpenURL(dir)) {
        if (SDL_GetWindowFlags(nw_profiles.window) & SDL_WINDOW_FULLSCREEN) {
            SDL_MinimizeWindow(nw_profiles.window);
        }
    } else {
        SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Could not open the folder: %s",
                     SDL_GetError());
    }
    SDL_free(dir);
}

static void nw_profiles_draw(void *user)
{
    (void)user;
    if (!nw_profiles.listed || SDL_GetTicks() - nw_profiles.listed_at >= NW_PROFILES_RESCAN_MS) {
        nw_profiles_scan();
    }
    char name[NW_PROFILE_NAME], id[64];
    SDL_strlcpy(name, nw_profiles.name, sizeof(name));
    nw_settings_profile_name(name);
    nw_debug_input("Name", nw_profiles.name, sizeof(nw_profiles.name));
    nw_debug_same_line(0.0f);
    nw_debug_enable_begin(name[0] != '\0');
    if (nw_debug_button("Save")) {
        if (nw_profiles_find(name)) {
            nw_profiles_ask_for(NW_PROFILES_REPLACE, name, NW_PARTS_ALL, NULL);
        } else if (nw_profiles_write(name, NW_PARTS_ALL, nw_profiles.movement)) {
            SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "Saved %s.", name);
        }
    }
    nw_debug_enable_end();

    nw_debug_heading("Saved");
    if (!nw_profiles.count) {
        nw_debug_text("No saved profiles yet.");
    }
    for (int i = 0; i < nw_profiles.count; i++) {
        const NwProfileFile *f = &nw_profiles.files[i];
        SDL_snprintf(id, sizeof(id), "Load##load%d", i);
        if (nw_debug_button(id)) {
            nw_profiles_use(&f->movement, f->parts, f->name);
        }
        nw_debug_same_line(0.0f);
        SDL_snprintf(id, sizeof(id), "Copy##copy%d", i);
        if (nw_debug_button(id)) {
            nw_profiles_clip(&f->movement, f->parts, f->name);
        }
        nw_debug_same_line(0.0f);
        nw_debug_enable_begin(nw_profiles.dialog_event != 0);
        SDL_snprintf(id, sizeof(id), "Export...##export%d", i);
        if (nw_debug_button(id)) {
            nw_profiles_export_dialog(f->name);
        }
        nw_debug_enable_end();
        nw_debug_same_line(0.0f);
        SDL_snprintf(id, sizeof(id), "Delete##delete%d", i);
        if (nw_debug_button(id)) {
            nw_profiles_ask_for(NW_PROFILES_DELETE, f->name, 0, NULL);
        }
        nw_debug_same_line(0.0f);
        if (f->parts == NW_PARTS_ALL) {
            nw_debug_label(f->name);
        } else {
            char parts[160], label[224];
            nw_profiles_parts_text(f->parts, parts, sizeof(parts));
            SDL_snprintf(label, sizeof(label), "%s (%s)", f->name, parts);
            nw_debug_label(label);
        }
    }

    nw_debug_heading("Share");
    nw_debug_enable_begin(nw_profiles.dialog_event != 0);
    if (nw_debug_button("Import...")) {
        nw_profiles_import_dialog();
    }
    nw_debug_enable_end();
    nw_debug_same_line(0.0f);
    if (nw_debug_button("Paste")) {
        nw_profiles_paste(NW_PROFILES_IMPORT);
    }
    nw_debug_same_line(0.0f);
    if (nw_debug_button("Open folder")) {
        nw_profiles_folder();
    }
    nw_debug_text("Or drop a profile file onto the window. Ctrl+V in game applies a copied profile.");

    nw_debug_heading("Copy to clipboard");
    if (nw_debug_button("All")) {
        nw_profiles_copy(NW_PARTS_ALL);
    }
    for (int p = 0; p < NW_MOVEMENT_PARTS; p++) {
        if ((p + 1) % 5) {
            nw_debug_same_line(0.0f);
        }
        if (nw_debug_button(nw_profiles_parts[p])) {
            nw_profiles_copy(1u << p);
        }
    }
    if (nw_profiles.notice[0]) {
        nw_debug_text("%s", nw_profiles.notice);
    }
}

void nw_profiles_event(const SDL_Event *e)
{
    if (!nw_profiles.started) {
        return;
    }
    if (e->type == SDL_EVENT_DROP_FILE && e->drop.data) {
        if (nw_profiles_toml(e->drop.data)) {
            nw_profiles_offer(e->drop.data, true);
        }
    } else if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && e->key.key == SDLK_V &&
               (e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && !nw_debug_wants_keyboard()) {
        nw_profiles_paste(NW_PROFILES_APPLY);
    } else if (nw_profiles.dialog_event && e->type == nw_profiles.dialog_event) {
        const char *path = e->user.data1, *why = e->user.data2;
        if (path && e->user.code == NW_PROFILES_EXPORT) {
            nw_profiles_export(path);
        } else if (path) {
            nw_profiles_offer(path, e->user.code == NW_PROFILES_DROP);
        } else if (why) {
            SDL_snprintf(nw_profiles.notice, sizeof(nw_profiles.notice), "No file dialog: %s", why);
        }
        SDL_free(e->user.data1);
        SDL_free(e->user.data2);
    }
}

void nw_profiles_start(SDL_Window *window, const char *root, NwMovement *movement)
{
    nw_profiles_stop();
    nw_profiles.window = window;
    nw_profiles.root = root ? SDL_strdup(root) : NULL;
    nw_profiles.movement = movement;
    nw_profiles.dialog_event = SDL_RegisterEvents(1);
    nw_profiles.started = true;
    nw_droptarget_start(window, nw_profiles_valid, nw_profiles.dialog_event, NW_PROFILES_DROP);
    nw_debug_panel("N. Hanced Movement", "Profiles", nw_profiles_draw, NULL, false);
}

void nw_profiles_stop(void)
{
    if (!nw_profiles.started) {
        return;
    }
    nw_droptarget_stop();
    nw_profiles_clear();
    SDL_free(nw_profiles.root);
    SDL_zero(nw_profiles);
}
