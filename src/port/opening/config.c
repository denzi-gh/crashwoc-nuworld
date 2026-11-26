/* The opening's settings: opening.txt, one "key value..." line each, no scripting. The built-in file comes first;
   OPENING/OPENING.TXT (or NUWORLD_OPENING_TXT) overrides it; NUWORLD_OPENING=key=value,... overrides both.
   NUWORLD_OPENING_LIVE names a folder shaped as the disc (opening/, levels/) whose files stand in for the disc's
   and reload when saved. */
#include "port/opening/internal.h"
#include <float.h>
#include "port/nutexanm.h"
#include "port/signals.h"
#include "shim/shim.h"
#include "vfs/vfs.h"

struct nw_embed_file { const char *name; const unsigned char *data; size_t size; };
extern const struct nw_embed_file nw_opening_assets[], nw_level_assets[];
extern const size_t nw_opening_assets_count, nw_level_assets_count;

NwOpeningParams nw_opening;
NwOpeningLists nw_lists;
char nw_station_cut[128];
bool nw_station_loop;
bool nw_new_game_dissolve;
bool nw_assets_only;

/* ---- Files: the live folder, then the disc (and its overlays), then the built-in assets/opening ---- */

/* The live folder (NUWORLD_OPENING_LIVE) and the newest of its files' times, to see a save. NULL: off. */
static char *live_dir;
static SDL_Time live_stamp;

/* A relative disc path from the live folder, if it has the file. */
static bool live_read(const char *path, NwTexAnimFile *file)
{
    char *host = NULL;
    if (!live_dir || SDL_asprintf(&host, "%s/%s", live_dir, path) < 0) return false;
    for (char *c = host; *c; c++) if (*c == '\\') *c = '/';
    file->owner = SDL_LoadFile(host, &file->size);
    SDL_free(host);
    file->bytes = file->owner;
    return file->owner != NULL;
}

/* The built-in files by their disc folder: assets/opening as OPENING/, assets/levels as LEVELS/. */
static const struct nw_embed_file *embedded(const char *path)
{
    static const struct { const char *prefix; const struct nw_embed_file *files; const size_t *count; } bundles[] = {
        {"OPENING/", nw_opening_assets, &nw_opening_assets_count},
        {"LEVELS/", nw_level_assets, &nw_level_assets_count},
    };
    char key[NW_VFS_PATH_MAX];
    NwVfsDevice device;
    if (!nw_vfs_map(path, &device, key) || device != NW_VFS_CDROM) return NULL;
    for (size_t b = 0; b < SDL_arraysize(bundles); b++) {
        size_t n = SDL_strlen(bundles[b].prefix);
        for (size_t i = 0; !SDL_strncmp(key, bundles[b].prefix, n) && i < *bundles[b].count; i++)
            if (!SDL_strcasecmp(key + n, bundles[b].files[i].name)) return &bundles[b].files[i];
    }
    return NULL;
}

static bool file_read(void *context, const char *path, NwTexAnimFile *file)
{
    (void)context;
    *file = (NwTexAnimFile){0};
    if (live_read(path, file)) return true;
    char key[NW_VFS_PATH_MAX];
    NwVfsDevice device;
    if (!nw_vfs_map(path, &device, key) || device != NW_VFS_CDROM) return SDL_SetError("expected a relative disc path: %s", path);
    const NwVfs *vfs = nw_shim_file_vfs();
    char *disc = vfs ? nw_vfs_resolve(vfs, key) : NULL;
    if (disc) {
        file->owner = SDL_LoadFile(disc, &file->size);
        SDL_free(disc);
        file->bytes = file->owner;
        return file->owner != NULL;
    }
    const struct nw_embed_file *base = embedded(path);
    if (!base) return SDL_SetError("file not found: %s", path);
    file->bytes = (const char *)base->data;
    file->size = base->size;
    return true;
}

static void file_release(void *context, NwTexAnimFile *file)
{
    (void)context;
    SDL_free(file->owner);
    *file = (NwTexAnimFile){0};
}

const NwTexAnimIO nw_opening_io = {.read = file_read, .release = file_release};

/* ---- Parameters: the names of params.def ---- */

typedef struct { const char *name; float *v; int count; } Param;
static const Param params[] = {
#define SCALAR(f) { #f, &nw_opening.f, 1 },
#define VECTOR(f, n) { #f, nw_opening.f, n },
#include "port/opening/params.def"
#undef VECTOR
#undef SCALAR
};

static bool valid_value(const Param *p, const float *values)
{
    float v = values[0];
    if (p->v == nw_opening.cloud_r) return v > 0 && values[1] > v;
    if (p->v == nw_opening.text_arc) return values[1] > 0 && values[3] != 0;
    if (p->v == nw_opening.text_y) return values[1] != 0;
    if (p->v == &nw_opening.uis_scale || p->v == &nw_opening.uis_r0 || p->v == &nw_opening.uis_back ||
        p->v == &nw_opening.logos_r || p->v == &nw_opening.earth_r || p->v == &nw_opening.film_r)
        return v > 0;
    if (p->v == nw_opening.ox_frames) return values[1] > v;
    if (p->v == &nw_opening.flyer_pass) return v > 0;
    if (p->v == &nw_opening.ox_shots || p->v == &nw_opening.go_text ||
        p->v == &nw_opening.go_wait || p->v == &nw_opening.go_frames)
        return v >= 0 && v < (double)INT32_MAX / 4;
    return true;
}

static char *token(char **rest)
{
    char *start = *rest + strspn(*rest, " \t\r");
    if (!*start) { *rest = start; return NULL; }
    char *end = start + strcspn(start, " \t\r");
    *rest = *end ? end + 1 : end;
    *end = 0;
    return start;
}

/* Numbers separated by spaces or ':', converted as the former reader did (strtod, then float). */
static bool numbers(char **rest, float *out, int count)
{
    for (int i = 0; i < count; i++) {
        char *end, *start = *rest + strspn(*rest, " \t\r:");
        double v = strtod(start, &end);
        if (end == start || !(v >= -FLT_MAX && v <= FLT_MAX) || (*end && !strchr(" \t\r:", *end))) return false;
        out[i] = (float)v;
        *rest = end;
    }
    return !*(*rest + strspn(*rest, " \t\r"));
}

static bool set_param(const char *key, char *values)
{
    for (size_t i = 0; i < SDL_arraysize(params); i++) {
        if (SDL_strcmp(params[i].name, key)) continue;
        float v[4];
        if (!numbers(&values, v, params[i].count) || !valid_value(&params[i], v))
            return SDL_SetError("%s: expected %d finite value(s) in range", key, params[i].count);
        SDL_memcpy(params[i].v, v, sizeof(float) * (size_t)params[i].count);
        return true;
    }
    return SDL_SetError("unknown setting '%s'", key);
}

/* ---- Commands: the lines that are not parameters ---- */

static bool presentation(char **rest)
{
    char *mode = token(rest);
    if (!mode || token(rest) || (SDL_strcmp(mode, "authored") && SDL_strcmp(mode, "enhanced")))
        return SDL_SetError("expected presentation authored|enhanced");
    nw_assets_only = !SDL_strcmp(mode, "authored");
    return true;
}

/* The opening replaces disc files by source path; the built-in list applies only to the enhanced presentation. */
typedef struct { char source[NW_VFS_PATH_MAX], target[128]; bool embedded; } Asset;
static Asset *assets;
static size_t asset_count;
static bool reading_builtin;

static bool replace_asset(char **rest)
{
    char *source = token(rest), *target = token(rest);
    char key[NW_VFS_PATH_MAX], dest[NW_VFS_PATH_MAX];
    NwVfsDevice device;
    if (!source || !target || token(rest) || !nw_vfs_map(source, &device, key) || device != NW_VFS_CDROM ||
        !nw_vfs_map(target, &device, dest) || device != NW_VFS_CDROM || SDL_strchr(target, ':') ||
        SDL_strlen(target) >= sizeof(assets[0].target))
        return SDL_SetError("expected replace DISC_FILE RELATIVE_FILE (destination up to 127 bytes)");
    const char *ext = SDL_strrchr(key, '.'), *to_ext = SDL_strrchr(dest, '.');
    if (!ext || !to_ext || SDL_strcmp(ext, to_ext) ||
        (SDL_strcmp(ext, ".CUT") && SDL_strcmp(ext, ".PTL") && SDL_strcmp(ext, ".GSC") && SDL_strcmp(ext, ".GHG")))
        return SDL_SetError("replace requires matching CUT, PTL, GSC or GHG files");
    size_t i = 0;
    while (i < asset_count && SDL_strcmp(assets[i].source, key)) i++;
    if (i == asset_count) {
        Asset *more = SDL_realloc(assets, (i + 1) * sizeof(*more));
        if (!more) return false;
        assets = more;
        SDL_strlcpy(assets[i].source, key, sizeof(assets[i].source));
        asset_count++;
    }
    SDL_strlcpy(assets[i].target, target, sizeof(assets[i].target));
    assets[i].embedded = reading_builtin;
    return true;
}

const char *nw_opening_model(const char *path)
{
    char key[NW_VFS_PATH_MAX];
    NwVfsDevice device;
    if (!nw_vfs_map(path, &device, key) || device != NW_VFS_CDROM) return path;
    for (size_t i = 0; i < asset_count; i++)
        if ((!assets[i].embedded || !nw_assets_only) && !SDL_strcmp(assets[i].source, key)) return assets[i].target;
    return path;
}

char *nw_opening_data(char *path)
{
    char key[NW_VFS_PATH_MAX];
    NwVfsDevice device;
    if (!path || !nw_opening_enabled() || !nw_vfs_map(path, &device, key) || device != NW_VFS_CDROM) return path;
    for (size_t i = 0; i < asset_count; i++) {
        bool applicable = assets[i].embedded ? !nw_assets_only : (cutmovie == 0 || cutmovie == 1);
        if (!applicable || SDL_strcmp(assets[i].source, key)) continue;
        if (NuFileSize(assets[i].target) > 0) {
            if (!assets[i].embedded) SDL_Log("opening: %s -> %s", key, assets[i].target);
            return assets[i].target;
        }
        if (!assets[i].embedded) SDL_Log("opening: %s missing, using %s", assets[i].target, key);
        return path;
    }
    return path;
}

static bool scene(char **rest)
{
    char *name = token(rest), *path = token(rest), *mode = token(rest);
    if (!name || SDL_strcmp(name, "station") || !path || SDL_strlen(path) >= sizeof(nw_station_cut) ||
        !mode || (SDL_strcmp(mode, "loop") && SDL_strcmp(mode, "once")) || token(rest))
        return SDL_SetError("expected scene station CUT loop|once (path up to 127 bytes)");
    SDL_strlcpy(nw_station_cut, path, sizeof(nw_station_cut));
    nw_station_loop = !SDL_strcmp(mode, "loop");
    return true;
}

static bool on_event(char **rest)
{
    char *event = token(rest), *action = token(rest);
    if (!event || SDL_strcmp(event, "new_game") || !action || token(rest) ||
        (SDL_strcmp(action, "intro.dissolve") && SDL_strcmp(action, "intro.fade")))
        return SDL_SetError("expected on new_game intro.dissolve|intro.fade");
    nw_new_game_dissolve = !SDL_strcmp(action, "intro.dissolve");
    return true;
}

/* A new entry of a list, its index; -1 when full. A file's first line of a list replaces what an earlier file gave. */
static int reading_file, list_file[3];

static int list_add(int list, int *count, int max)
{
    if (list_file[list] != reading_file) *count = 0;
    list_file[list] = reading_file;
    if (*count == max) SDL_SetError("at most %d of these", max);
    return *count == max ? -1 : (*count)++;
}

/* flyer_model PATH [anim] */
static bool flyer_model(char **rest)
{
    char *path = token(rest), *anim = token(rest);
    int i = -1;
    if (!path || SDL_strlen(path) >= sizeof(nw_lists.model[0].path) || (anim && SDL_strcmp(anim, "anim")) || token(rest))
        return SDL_SetError("expected flyer_model PATH [anim]");
    if ((i = list_add(0, &nw_lists.models, 8)) < 0) return false;
    SDL_strlcpy(nw_lists.model[i].path, path, sizeof(nw_lists.model[0].path));
    nw_lists.model[i].anim = anim != NULL;
    return true;
}

static bool flyer_anim(char **rest)
{
    char *path = token(rest);
    if (!path || SDL_strlen(path) >= sizeof(nw_lists.anim) || token(rest)) return SDL_SetError("expected flyer_anim PATH");
    SDL_strlcpy(nw_lists.anim, path, sizeof(nw_lists.anim));
    return true;
}

/* flyer_way FROMx FROMy FROMdepth TOx TOy TOdepth */
static bool flyer_way(char **rest)
{
    float v[6];
    int i = -1;
    if (!numbers(rest, v, 6)) return SDL_SetError("expected flyer_way x y depth x y depth");
    if ((i = list_add(1, &nw_lists.ways, 8)) < 0) return false;
    SDL_memcpy(&nw_lists.way[i], v, sizeof(v));
    return true;
}

/* passer comet|star|saucer FROMx FROMy TOx TOy DEPTH SIZE SECONDS EVERY FIRST */
static bool passer(char **rest)
{
    static const char *const kinds[] = {"comet", "star", "saucer"};
    char *kind = token(rest);
    int k = 0;
    float v[9];
    while (k < 3 && (!kind || SDL_strcmp(kind, kinds[k]))) k++;
    if (k == 3 || !numbers(rest, v, 9))
        return SDL_SetError("expected passer comet|star|saucer x y x y depth size seconds every first");
    int i = list_add(2, &nw_lists.passers, 4);
    if (i < 0) return false;
    nw_lists.passer[i].kind = k + 1;
    SDL_memcpy(&nw_lists.passer[i].x0, v, sizeof(v));
    return true;
}

static const struct { const char *name; bool (*run)(char **rest); } commands[] = {
    {"presentation", presentation}, {"replace", replace_asset},
    {"scene", scene}, {"on", on_event},
    {"flyer_model", flyer_model}, {"flyer_anim", flyer_anim}, {"flyer_way", flyer_way}, {"passer", passer},
};

/* Reads one settings file; ';' starts a comment. An error names the line and stops the file. */
static bool read_settings(const char *path, const char *text, size_t size)
{
    char line[512];
    int number = 0;
    reading_file++;
    for (const char *p = text, *end = text + size; p < end; number++) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t length = (size_t)((eol ? eol : end) - p);
        SDL_strlcpy(line, p, SDL_min(length + 1, sizeof(line)));
        p += length + 1;
        line[strcspn(line, ";")] = 0;
        char *rest = line, *key = token(&rest);
        if (!key) continue;
        bool ok = false, command = false;
        for (size_t i = 0; i < SDL_arraysize(commands) && !command; i++)
            if (!SDL_strcmp(key, commands[i].name)) { command = true; ok = commands[i].run(&rest); }
        if (!command) ok = set_param(key, rest);
        if (!ok) {
            SDL_Log("%s:%d: %s", path, number + 1, SDL_GetError());
            return false;
        }
    }
    return true;
}

static bool read_file(const char *path, bool builtin)
{
    NwTexAnimFile file;
    if (!file_read(NULL, path, &file)) { SDL_Log("opening: %s", SDL_GetError()); return false; }
    reading_builtin = builtin;
    bool ok = read_settings(path, file.bytes, file.size);
    reading_builtin = false;
    file_release(NULL, &file);
    return ok;
}

/* A mod's settings: a host file named by NUWORLD_OPENING_TXT, else an overlay's OPENING/OPENING.TXT. */
static bool read_mod(const char *host_path, bool overlay)
{
    if (!host_path) return !overlay || read_file("opening/opening.txt", false);
    size_t size;
    char *text = SDL_LoadFile(host_path, &size);
    if (!text) { SDL_Log("opening: %s: %s", host_path, SDL_GetError()); return false; }
    bool ok = read_settings(host_path, text, size);
    SDL_free(text);
    return ok;
}

static SDL_EnumerationResult SDLCALL live_newest(void *newest, const char *dir, const char *name)
{
    char *path = NULL;
    SDL_PathInfo info;
    if (SDL_asprintf(&path, "%s%s", dir, name) >= 0 && SDL_GetPathInfo(path, &info)) {
        if (info.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(path, live_newest, newest);
        else if (info.modify_time > *(SDL_Time *)newest) *(SDL_Time *)newest = info.modify_time;
    }
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static SDL_Time live_time(void)
{
    SDL_Time newest = 0;
    if (live_dir) SDL_EnumerateDirectory(live_dir, live_newest, &newest);
    return newest;
}

/* Once a second of the sky: a saved file in the live folder reloads the scene's ATS effects, the direction's and
   the looks; a file that fails keeps what was in use. */
void nw_opening_live_check(void)
{
    if (!live_dir || nw_op.sky_frames % 60) return;
    SDL_Time newest = live_time();
    if (newest == live_stamp) return;
    live_stamp = newest;
    SDL_Log("opening: %s changed, reloading its ATS and .looks files", live_dir);
    nw_texanim_scene_reload();
    nw_opening_letters_follow();
    if (!nw_opening_looks_load()) SDL_Log("opening reload: %s", SDL_GetError());
}

/* The settings as signals, under their names (port/signals.h): what .looks drives read, as elec and wobble. */
static bool bind_params(void)
{
    for (size_t i = 0; i < SDL_arraysize(params); i++)
        if (params[i].count == 1 && !nw_signal_bind(params[i].name, params[i].v)) return false;
    return true;
}

/* The signals the listed effects' param tracks write, made before a .looks file reads them. */
static void make_signal(void *context, const char *effect, const NwCurve *curve)
{
    (void)context;
    char name[96];
    SDL_snprintf(name, sizeof(name), "%s.%s", effect, curve->name);
    nw_signal(name);
}

void nw_opening_read(void)
{
    const char *env = SDL_getenv("NUWORLD_OPENING"), *host_path = SDL_getenv("NUWORLD_OPENING_TXT");
    nw_texanim_scene_list(NULL, (NwTexAnimIO){0});
    nw_op.on = false;
    asset_count = 0;
    SDL_zero(nw_lists);
    const char *live = SDL_getenv("NUWORLD_OPENING_LIVE");
    SDL_free(live_dir);
    live_dir = live && *live ? SDL_strdup(live) : NULL;
    live_stamp = live_time();
    const NwVfs *vfs = nw_shim_file_vfs();
    char *mounted = vfs ? nw_vfs_resolve(vfs, "OPENING/OPENING.TXT") : NULL;
    bool overlay = mounted != NULL;
    SDL_free(mounted);
    if (!env && !host_path && !overlay && !live_dir) return;
    const struct nw_embed_file *builtin = embedded("opening/opening.txt");
    reading_builtin = true;
    bool ok = builtin && read_settings("built-in opening.txt", (const char *)builtin->data, builtin->size);
    reading_builtin = false;
    if (!ok || !read_mod(host_path, overlay)) return;
    char buf[512], *rest = NULL;
    SDL_strlcpy(buf, env ? env : "", sizeof(buf));
    for (char *s = SDL_strtok_r(buf, ",", &rest); s; s = SDL_strtok_r(NULL, ",", &rest)) {
        char *eq = SDL_strchr(s, '=');
        if (eq && (*eq = 0, !set_param(s, eq + 1))) SDL_Log("NUWORLD_OPENING: %s", SDL_GetError());
    }
    if (!bind_params() || !nw_opening_direction_load()) { SDL_Log("opening: %s", SDL_GetError()); return; }
    /* The scene's own ATS V2 effects, loaded and played by TexAnim; their signals first, for the looks. */
    nw_texanim_scene_curves("opening/ats.txt", nw_opening_io, make_signal, NULL);
    if (!nw_opening_looks_load()) { SDL_Log("opening: %s", SDL_GetError()); return; }
    nw_texanim_scene_list("opening/ats.txt", nw_opening_io);
    nw_op.on = true;
    SDL_Log("opening: presentation %s", nw_assets_only ? "authored" : "enhanced");
    if (live_dir) SDL_Log("opening: live files from %s", live_dir);
}
