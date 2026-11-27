#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.h"
#include "audio/nusound.h"
#include "frontend/help.h"
#include "frontend/ui.h"
#include "frontend/wizard.h"
#include "gfx/gl.h"
#include "platform/buildid.h"
#include "platform/crash.h"
#include "platform/discord.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "platform/prompts.h"
#include "platform/update.h"
#include "vfs/extract.h"
#ifdef NW_WITH_GAME
#include "data/image.h"
#include "frontend/cheatpanel.h"
#include "frontend/controlpanel.h"
#include "frontend/debug.h"
#include "frontend/profiles.h"
#include "frontend/saveexport.h"
#include "frontend/saveimport.h"
#include "frontend/settings.h"
#include "frontend/video.h"
#include "gameplay/gameplay.h"
#include "gameplay/minigame.h"
#include "gfx/backend.h"
#include "gfx/gsc.h"
#include "gfx/view.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/capture.h"
#include "port/disc.h"
#include "port/dump.h"
#include "port/game.h"
#include "port/hz.h"
#include "port/rev.h"
#include "port/level.h"
#include "port/padscript.h"
#include "port/options.h"
#include "port/presence.h"
#include "port/savemenu.h"
#include "port/saves.h"
#include "port/autosave.h"
#include "save/saveworker.h"
#include "port/scene.h"
#include "port/trace.h"
#include "port/wide.h"
#include "shim/shim.h"
#include "vfs/dat.h"
#include "vfs/sha256.h"
#include "vfs/vfs.h"
#endif

static const char *nw_log_arg, *nw_log_file_arg;
static bool nw_gl_debug_arg;

static bool nw_print_progress(void *user, uint64_t done, uint64_t total)
{
    int *percent = user;
    int now = total ? (int)(done * 100 / total) : 100;
    if (now != *percent) {
        *percent = now;
        printf("\r  %3d%%  %llu / %llu MB", now, (unsigned long long)(done >> 20), (unsigned long long)(total >> 20));
        fflush(stdout);
    }
    return true;
}

/* --data-dir, else <exe dir>/user/ in portable mode, else the pref path. */
static char *nw_data_root(const char *data_dir)
{
    char *root = data_dir ? nw_paths_dir(data_dir, NULL, true) : nw_paths_data_root(SDL_GetBasePath(), NULL);
    if (root && !nw_paths_create_layout(root)) {
        SDL_free(root);
        root = NULL;
    }
    if (!root) {
        nw_log_error(NW_LOG_APP, "no data folder: %s", SDL_GetError());
    }
    return root;
}

static int nw_import(const char *image, const char *data_dir)
{
    NwDisc disc;
    NwIso iso;
    int percent = -1;
    char *root = nw_data_root(data_dir);
    if (!root) {
        return 1;
    }

    printf("Importing %s\n", image);
    const NwManifest *m = NULL;
    if (nw_disc_open(&disc, image)) {
        if (nw_iso_index(&iso, &disc)) {
            printf("  %s, %u sectors, %u files\n", nw_disc_format_name(disc.format), disc.sectors, iso.count);
            m = nw_extract_disc(&disc, &iso, nw_disc_manifests, root, nw_print_progress, &percent);
            if (percent >= 0) {
                printf("\n");
            }
            nw_iso_free(&iso);
        }
        nw_disc_close(&disc);
    }
    bool ok = m != NULL;
    if (ok) {
        printf("Imported %s into %s" NW_DISC_DIR "/\n", m->name, root);
    } else {
        fflush(stdout);
        fprintf(stderr, NW_APP_NAME ": import failed: %s\n", SDL_GetError());
    }
    SDL_free(root);
    return ok ? 0 : 1;
}

static int nw_verify(const char *data_dir)
{
    int percent = -1;
    char *root = nw_data_root(data_dir);
    if (!root) {
        return 1;
    }

    printf("Verifying %s" NW_DISC_DIR "/\n", root);
    const NwManifest *m = nw_extract_manifest(root, nw_disc_manifests);
    bool ok = m && nw_extract_verify(root, m, nw_print_progress, &percent);
    if (percent >= 0) {
        printf("\n");
    }
    if (ok) {
        printf("All files match %s\n", m->name);
    } else {
        fflush(stdout);
        fprintf(stderr, NW_APP_NAME ": verify failed: %s\n", SDL_GetError());
    }
    SDL_free(root);
    return ok ? 0 : 1;
}

#ifdef NW_WITH_GAME
static int nw_import_save(const char *file, const char *data_dir)
{
    char *root = nw_data_root(data_dir);
    char *saves = root ? nw_paths_dir(root, NW_PATHS_SAVES, false) : NULL;
    int made = saves ? nw_saves_import(saves, file) : -1;
    if (made > 0) {
        printf("Imported %d games of %s into %s\n", made, file, saves);
    } else if (made < 0 && root) {
        fprintf(stderr, NW_APP_NAME ": save import failed: %s\n", SDL_GetError());
    } else if (made == 0) {
        fprintf(stderr, NW_APP_NAME ": %s holds no save\n", file);
    }
    SDL_free(saves);
    SDL_free(root);
    return made > 0 ? 0 : made == 0 ? 2 : 1;
}

/* slots: comma-separated per PS2 slot, a save's number, "-" keeps the card's game, "x" empties it. */
static int nw_export_save(const char *card, const char *slots, const char *data_dir)
{
    int picks[NW_SAVE_PS2_GAMES] = { NW_SAVE_PS2_KEEP, NW_SAVE_PS2_KEEP, NW_SAVE_PS2_KEEP, NW_SAVE_PS2_KEEP };
    int count = 0;
    for (const char *p = slots; *p;) {
        const char *end = SDL_strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : SDL_strlen(p);
        char item[8] = "";
        int n = 0;
        if (len < sizeof(item)) {
            SDL_memcpy(item, p, len);
        }
        if (count >= NW_SAVE_PS2_GAMES || len >= sizeof(item) || !len) {
            count = -1;
            break;
        }
        if (SDL_strcmp(item, "-") == 0) {
            picks[count++] = NW_SAVE_PS2_KEEP;
        } else if (SDL_strcasecmp(item, "x") == 0) {
            picks[count++] = NW_SAVE_PS2_EMPTY;
        } else if (SDL_sscanf(item, "%d", &n) == 1 && n >= 1 && n <= NW_SAVE_GAMES && SDL_isdigit(item[0]) &&
                   SDL_isdigit(item[len - 1])) {
            picks[count++] = n - 1;
        } else {
            count = -1;
            break;
        }
        p = end ? end + 1 : p + len;
    }
    if (count <= 0) {
        fprintf(stderr, NW_APP_NAME ": --export-save <card.ps2> <slots>: slots are up to 4 of a save's number (1 to "
                        "%d), - (keep the card's) or x (empty), comma-separated\n", NW_SAVE_GAMES);
        return 2;
    }
    char *root = nw_data_root(data_dir);
    char *saves = root ? nw_paths_dir(root, NW_PATHS_SAVES, false) : NULL;
    NwVfs vfs;
    bool vfs_open = root && nw_extract_has_disc(root) && nw_vfs_init(&vfs, root);
    size_t icon_len = 0;
    void *icon = vfs_open ? nw_saveexport_icon(&vfs, &icon_len) : NULL; /* a card with the save's has one */
    bool ok = saves && nw_saves_export(saves, card, nw_saveexport_ps2_name(vfs_open ? &vfs : NULL), picks, icon,
                                       icon_len);
    if (ok) {
        printf("Exported to %s (the card before is %s.bak)\n", card, card);
    } else if (root) {
        fprintf(stderr, NW_APP_NAME ": save export failed: %s\n", SDL_GetError());
    }
    SDL_free(icon);
    if (vfs_open) {
        nw_vfs_shutdown(&vfs);
    }
    SDL_free(saves);
    SDL_free(root);
    return ok ? 0 : 1;
}

static void *nw_read_elf(const NwVfs *vfs, size_t *size)
{
    char path[NW_VFS_PATH_MAX];
    SDL_snprintf(path, sizeof(path), "cdrom0:\\%s;1", vfs->manifest->boot);
    const NwManifestEntry *e = nw_manifest_find(vfs->manifest, vfs->manifest->boot);
    NwVfsFile *f = nw_vfs_open(vfs, path, NW_VFS_READ);
    if (!e || !f) {
        nw_vfs_close(f);
        return NULL;
    }
    int64_t n = nw_vfs_size(f);
    void *data = n == (int64_t)e->size ? SDL_malloc((size_t)n) : NULL;
    bool ok = data && nw_vfs_read(f, data, (size_t)n) == n;
    nw_vfs_close(f);
    if (ok) {
        uint8_t digest[NW_SHA256_SIZE];
        char hex[NW_SHA256_HEX];
        nw_sha256(data, (size_t)n, digest);
        nw_sha256_hex(digest, hex);
        ok = SDL_strcmp(hex, e->sha256) == 0;
    }
    if (!ok) {
        SDL_free(data);
        SDL_SetError("%s is not the %s ELF (run --verify)", vfs->manifest->boot, vfs->manifest->name);
        return NULL;
    }
    *size = (size_t)n;
    return data;
}

static bool nw_image_build(const NwManifest *m, const void *elf, size_t size, NwImageStats *stats, char *err,
                           size_t err_size)
{
    if (m->flags & NW_DISC_DATA_ELF) {
        return nw_image_load(&nw_image_tables, elf, size, stats, err, err_size);
    }
    return nw_image_load_disc(&nw_image_tables, m->name, elf, size, stats, err, err_size);
}

static int nw_check_data(const char *data_dir)
{
    NwVfs vfs;
    size_t size = 0;
    char *root = nw_data_root(data_dir);
    void *elf = root && nw_vfs_init(&vfs, root) ? nw_read_elf(&vfs, &size) : NULL;
    if (!elf) {
        fprintf(stderr, NW_APP_NAME ": no data image: %s\n", SDL_GetError());
        if (root) {
            nw_vfs_shutdown(&vfs);
        }
        SDL_free(root);
        return 2;
    }
    bool pal = vfs.manifest->flags & NW_DISC_DATA_ELF;
    if (!pal) {
        printf("%s: the data image without the PAL v1.03 ELF\n", vfs.manifest->name);
    }
    NwImageStats stats;
    char err[512];
    bool ok = nw_image_build(vfs.manifest, elf, size, &stats, err, sizeof(err));
    nw_image_report(&stats, stdout);
    if (!ok) {
        fflush(stdout);
        fprintf(stderr, NW_APP_NAME ": data image: %s\n", err);
    } else {
        if (pal) {
            NwImageTrip trip;
            ok = nw_image_roundtrip(&trip, stdout, err, sizeof(err));
            if (ok) {
                nw_image_trip_report(&trip, stdout);
            }
            fflush(stdout);
            if (!ok) {
                fprintf(stderr, NW_APP_NAME ": data image round trip: %s\n", err);
            } else if (trip.differ[0] || trip.differ[1]) {
                fprintf(stderr, NW_APP_NAME ": the data image does not convert back to the ELF\n");
                ok = false;
            }
            if (ok && nw_image_seed_check(stdout)) {
                fflush(stdout);
                fprintf(stderr, NW_APP_NAME ": port/data.toml [seeds] does not make the ELF's mirror roots\n");
                ok = false;
            }
        }
        if (ok) {
            int kept = nw_hz_patch();
            printf("60 Hz table: %d values not retail's\n", kept);
            printf("late revision table: %d values not PAL v1.03's\n", nw_rev_patch(60));
            ok = kept == 0;
        }
    }
    nw_image_free();
    SDL_free(elf);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return ok ? 0 : 1;
}

/* The decomp's RNC unpacker (NuFileLoadBuffer's). */
void NuPPUnpack(unsigned char *src, unsigned char *out);

/* Takes over data; NULL with SDL_GetError() for a bad header. */
static uint8_t *nw_unpack_rnc(uint8_t *data, size_t n, size_t *size)
{
    if (n >= 18 && memcmp(data, "RNC\x02", 4) == 0) {
        uint32_t unpacked = (uint32_t)data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
        uint32_t packed = (uint32_t)data[8] << 24 | data[9] << 16 | data[10] << 8 | data[11];
        uint8_t *out = packed <= n - 18 && unpacked <= (64u << 20) ? SDL_malloc(unpacked + 1) : NULL;
        if (out) {
            NuPPUnpack(data, out);
        } else {
            SDL_SetError("RNC header of %u bytes for %u", packed, unpacked);
        }
        SDL_free(data);
        data = out;
        n = unpacked;
    }
    *size = n;
    return data;
}

static uint8_t *nw_read_scene(const NwVfs *vfs, const NwManifestEntry *e, size_t *size)
{
    char path[NW_VFS_PATH_MAX];
    SDL_snprintf(path, sizeof(path), "cdrom0:\\%s;1", e->path);
    for (char *c = path; *c; c++) {
        *c = *c == '/' ? '\\' : *c;
    }
    NwVfsFile *f = nw_vfs_open(vfs, path, NW_VFS_READ);
    int64_t n = f ? nw_vfs_size(f) : -1;
    uint8_t *data = n == (int64_t)e->size ? SDL_malloc((size_t)n + 1) : NULL;
    bool ok = data && nw_vfs_read(f, data, (size_t)n) == n;
    nw_vfs_close(f);
    if (!ok) {
        SDL_free(data);
        SDL_SetError("missing or not the size the manifest lists (run --verify)");
        return NULL;
    }
    return nw_unpack_rnc(data, (size_t)n, size);
}

static void nw_check_disc_line(const char *kind, const char *archive, const char *path, const void *data, size_t size)
{
    uint8_t digest[NW_SHA256_SIZE];
    char hex[NW_SHA256_HEX];
    nw_sha256(data, size, digest);
    nw_sha256_hex(digest, hex);
    printf("%s %s%s%s size=%zu sha256=%s\n", kind, archive ? archive : "", archive ? ":" : "", path, size, hex);
}

static void nw_check_dat_path(void *user, int index, const char *path)
{
    char **paths = user;
    paths[index] = SDL_strdup(path);
}

static int nw_check_dat(const NwVfs *vfs, const char *archive)
{
    char game_path[NW_VFS_PATH_MAX];
    SDL_snprintf(game_path, sizeof(game_path), "cdrom0:\\%s;1", archive);
    NwDat dat;
    if (!nw_dat_open(&dat, vfs, game_path)) {
        fprintf(stderr, NW_APP_NAME ": %s: %s\n", archive, SDL_GetError());
        return -1;
    }
    char **paths = SDL_calloc(dat.count + 1, sizeof(*paths));
    bool ok = paths && nw_dat_list(&dat, nw_check_dat_path, paths);
    for (uint32_t i = 0; ok && i < dat.count; i++) {
        const NwDatEntry *e = &dat.entries[i];
        size_t size = e->size;
        uint8_t *data = nw_dat_load(&dat, (int)i);
        if (data && (e->flags & NW_DAT_PACKED)) {
            data = nw_unpack_rnc(data, e->size, &size);
        }
        ok = data && paths[i] && (!(e->flags & NW_DAT_PACKED) || size == e->unpacked_size);
        if (ok) {
            nw_check_disc_line("DAT", archive, paths[i], data, size);
        } else {
            fflush(stdout);
            fprintf(stderr, NW_APP_NAME ": %s entry %u: %s\n", archive, i,
                    paths[i] ? "not unpacked to its listed size" : "not in the path tree");
        }
        SDL_free(data);
    }
    int count = ok ? (int)dat.count : -1;
    for (uint32_t i = 0; paths && i < dat.count; i++) {
        SDL_free(paths[i]);
    }
    SDL_free(paths);
    nw_dat_close(&dat);
    return count;
}

/* Prints "FILE|DAT <path> size=<n> sha256=<hex>" lines, the list tools/disc_check.py makes. */
static int nw_check_disc(const char *data_dir)
{
    NwVfs vfs;
    char *root = nw_data_root(data_dir);
    if (!root || !nw_extract_has_disc(root) || !nw_vfs_init(&vfs, root)) {
        fprintf(stderr, NW_APP_NAME ": no imported disc (run --import)\n");
        SDL_free(root);
        return 2;
    }
    unsigned files = 0, entries = 0, failed = 0;
    for (size_t i = 0; i < vfs.manifest->count; i++) {
        const NwManifestEntry *e = &vfs.manifest->entries[i];
        if (e->flags & NW_MANIFEST_SKIP) {
            continue;
        }
        size_t size = 0, len = strlen(e->path);
        uint8_t *data = nw_read_scene(&vfs, e, &size);
        bool read = data != NULL;
        files++;
        if (read) {
            nw_check_disc_line("FILE", NULL, e->path, data, size);
        } else {
            failed++;
            fflush(stdout);
            fprintf(stderr, NW_APP_NAME ": %s: %s\n", e->path, SDL_GetError());
        }
        SDL_free(data);
        if (read && len >= 4 && SDL_strcasecmp(e->path + len - 4, ".DAT") == 0) {
            int n = nw_check_dat(&vfs, e->path);
            failed += n < 0;
            entries += n > 0 ? (unsigned)n : 0;
        }
    }
    printf("TOTAL files=%u entries=%u failed=%u\n", files, entries, failed);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return failed ? 1 : 0;
}

static int nw_check_canon(const char *data_dir)
{
    NwVfs vfs;
    char *root = nw_data_root(data_dir);
    if (!root || !nw_extract_has_disc(root) || !nw_vfs_init(&vfs, root)) {
        fprintf(stderr, NW_APP_NAME ": no imported disc (run --import)\n");
        SDL_free(root);
        return 2;
    }
    unsigned patched = 0, added = 0, scenes = 0, failed = 0;
    for (size_t i = 0; i < nw_canon_table.count; i++) {
        const NwCanonFile *c = &nw_canon_table.files[i];
        if (c->disc != vfs.manifest) {
            continue;
        }
        char *path = c->sha256 ? nw_vfs_resolve(&vfs, c->path) : NULL;
        size_t size = 0;
        uint8_t *data = !c->sha256 || path ? nw_canon_load(&nw_canon_table, c, path, &size) : NULL;
        const NwManifestEntry *want = nw_manifest_find(&nw_disc_manifest_usa101, c->path);
        uint8_t digest[NW_SHA256_SIZE];
        char hex[NW_SHA256_HEX] = "";
        if (data) {
            nw_sha256(data, size, digest);
            nw_sha256_hex(digest, hex);
        }
        bool ok = data && (c->scene || (want && want->size == size && SDL_strcmp(want->sha256, hex) == 0));
        printf("%s %s size=%zu sha256=%s%s\n", !c->sha256 ? "ADD" : c->scene ? "SCENE" : "PATCH", c->path, size, hex,
               ok ? (c->scene ? "" : " = USA v1.01") : " FAILED");
        if (!data) {
            fflush(stdout);
            fprintf(stderr, NW_APP_NAME ": %s: %s\n", c->path, SDL_GetError());
        }
        failed += !ok;
        added += ok && !c->sha256;
        scenes += ok && c->sha256 && c->scene;
        patched += ok && c->sha256 && !c->scene;
        SDL_free(data);
        SDL_free(path);
    }
    printf("TOTAL %s: patched=%u added=%u scenes=%u failed=%u\n", vfs.manifest->name, patched, added, scenes, failed);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return failed ? 1 : 0;
}

static int nw_check_scenes(const char *data_dir)
{
    NwVfs vfs;
    char *root = nw_data_root(data_dir);
    if (!root || !nw_extract_has_disc(root) || !nw_vfs_init(&vfs, root)) {
        fprintf(stderr, NW_APP_NAME ": no imported disc (run --import)\n");
        SDL_free(root);
        return 2;
    }
    NwGscTotal total = { 0 };
    for (size_t i = 0; i < vfs.manifest->count; i++) {
        const NwManifestEntry *e = &vfs.manifest->entries[i];
        size_t len = strlen(e->path), size = 0;
        if (len < 4 || SDL_strcasecmp(e->path + len - 4, ".GSC") != 0 || (e->flags & NW_MANIFEST_SKIP)) {
            continue;
        }
        uint8_t *data = nw_read_scene(&vfs, e, &size);
        NwGsc g;
        bool ok = data && nw_gsc_open(&g, data, size);
        if (ok) {
            ok = nw_gsc_report(&g, e->path, stdout, &total);
            nw_gsc_close(&g);
        } else {
            total.files++;
            total.failed++;
        }
        if (!ok) {
            fflush(stdout);
            fprintf(stderr, NW_APP_NAME ": %s: %s\n", e->path, SDL_GetError());
        }
        SDL_free(data);
    }
    nw_gsc_report_total(&total, stdout);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return total.failed || total.unknown ? 1 : 0;
}

/* The decomp's main, renamed by port/units.toml. */
int nw_game_main(int argc, char **argv);
extern int LANGUAGEOPTION;

/* PAL's on every disc: the decomp is its code. */
static char nw_game_arg0[] = "cdrom0:\\SLES_503.86;1";
static char *nw_game_argv[] = { nw_game_arg0, NULL };

#define NW_BOOT_CHECK_MS 30000         /* --boot-check's watchdog; at the hardware speed BGLoadSfx alone takes 13 s */
#define NW_GAME_PARK_MS 1000
#define NW_SCENE_CHECK_FRAMES 3
#define NW_CAPTURE_MS 300000
#define NW_DUMP_MS 3600000
#define NW_LEVEL_CHECK_MS 600000
#define NW_SETTINGS_SAVE_MS 1000       /* how long settings stay unchanged before they are saved */

static const char *nw_run_level;
static const char *nw_pad_script;
static const char *nw_overlays[NW_VFS_OVERLAYS]; /* the later wins */
static int nw_overlay_count;

/* --pad-script and --revision 1.03 play the retail game: no [game] switches. */
static bool nw_run_retail(void)
{
    return nw_pad_script || !nw_rev;
}

static void nw_overlay_conflict(void *user, const char *key)
{
    nw_log_warn(NW_LOG_LOAD, "--overlay %s: %s is in an earlier overlay too; this one wins", (const char *)user, key);
}

/* The file system stays open for the game. */
static int nw_boot_prepare(const char *data_dir, NwClockMode mode, bool hardware_speed)
{
    static NwVfs vfs;
    size_t size = 0;
    char *root = nw_data_root(data_dir);
    bool vfs_open = root && nw_vfs_init(&vfs, root);
    void *elf = vfs_open ? nw_read_elf(&vfs, &size) : NULL;
    char *saves = root ? nw_paths_dir(root, NW_PATHS_SAVES, false) : NULL;
    nw_saves_init(saves);
    nw_savemenu_platform(nw_prompts_on_deck());
    SDL_free(saves);
    SDL_free(root);
    if (!elf) {
        nw_log_error(NW_LOG_LOAD, "no data image: %s", SDL_GetError());
        if (vfs_open) {
            nw_vfs_shutdown(&vfs);
        }
        return 2;
    }
    for (int i = 0; i < nw_overlay_count; i++) {
        if (!nw_vfs_overlay(&vfs, nw_overlays[i], nw_overlay_conflict, (void *)nw_overlays[i])) {
            nw_log_error(NW_LOG_LOAD, "--overlay: %s", SDL_GetError());
            SDL_free(elf);
            nw_vfs_shutdown(&vfs);
            return 2;
        }
        nw_log_info(NW_LOG_LOAD, "--overlay %s: %u files", nw_overlays[i], vfs.mounts[NW_VFS_OVERLAY].count);
    }
    nw_shim_file_init(&vfs);
    nw_newlib_init();
    NwImageStats stats;
    char err[512];
    bool ok = nw_image_build(vfs.manifest, elf, size, &stats, err, sizeof(err));
    SDL_free(elf);
    if (!ok) {
        nw_log_error(NW_LOG_LOAD, "data image: %s", err);
        return 1;
    }
    if (vfs.manifest->flags & NW_DISC_ENGLISH) {
        LANGUAGEOPTION = 0;
    }
    if (!nw_rev && vfs.manifest != &nw_disc_manifest) {
        nw_log_error(NW_LOG_APP, "--revision 1.03 runs only the PAL v1.03 disc, not %s", vfs.manifest->name);
        return 2;
    }
    vfs.canon = nw_rev ? &nw_canon_table : NULL;
    unsigned canon_files = 0;
    for (size_t i = 0; vfs.canon && i < vfs.canon->count; i++) {
        canon_files += vfs.canon->files[i].disc == vfs.manifest;
    }
    if (canon_files) {
        nw_log_info(NW_LOG_LOAD, "late revision: %u level files are USA v1.01's", canon_files);
    }
    /* Every disc plays at 60 Hz; --revision 1.03 is the PAL PS2's game at its 50 Hz. */
    int hz = nw_hz_set(nw_rev ? 60 : 50);
    int script_hz = nw_pad_script ? nw_pad_script_hz(nw_pad_script) : 0;
    if (script_hz && script_hz != hz) {
        nw_log_error(NW_LOG_APP, "--pad-script %s replays %d Hz ticks: %s", nw_pad_script, script_hz,
                     script_hz == 50 ? "it needs --revision 1.03" : "not with --revision 1.03");
        return 2;
    }
    nw_log_info(NW_LOG_LOAD, "disc: %s at %d Hz, revision %s", vfs.manifest->name, hz, nw_rev_name());
    int kept = hz == 60 ? nw_hz_patch() : 0;
    int rev_kept = nw_rev ? nw_rev_patch(hz) : 0;
    if (rev_kept) {
        nw_log_info(NW_LOG_GAME, "late revision: %d data values are not PAL v1.03's and stay", rev_kept);
    }
    if (kept) {
        nw_log_info(NW_LOG_GAME, "60 Hz: %d data values are not retail's and stay", kept);
    }
    if (!nw_game_init(mode, (unsigned)hz)) {
        nw_log_error(NW_LOG_APP, "no vblank clock: %s", SDL_GetError());
        return 1;
    }
    nw_nusound_init(&vfs, nw_game_us);
    nw_nusound_set_hardware_speed(hardware_speed);
    if (nw_run_level && !nw_level_start(nw_run_level)) {
        nw_log_error(NW_LOG_APP, "--level %s: no such level on the disc", nw_run_level);
        return 2;
    }
    if (nw_pad_script && !nw_pad_script_start(&vfs, nw_pad_script, nw_level_final(), hz)) {
        nw_log_error(NW_LOG_APP, "--pad-script %s: %s", nw_pad_script, SDL_GetError());
        return 2;
    }
    return 0;
}

static const char *nw_game_state_name(NwGameState state)
{
    switch (state) {
    case NW_GAME_IDLE:
        return "not started";
    case NW_GAME_RUNNING:
        return "running";
    case NW_GAME_PARKED:
        return "parked";
    case NW_GAME_FATAL:
        return "stopped by a fatal error";
    case NW_GAME_RETURNED:
        return "returned from main";
    }
    return "?";
}

static size_t nw_boot_crash_state(char *out, size_t size)
{
    const NwPresenceSnap *s = nw_presence_peek();
    int n;
    if (s->valid) {
        n = SDL_snprintf(out, size,
                         "state: level %d %s, warp room %d, mode %d, cutscene %d, menu %d, bonus %d%s%s%s%s\n",
                         s->level, s->level_name, s->hub, s->game_mode, s->cutmovie, s->menu, s->bonus,
                         s->paused ? ", paused" : "", s->loading ? ", loading" : "", s->demo ? ", demo" : "",
                         s->time_trial ? ", time trial" : "");
    } else {
        n = SDL_snprintf(out, size, "state: no frame drawn yet\n");
    }
    n = SDL_clamp(n, 0, (int)size - 1);
    int m = SDL_snprintf(out + n, size - (size_t)n, "game thread: %s, %llu frames, %llu vblanks\n",
                         nw_game_state_name(nw_game_state()), (unsigned long long)nw_game_swaps(),
                         (unsigned long long)nw_game_edges());
    return (size_t)n + (size_t)SDL_clamp(m, 0, (int)(size - (size_t)n) - 1);
}

/* A game thread that misses the deadline is left running; the process must then _Exit, not unwind under it. */
static bool nw_game_parked(void)
{
    NwGameState state = nw_game_state() == NW_GAME_RUNNING ? nw_game_stop(NW_GAME_PARK_MS) : nw_game_state();
    if (state == NW_GAME_RUNNING) {
        nw_log_warn(NW_LOG_GAME, "the game thread did not stop within %d ms", NW_GAME_PARK_MS);
        return false;
    }
    if (state == NW_GAME_PARKED || state == NW_GAME_RETURNED) {
        nw_saves_finish();
        nw_autosave_finish();
        nw_save_worker_shutdown();
    }
    return true;
}

static int nw_boot_exit(int code, bool parked)
{
    nw_log_close(code);
    if (!parked) {
        fflush(stdout);
        fflush(stderr);
        _Exit(code);
    }
    return code;
}

/* stdout, in first-hit order; tools/stub_hits.py reads it. */
static void nw_print_stub_hits(const char *mode, NwGameState state)
{
    unsigned probes = 0;
    printf("%s: stub and near-match hits in first-hit order (order, stub|near, unit, function, retail address, "
           "hits):\n", mode);
    unsigned stubs = nw_stub_report(stdout, &probes);
    printf("%s: %u stubs and %u near-match functions hit; the game thread %s\n", mode, stubs, probes,
           nw_game_state_name(state));
}

static int nw_boot_check(const char *data_dir, int vblanks, int loads, NwClockMode mode, bool hardware_speed)
{
    int code = nw_boot_prepare(data_dir, mode, hardware_speed);
    if (code) {
        return code;
    }
    nw_stub_set_continue(true);
    nw_ee_park_after((unsigned)loads);
    if (!nw_game_start(nw_game_main, 1, nw_game_argv)) {
        fprintf(stderr, NW_APP_NAME ": no game thread: %s\n", SDL_GetError());
        return 1;
    }
    uint64_t start = SDL_GetTicks(), edge = 0, first = 0;
    int level = nw_level_arrival();
    uint64_t watchdog = level >= 0 ? NW_LEVEL_CHECK_MS : NW_BOOT_CHECK_MS;
    bool swapped = false;
    NwGameState state = NW_GAME_RUNNING;
    while (state == NW_GAME_RUNNING && SDL_GetTicks() - start < watchdog) {
        edge = nw_game_watch_edge(edge, 100);
        if (!swapped && (level >= 0 ? nw_level_arrived() : nw_game_swaps() > 0)) {
            swapped = true;
            first = edge;
        }
        if (!loads && swapped && edge - first >= (uint64_t)vblanks) {
            break;
        }
        state = nw_game_state();
    }
    uint64_t ended_at[8];
    unsigned ended = nw_ee_terminated(ended_at, 8);
    bool reached = loads ? swapped && ended >= (unsigned)loads : swapped && edge - first >= (uint64_t)vblanks;
    bool parked = nw_game_parked();
    state = nw_game_state();
    fflush(stderr);
    if (swapped) {
        printf("boot-check: %s at vblank %llu, %llu frames by vblank %llu\n",
               level >= 0 ? "level running" : "first swap", (unsigned long long)first,
               (unsigned long long)nw_game_swaps(), (unsigned long long)edge);
    } else {
        printf("boot-check: %s in %llu vblanks\n", level >= 0 ? "level not running" : "no swap",
               (unsigned long long)edge);
    }
    for (unsigned i = 0; i < ended && i < 8; i++) {
        printf("boot-check: load %u ended at vblank %llu\n", i + 1, (unsigned long long)ended_at[i]);
    }
    const NwLevelArrival *arrivals;
    for (int i = 0, n = nw_level_arrivals(&arrivals); i < n; i++) {
        printf("boot-check: level %d ran with %zu scene GPU objects alive\n", arrivals[i].level, arrivals[i].gpu);
    }
    nw_print_stub_hits("boot-check", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    } else if (!reached && state == NW_GAME_PARKED) {
        fprintf(stderr, NW_APP_NAME ": boot-check: the watchdog stopped the game after %llu ms\n",
                (unsigned long long)watchdog);
    }
    return nw_boot_exit(reached && state == NW_GAME_PARKED ? 0 : 1, parked);
}

static bool nw_window_ready(NwPlatform *p, const char *data_dir)
{
    char *root = nw_data_root(data_dir);
    bool ok = root && nw_backend_init();
    if (ok) {
        ok = nw_debug_init(p, root);
        if (!ok) {
            nw_backend_shutdown();
        }
    }
    SDL_free(root);
    if (!ok) {
        nw_log_error(NW_LOG_APP, "%s", SDL_GetError());
        nw_platform_shutdown(p);
    }
    return ok;
}

static bool nw_window_open(NwPlatform *p, const char *data_dir)
{
    return nw_platform_init(p, NW_APP_NAME, 1280, 720) && nw_window_ready(p, data_dir);
}

static void nw_window_close(NwPlatform *p)
{
    nw_debug_shutdown();
    nw_backend_shutdown();
    nw_platform_shutdown(p);
}

static void nw_boot_settings(const char *data_dir, NwSettings *s, bool save)
{
    char *root = nw_data_root(data_dir);
    if (root && !(save ? nw_settings_save(s, root) : nw_settings_load(s, root))) {
        nw_log_warn(NW_LOG_APP, "%s%s: %s", root, NW_PATHS_SETTINGS, SDL_GetError());
    }
    SDL_free(root);
}

/* settings.toml keeps the player's fog */
static void nw_boot_store(const char *data_dir, const NwSettings *s, int fog_kept)
{
    NwSettings stored = *s;
    if (fog_kept >= 0) {
        stored.fog = fog_kept;
    }
    nw_boot_settings(data_dir, &stored, true);
}

/* Master and Voice are the port's buses; Music and SFX are the game's volumes, which its options menu writes too. */
static void nw_boot_audio(void *user)
{
    NwSettings *s = user;
    int music = nw_options_volume(true), sfx = nw_options_volume(false);

    nw_debug_slider_int("Master", &s->master_volume, 0, 100);
    if (nw_debug_slider_int("Music", &music, 0, 100)) {
        nw_options_set_volume(true, music);
    }
    if (nw_debug_slider_int("SFX", &sfx, 0, 100)) {
        nw_options_set_volume(false, sfx);
    }
    nw_debug_slider_int("Voice", &s->voice_volume, 0, 100);
    nw_debug_checkbox("Subtitles", &s->subtitles);
    nw_debug_checkbox("Subtitles on title screen", &s->title_subtitles);
}

static const char *const nw_movement_profiles[] = { "Retail", "N. Hanced", "Custom" };

static bool nw_boot_switch(const char *label, const char *help, bool *value)
{
    nw_debug_checkbox(label, value);
    nw_debug_tooltip(help);
    return *value;
}

static void nw_boot_flag(const char *label, const char *help, bool *value)
{
    nw_debug_indent(true);
    nw_debug_checkbox(label, value);
    nw_debug_tooltip(help);
    nw_debug_indent(false);
}

static void nw_boot_value(const char *label, const char *help, int *value, int lo, int hi)
{
    nw_debug_indent(true);
    nw_debug_slider_int(label, value, lo, hi);
    nw_debug_tooltip(help);
    nw_debug_indent(false);
}

static uint64_t nw_boot_copied;
static unsigned nw_boot_copied_parts;

static void nw_boot_profile(NwSettings *s, unsigned parts)
{
    nw_debug_label(nw_movement_profiles[nw_movement_profile(&s->movement)]);
    nw_debug_same_line(6.0f);
    if (nw_debug_button("Retail")) {
        s->movement = (NwMovement)NW_MOVEMENT_RETAIL;
    }
    nw_debug_same_line(0.0f);
    if (nw_debug_button("N. Hanced")) {
        s->movement = (NwMovement)NW_MOVEMENT_NHANCED;
    }
    nw_debug_same_line(0.0f);
    bool copied = nw_boot_copied_parts == parts && SDL_GetTicks() - nw_boot_copied < 1500;
    if (nw_debug_button(copied ? "Copied###copy" : "Copy###copy") && nw_profiles_copy(parts)) {
        nw_boot_copied = SDL_GetTicks();
        nw_boot_copied_parts = parts;
    }
    nw_debug_tooltip("copy this panel's settings to the clipboard for sharing");
}

#define NW_BOOT_SWITCH_ITEM(f, label, on_, help) on = nw_boot_switch(label, help, &c->f);
#define NW_BOOT_FLAG_ITEM(f, label, def, help)     if (on) {         nw_boot_flag(label, help, &c->f);     }
#define NW_BOOT_VALUE_ITEM(f, label, def, lo, hi, help)     if (on) {         nw_boot_value(label, help, &c->f, lo, hi);     }

static void nw_boot_movement(void *user)
{
    NwSettings *s = user;
    NwCrashMovement *c = &s->movement.crash;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_CRASH);
    NW_CRASH_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    nw_debug_slider_int("Monkey Bar Speed %", &s->movement.monkey_bar_speed, 100, 500);
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_coco(void *user)
{
    NwSettings *s = user;
    NwCocoMovement *c = &s->movement.coco;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_COCO);
    NW_COCO_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
}

static void nw_boot_mech(void *user)
{
    NwSettings *s = user;
    NwMechMovement *c = &s->movement.mech;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_MECH);
    NW_MECH_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_submarine(void *user)
{
    NwSettings *s = user;
    NwSubMovement *c = &s->movement.submarine;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_SUBMARINE);
    NW_SUB_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_jeep(void *user)
{
    NwSettings *s = user;
    NwJeepMovement *c = &s->movement.jeep;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_JEEP);
    NW_JEEP_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_atlas(void *user)
{
    NwSettings *s = user;
    NwAtlasMovement *c = &s->movement.atlasphere;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_ATLASPHERE);
    NW_ATLAS_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_firefly(void *user)
{
    NwSettings *s = user;
    NwFireflyMovement *c = &s->movement.firefly;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_FIREFLY);
    NW_FIREFLY_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_plane(void *user)
{
    NwSettings *s = user;
    NwPlaneMovement *c = &s->movement.plane;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_PLANE);
    NW_PLANE_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_spacefighter(void *user)
{
    NwSettings *s = user;
    NwSpaceMovement *c = &s->movement.spacefighter;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_SPACEFIGHTER);
    NW_SPACE_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_time_trial(void *user)
{
    NwSettings *s = user;
    NwTimeTrialMovement *c = &s->movement.time_trial;
    bool on = true;

    nw_boot_profile(s, 1u << NW_PART_TIME_TRIAL);
    NW_TT_MOVEMENT(NW_BOOT_SWITCH_ITEM, NW_BOOT_FLAG_ITEM, NW_BOOT_VALUE_ITEM)
    (void)on;
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

#undef NW_BOOT_SWITCH_ITEM
#undef NW_BOOT_FLAG_ITEM
#undef NW_BOOT_VALUE_ITEM

/* Applied by the game thread at its next swap. */
static void nw_boot_request(const NwSettings *s, bool hitboxes, bool boss_pics)
{
    nw_profile_request(&(NwProfile){ .movement = s->movement,
                                     .air_indicator = s->air_indicator,
                                     .boss_health = s->boss_health,
                                     .wombat = s->wombat,
                                     .hub_minigame = s->hub_minigame,
                                     .long_time_names = s->long_time_names,
                                     .relic_spin = s->relic_spin,
                                     .camera_stability = s->camera_stability,
                                     .hub_gem = s->hub_gem,
                                     .first_boss_cut = s->first_boss_cut,
                                     .vehicle_hints = s->vehicle_hints,
                                     .subtitles = s->subtitles,
                                     .title_subtitles = s->title_subtitles,
                                     .bug_fixes = s->bug_fixes,
                                     .hitboxes = hitboxes,
                                     .boss_pics = boss_pics });
}

static void nw_boot_camera(void *user)
{
    NwSettings *s = user;

    nw_debug_slider_int("Jump stability %", &s->camera_stability, 0, 100);
    nw_debug_tooltip("0 follows the jump as the PS2, 100 holds the height until he lands");
    if (nw_run_retail()) {
        nw_debug_text("this run plays retail: changes take effect at the next start");
    }
}

static void nw_boot_levels(void *user)
{
    static char filter[64];
    (void)user;
    nw_debug_filter("filter", filter, sizeof(filter));
    nw_debug_list_begin("levels", 16);
    for (int i = 0, shown = -1; i < nw_level_count(); i++) {
        int room, slot, level = nw_level_at(i, &room, &slot);
        const char *name = nw_level_name(level);
        if (!name || !SDL_strcasestr(name, filter)) {
            continue;
        }
        char label[80];
        if (room != shown) {
            SDL_snprintf(label, sizeof(label), "Warp Room %d", room);
            nw_debug_heading(room ? label : "Other");
            shown = room;
        }
        if (!room) {
            SDL_strlcpy(label, name, sizeof(label));
        } else if (slot == 5) {
            SDL_snprintf(label, sizeof(label), "Boss: %s", name);
        } else {
            SDL_snprintf(label, sizeof(label), "%d. %s", slot + 1, name);
        }
        if (nw_debug_selectable(label, level == nw_level_current())) {
            nw_level_load(level);
        }
    }
    nw_debug_list_end();
    int pending = nw_level_pending();
    if (pending >= 0) {
        nw_debug_text("entering %s", nw_level_name(pending));
    }
}

static void nw_boot_cutscenes(void *user)
{
    (void)user;
    int current = nw_cutscene_current();
    const char *shown = NULL;
    nw_debug_list_begin("cutscenes", 16);
    for (int i = 0; i < nw_cutscene_count(); i++) {
        const char *group;
        const char *name = nw_cutscene_name(i, &group);
        if (!name) {
            continue;
        }
        if (!shown || SDL_strcmp(shown, group) != 0) {
            nw_debug_heading(group);
            shown = group;
        }
        char label[64];
        SDL_snprintf(label, sizeof(label), "%s##%d", name, i);
        if (nw_debug_selectable(label, i == current)) {
            nw_cutscene_play(i);
        }
    }
    nw_debug_list_end();
    const char *group;
    const char *name = nw_cutscene_name(nw_cutscene_pending(), &group);
    if (name) {
        nw_debug_text("starting %s: %s", group, name);
    }
}

static void nw_boot_minigame(void *user)
{
    (void)user;
    nw_minigame_start();
}

static void nw_boot_fatal(SDL_Window *window, const char *data_dir)
{
    char *root = nw_data_root(data_dir);
    char msg[1024];
    SDL_snprintf(msg, sizeof(msg), "The game stopped because of an error:\n\n%s\n\nDetails are in %s%s",
                 nw_game_error(), root ? root : "", NW_PATHS_LOG);
    SDL_free(root);
    SDL_SetWindowFullscreen(window, false);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_APP_NAME, msg, window);
}

/* -1 takes settings.toml's; window: the setup's to take over (it keeps the keyboard focus), NULL to open one. */
static int nw_boot(const char *data_dir, int hardware_speed, float aspect, NwPlatform *window)
{
    char *root = nw_data_root(data_dir);
    if (root && !nw_log_file_arg && !nw_log_open(root)) {
        nw_log_error(NW_LOG_APP, "%s%s: %s", root, NW_PATHS_LOG, SDL_GetError());
    }
    SDL_free(root);
    SDL_Time started = 0;
    SDL_GetCurrentTime(&started);
    /* Not for routes and recordings, which test runs use. */
    bool presence = !nw_pad_script && !nw_run_level;
    NwSettings settings;
    nw_settings_defaults(&settings);
    nw_boot_settings(data_dir, &settings, false);
    if (hardware_speed >= 0) {
        settings.hardware_speed = hardware_speed;
    }
    if (aspect >= 0.0f) {
        settings.aspect = aspect;
    }
    if (!nw_log_arg && SDL_strcmp(settings.log, "info") != 0) {
        if (nw_log_configure(settings.log)) {
            nw_log_info(NW_LOG_APP, "log: %s", settings.log);
        } else {
            nw_log_warn(NW_LOG_APP, "settings: [log] level \"%s\" is not one; info", settings.log);
        }
    }
    if (!settings.minidump) {
        nw_crash_dumps(NULL);
    }
    nw_crash_arm();
    nw_update_cleanup(SDL_GetBasePath());
    nw_platform_gl_debug(settings.gl_debug || nw_gl_debug_arg);
    if (SDL_strcmp(settings.gpu, "auto") != 0) {
        nw_gpu_prefer(settings.gpu);
    }
    char aspect_name[32];
    nw_log_info(NW_LOG_APP,
                "settings: hardware speed %s, aspect %s, interpolation %s, movement %s, "
                "air indicator %s, boss health %s, camera stability %d%%",
                settings.hardware_speed ? "on" : "off",
                nw_settings_aspect_name(settings.aspect, aspect_name, sizeof(aspect_name)),
                settings.interpolation ? "on" : "off", nw_movement_profiles[nw_movement_profile(&settings.movement)],
                settings.air_indicator ? "on" : "off", settings.boss_health ? "on" : "off", settings.camera_stability);
    int code = nw_boot_prepare(data_dir, NW_CLOCK_REALTIME, settings.hardware_speed);
    if (code) {
        if (window) {
            nw_platform_shutdown(window);
        }
        return code;
    }
    NwPlatform platform;
    if (window ? !nw_window_ready(window, data_dir) : !nw_window_open(&platform, data_dir)) {
        return 1;
    }
    if (window) {
        platform = *window;
    }
    if (nw_run_retail()) {
        nw_log_info(NW_LOG_APP, "%s plays the retail game: no [game] switches",
                    nw_pad_script ? "--pad-script" : "--revision 1.03");
    } else {
        nw_profile_apply(&(NwProfile){ .title_menu = !settings.retail_menu,
                                       .movement = settings.movement,
                                       .air_indicator = settings.air_indicator,
                                       .boss_health = settings.boss_health,
                                       .wombat = settings.wombat,
                                       .hub_minigame = settings.hub_minigame,
                                       .long_time_names = settings.long_time_names,
                                       .relic_spin = settings.relic_spin,
                                       .camera_stability = settings.camera_stability,
                                       .hub_gem = settings.hub_gem,
                                       .first_boss_cut = settings.first_boss_cut,
                                       .vehicle_hints = settings.vehicle_hints,
                                       .subtitles = settings.subtitles,
                                       .title_subtitles = settings.title_subtitles,
                                       .bug_fixes = settings.bug_fixes,
                                       .boss_pics = true });
    }
    nw_wide_set(nw_video_start(&platform, &settings), settings.hud);
    nw_debug_panel("Settings", "Audio", nw_boot_audio, &settings, false);
    nw_control_panel_start(&settings.controls);
    if (presence) {
        nw_debug_toggle("Settings", "Discord Rich Presence", &settings.discord);
        nw_debug_toggle("Settings", "Check for updates", &settings.updates);
    }
    nw_debug_panel("Gameplay", "Camera", nw_boot_camera, &settings, false);
    nw_debug_toggle("Gameplay", "Air indicator", &settings.air_indicator);
    nw_debug_toggle("Gameplay", "Boss health", &settings.boss_health);
    nw_debug_toggle("Gameplay", "Loading minigame in the hub", &settings.hub_minigame);
    nw_debug_toggle("Gameplay", "Time trial names of up to 8 letters", &settings.long_time_names);
    nw_debug_toggle("Gameplay", "Turning relic on time trial records", &settings.relic_spin);
    nw_debug_toggle("Gameplay", "Aku Aku after the first boss", &settings.first_boss_cut);
    nw_debug_toggle("Gameplay", "Vehicle controls", &settings.vehicle_hints);
    nw_debug_toggle("Gameplay", "PS2 bug fixes", &settings.bug_fixes);
    if (!nw_run_retail()) {
        nw_cheat_panel_start();
    }
    {
        char *root = nw_data_root(data_dir);
        char *saves = root ? nw_paths_dir(root, NW_PATHS_SAVES, false) : NULL;
        nw_saveimport_start(platform.window, saves);
        nw_saveexport_start(platform.window, saves, nw_shim_file_vfs());
        SDL_free(saves);
        SDL_free(root);
    }
    nw_debug_panel_key("N. Hanced Movement", "Crash", SDL_SCANCODE_F2, nw_boot_movement, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Coco", SDL_SCANCODE_F10, nw_boot_coco, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Mech", SDL_SCANCODE_F3, nw_boot_mech, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Submarine", SDL_SCANCODE_F4, nw_boot_submarine, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Jeep", SDL_SCANCODE_F5, nw_boot_jeep, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Atlasphere", SDL_SCANCODE_F6, nw_boot_atlas, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Firefly", SDL_SCANCODE_F9, nw_boot_firefly, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Plane", SDL_SCANCODE_F8, nw_boot_plane, &settings);
    nw_debug_panel_key("N. Hanced Movement", "Space Fighter", SDL_SCANCODE_F7, nw_boot_spacefighter, &settings);
    nw_debug_panel("N. Hanced Movement", "Time Trial", nw_boot_time_trial, &settings, false);
    {
        char *root = nw_data_root(data_dir);
        nw_profiles_start(platform.window, root, &settings.movement);
        SDL_free(root);
    }
    nw_debug_panel("Debug", "Load Level", nw_boot_levels, NULL, false);
    nw_debug_panel("Debug", "Play Cutscene", nw_boot_cutscenes, NULL, false);
    bool hitboxes = false, hitboxes_shown = false, boss_pics = true, boss_pics_shown = true;
    if (!nw_run_retail()) {
        nw_debug_action("Debug", "Start Minigame", SDL_SCANCODE_UNKNOWN, nw_boot_minigame, NULL);
    }
    nw_debug_toggle("Debug", "WOMBAT cheat", &settings.wombat);
    if (!nw_run_retail()) {
        nw_debug_toggle("Debug", "Hitboxes", &hitboxes);
        nw_debug_toggle("Debug", "Boss pictures in the hub", &boss_pics);
    }
    nw_backend_debug_menu();
    {
        char *root = nw_data_root(data_dir);
        nw_help_start(platform.window, root);
        SDL_free(root);
    }
    NwSettings applied = settings;
    bool unsaved = false;
    uint64_t changed = 0;
    nw_audio_set_buses(settings.master_volume, settings.voice_volume);
    nw_input_set_controls(&settings.controls);
    if (!nw_audio_open()) {
        nw_log_warn(NW_LOG_AUDIO, "no sound: %s", SDL_GetError());
    }
    nw_stub_set_continue(true);
    if (!nw_game_start(nw_game_main, 1, nw_game_argv)) {
        nw_log_error(NW_LOG_GAME, "no game thread: %s", SDL_GetError());
        nw_audio_close();
        nw_window_close(&platform);
        return 1;
    }
    if (presence && !nw_discord_start(&(NwDiscordConfig){ .app_id = NW_DISCORD_APP_ID })) {
        nw_log_warn(NW_LOG_NET, "discord: %s", SDL_GetError());
    }
    nw_discord_enable(settings.discord);
    nw_update_enable(presence && settings.updates, nw_build_version);
    char title[96];
    SDL_snprintf(title, sizeof(title), NW_APP_NAME " v%s", nw_build_name);
    SDL_SetWindowTitle(platform.window, title);
    uint64_t presented = 0;
    NwPacer pacer = { 0 };
    int fog_kept = -1;
    bool running = true;
    while (running) {
        nw_pacer_wait(&pacer, nw_video_fps(&settings));
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!nw_debug_event(&event) && event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            nw_input_event(&event);
            nw_video_event(&event);
            nw_profiles_event(&event);
        }
        nw_input_poll(!nw_debug_wants_keyboard());
        if (nw_game_state() == NW_GAME_FATAL) {
            nw_discord_stop();
            nw_boot_fatal(platform.window, data_dir);
            running = false;
        }
        NwPresenceSnap snap;
        nw_presence_poll(&snap);
        if (snap.valid) {
            nw_settings_level_fog(&settings, snap.cutmovie == 0 ? -1 : snap.level, &fog_kept);
        }
        if (!nw_settings_equal(&settings, &applied)) {
            if (settings.discord != applied.discord) {
                nw_discord_enable(settings.discord);
            }
            if (settings.updates != applied.updates) {
                nw_update_enable(presence && settings.updates, nw_build_version);
            }
            nw_audio_set_buses(settings.master_volume, settings.voice_volume);
            nw_input_set_controls(&settings.controls);
            if (!nw_run_retail() &&
                (SDL_memcmp(&settings.movement, &applied.movement, sizeof(settings.movement)) != 0 ||
                 settings.air_indicator != applied.air_indicator ||
                 settings.boss_health != applied.boss_health ||
                 settings.wombat != applied.wombat ||
                 settings.hub_minigame != applied.hub_minigame ||
                 settings.long_time_names != applied.long_time_names ||
                 settings.relic_spin != applied.relic_spin ||
                 settings.camera_stability != applied.camera_stability ||
                 settings.hub_gem != applied.hub_gem ||
                 settings.first_boss_cut != applied.first_boss_cut ||
                 settings.vehicle_hints != applied.vehicle_hints ||
                 settings.subtitles != applied.subtitles ||
                 settings.title_subtitles != applied.title_subtitles ||
                 settings.bug_fixes != applied.bug_fixes)) {
                nw_boot_request(&settings, hitboxes, boss_pics);
                hitboxes_shown = hitboxes;
                boss_pics_shown = boss_pics;
            }
            nw_video_apply(&settings, &applied);
            applied = settings;
            unsaved = true;
            changed = SDL_GetTicks();
        }
        if (hitboxes != hitboxes_shown || boss_pics != boss_pics_shown) {
            nw_boot_request(&settings, hitboxes, boss_pics);
            hitboxes_shown = hitboxes;
            boss_pics_shown = boss_pics;
        }
        if (unsaved && SDL_GetTicks() - changed >= NW_SETTINGS_SAVE_MS) {
            nw_boot_store(data_dir, &settings, fog_kept);
            unsaved = false;
        }
        if (presence && SDL_GetTicks() - presented >= 1000) {
            NwActivity activity;
            nw_presence_activity(&snap, nw_build_name, started / SDL_NS_PER_MS, &activity);
            nw_discord_set(&activity);
            presented = SDL_GetTicks();
        }
        if (nw_update_poll(platform.window)) {
            running = false;
        }
        int width, height;
        nw_display_size(platform.window, &width, &height);
        nw_backend_frame(width, height, settings.interpolation ? nw_game_edge_time() : -1.0);
        nw_debug_frame();
        SDL_GL_SwapWindow(platform.window);
    }
    nw_discord_stop();
    if (unsaved) {
        nw_boot_store(data_dir, &settings, fog_kept);
    }
    bool parked = nw_game_parked();
    NwGameState state = nw_game_state();
    fflush(stderr);
    nw_print_stub_hits("boot", state);
    if (state == NW_GAME_FATAL) {
        nw_log_error(NW_LOG_GAME, "%s", nw_game_error());
    }
    nw_input_shutdown();
    nw_audio_close();
    nw_saveimport_stop();
    nw_saveexport_stop();
    nw_profiles_stop();
    nw_help_stop();
    nw_window_close(&platform);
    return nw_boot_exit(state == NW_GAME_FATAL ? 1 : 0, parked);
}

static int nw_capture(const char *data_dir, const char *list, const char *dir, bool hardware_speed)
{
    char *root = nw_data_root(data_dir);
    char *out = dir ? nw_paths_dir(dir, NULL, true) : root ? nw_paths_dir(root, NW_PATHS_CAPTURES, true) : NULL;
    SDL_free(root);
    if (!nw_capture_start(list) || !out) {
        fprintf(stderr, NW_APP_NAME ": --capture: no vblanks in %s or no folder for them\n", list);
        SDL_free(out);
        return 2;
    }
    int code = nw_boot_prepare(data_dir, NW_CLOCK_STEPPED, hardware_speed);
    NwPlatform platform;
    if (code || !nw_window_open(&platform, data_dir)) {
        SDL_free(out);
        return code ? code : 1;
    }
    SDL_HideWindow(platform.window);
    SDL_GL_SetSwapInterval(0);
    nw_stub_set_continue(true);
    if (!nw_game_start(nw_game_main, 1, nw_game_argv)) {
        fprintf(stderr, NW_APP_NAME ": no game thread: %s\n", SDL_GetError());
        nw_window_close(&platform);
        SDL_free(out);
        return 1;
    }
    uint64_t start = SDL_GetTicks();
    bool saved = true;
    while (!nw_capture_finished() && nw_game_state() == NW_GAME_RUNNING &&
           SDL_GetTicks() - start < NW_CAPTURE_MS) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        int width, height, vblank;
        SDL_GetWindowSizeInPixels(platform.window, &width, &height);
        const NwPacket *p = nw_backend_frame(width, height, -1.0);
        uint64_t seq = nw_capture_wanted(&vblank);
        if (seq && p && p->seq == seq) {
            char *path = NULL;
            SDL_asprintf(&path, "%s%d.png", out, vblank);
            if (!path || !nw_backend_save(path)) {
                fprintf(stderr, NW_APP_NAME ": capture: %s: %s\n", path ? path : "?", SDL_GetError());
                saved = false;
            }
            SDL_free(path);
            nw_capture_release();
        }
        SDL_GL_SwapWindow(platform.window);
    }
    bool finished = nw_capture_finished();
    bool parked = nw_game_parked();
    NwGameState state = nw_game_state();
    fflush(stderr);
    nw_print_stub_hits("capture", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    } else if (!finished) {
        fprintf(stderr, NW_APP_NAME ": capture: the game stopped or the watchdog ran out before the last frame\n");
    }
    nw_window_close(&platform);
    SDL_free(out);
    return nw_boot_exit(finished && saved && state == NW_GAME_PARKED ? 0 : 1, parked);
}

static int nw_dump_run(const char *data_dir, const char *list, const char *dir, bool heap, bool hardware_speed)
{
    char *root = nw_data_root(data_dir);
    char *out = dir ? nw_paths_dir(dir, NULL, true) : root ? nw_paths_dir(root, NW_PATHS_DUMPS, true) : NULL;
    SDL_free(root);
    if (!out || !nw_dump_start(list, out, heap)) {
        fprintf(stderr, NW_APP_NAME ": --dump: no vblanks in %s or no folder for them\n", list);
        SDL_free(out);
        return 2;
    }
    SDL_free(out);
    int code = nw_boot_prepare(data_dir, NW_CLOCK_STEPPED, hardware_speed);
    if (code) {
        return code;
    }
    nw_stub_set_continue(true);
    if (!nw_game_start(nw_game_main, 1, nw_game_argv)) {
        fprintf(stderr, NW_APP_NAME ": no game thread: %s\n", SDL_GetError());
        return 1;
    }
    uint64_t start = SDL_GetTicks(), edge = 0;
    while (!nw_dump_finished() && nw_game_state() == NW_GAME_RUNNING && SDL_GetTicks() - start < NW_DUMP_MS) {
        edge = nw_game_watch_edge(edge, 100);
    }
    bool finished = nw_dump_finished();
    bool parked = nw_game_parked();
    NwGameState state = nw_game_state();
    unsigned failed, written = nw_dump_written(&failed);
    fflush(stderr);
    printf("dump: %u states written, %u failed\n", written, failed);
    nw_print_stub_hits("dump", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    } else if (!finished) {
        fprintf(stderr, NW_APP_NAME ": dump: the game stopped or the watchdog ran out before the last vblank\n");
    }
    return nw_boot_exit(finished && !failed && state == NW_GAME_PARKED ? 0 : 1, parked);
}

static const NwManifestEntry *nw_scene_entry(const char *path)
{
    char name[NW_VFS_PATH_MAX];
    SDL_strlcpy(name, path, sizeof(name));
    for (char *c = name; *c; c++) {
        *c = *c == '\\' ? '/' : *c;
    }
    const NwManifestEntry *e = nw_manifest_find(nw_shim_file_vfs()->manifest, name);
    if (!e) {
        fprintf(stderr, NW_APP_NAME ": %s is not on the disc\n", path);
        return NULL;
    }
    return e;
}

/* An empty path starts on the scene list. */
static int nw_scene(const char *data_dir, const char *path, int max_frames)
{
    int code = nw_boot_prepare(data_dir, NW_CLOCK_REALTIME, false);
    if (code) {
        return code;
    }
    const NwManifestEntry *e = *path ? nw_scene_entry(path) : NULL;
    if (*path && !e) {
        return 2;
    }
    if (e) {
        nw_scene_load(e->path);
    }
    NwPlatform platform;
    if (!nw_window_open(&platform, data_dir)) {
        return 1;
    }
    nw_stub_set_continue(true);
    if (!nw_game_start(nw_scene_main, 1, nw_game_argv)) {
        fprintf(stderr, NW_APP_NAME ": no game thread: %s\n", SDL_GetError());
        nw_window_close(&platform);
        return 1;
    }
    code = nw_view_scene(&platform, e ? e->path : NULL, max_frames);
    bool parked = nw_game_parked();
    NwGameState state = nw_game_state();
    fflush(stderr);
    nw_print_stub_hits("scene", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    }
    nw_window_close(&platform);
    return nw_boot_exit(code, parked);
}

/* One process per scene, as the decomp loads one scene a run; INTRO/FINAL and TITLES/TITLESPL are skipped. */
static int nw_scene_check_all(const char *exe, const char *data_dir)
{
    NwVfs vfs;
    char *root = nw_data_root(data_dir);
    if (!root || !nw_extract_has_disc(root) || !nw_vfs_init(&vfs, root)) {
        fprintf(stderr, NW_APP_NAME ": no imported disc (run --import)\n");
        SDL_free(root);
        return 2;
    }
    unsigned scenes = 0, skipped = 0, failed = 0;
    for (size_t i = 0; i < vfs.manifest->count; i++) {
        const NwManifestEntry *e = &vfs.manifest->entries[i];
        size_t len = strlen(e->path), size = 0;
        bool ghg = len >= 4 && SDL_strcasecmp(e->path + len - 4, ".GHG") == 0;
        if (len < 4 || (!ghg && SDL_strcasecmp(e->path + len - 4, ".GSC") != 0) || (e->flags & NW_MANIFEST_SKIP)) {
            continue;
        }
        bool drawn = ghg;
        if (!ghg) {
            uint8_t *data = nw_read_scene(&vfs, e, &size);
            NwGsc g;
            drawn = data && nw_gsc_open(&g, data, size);
            if (drawn) {
                drawn = !g.truncated && g.nobjects > 0;
                nw_gsc_close(&g);
            }
            SDL_free(data);
        }
        scenes++;
        if (!drawn) {
            printf("scene-check: %s: skipped, nothing to draw\n", e->path);
            skipped++;
            continue;
        }
        const char *args[] = { exe, "--scene-check", e->path, data_dir ? "--data-dir" : NULL, data_dir, NULL };
        SDL_Process *child = SDL_CreateProcess(args, false);
        int code = 1;
        if (!child || !SDL_WaitProcess(child, true, &code)) {
            fprintf(stderr, NW_APP_NAME ": %s: %s\n", e->path, SDL_GetError());
        }
        SDL_DestroyProcess(child);
        failed += code != 0;
    }
    printf("scene-check: %u scenes, %u skipped, %u failed\n", scenes, skipped, failed);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return failed || scenes == skipped ? 1 : 0;
}

static int nw_scene_check(const char *exe, const char *data_dir, const char *path)
{
    if (strcmp(path, "all") == 0) {
        return nw_scene_check_all(exe, data_dir);
    }
    int code = nw_boot_prepare(data_dir, NW_CLOCK_REALTIME, false);
    if (code) {
        return code;
    }
    const NwManifestEntry *e = nw_scene_entry(path);
    if (!e) {
        return 2;
    }
    nw_scene_load(e->path);
    nw_stub_set_continue(true);
    if (!nw_game_start(nw_scene_main, 1, nw_game_argv)) {
        fprintf(stderr, NW_APP_NAME ": no game thread: %s\n", SDL_GetError());
        return 1;
    }
    uint64_t start = SDL_GetTicks(), edge = 0;
    while (nw_game_state() == NW_GAME_RUNNING && nw_game_swaps() < NW_SCENE_CHECK_FRAMES &&
           SDL_GetTicks() - start < NW_BOOT_CHECK_MS) {
        edge = nw_game_watch_edge(edge, 100);
    }
    bool parked = nw_game_parked();
    NwGameState state = nw_game_state();
    const NwPacket *p = nw_packet_take();
    uint32_t draws = 0;
    for (uint32_t i = 0; p && i < p->ncmds; i++) {
        draws += p->cmds[i].op == NW_CMD_DRAW || p->cmds[i].op == NW_CMD_SKIN;
    }
    fflush(stderr);
    printf("scene-check: %s: %llu frames, %u draws, %u materials, %u views\n", e->path,
           (unsigned long long)nw_game_swaps(), draws, p ? p->nmtls : 0, p ? p->nviews : 0);
    nw_print_stub_hits("scene-check", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    }
    return nw_boot_exit(state == NW_GAME_PARKED && draws > 0 ? 0 : 1, parked);
}
#endif

/* -1 when the game should start in the still open window, else the exit code with the window closed. */
static int nw_setup(const char *data_dir, int max_frames, NwPlatform *window)
{
    NwPlatform platform;
    if (!nw_platform_init(&platform, NW_APP_NAME, 1280, 720)) {
        return 1;
    }
    char *root = nw_data_root(data_dir);
    if (!root || !nw_ui_init(&platform, false)) {
        char msg[512];
        SDL_snprintf(msg, sizeof(msg), "%s:\n%s", root ? "The interface could not start" : "No data folder",
                     SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_APP_NAME, msg, platform.window);
        SDL_free(root);
        nw_platform_shutdown(&platform);
        return 1;
    }

    NwWizard *wizard = nw_wizard_create(platform.window, root);
    NwWizardResult result = wizard ? NW_WIZARD_OPEN : NW_WIZARD_QUIT;
    int frames = 0;
    while (result == NW_WIZARD_OPEN) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            nw_ui_event(&event);
            nw_wizard_event(wizard, &event);
            if (event.type == SDL_EVENT_QUIT) {
                result = NW_WIZARD_QUIT;
            }
        }
        if (SDL_GetWindowFlags(platform.window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        int width, height;
        SDL_GetWindowSizeInPixels(platform.window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.05f, 0.08f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        nw_ui_begin();
        if (result == NW_WIZARD_OPEN) {
            result = nw_wizard_draw(wizard);
        }
        nw_ui_end();
        SDL_GL_SwapWindow(platform.window);

        if (max_frames > 0 && ++frames >= max_frames) {
            result = NW_WIZARD_QUIT;
        }
    }

    nw_wizard_destroy(wizard);
    nw_ui_shutdown();
    SDL_free(root);
    if (result == NW_WIZARD_CLOSED) {
        *window = platform;
        return -1;
    }
    nw_platform_shutdown(&platform);
    return 0;
}

int main(int argc, char *argv[])
{
    int max_frames = 0; /* 0 = run until the window is closed */
    const char *import = NULL, *import_save = NULL, *data_dir = NULL, *scene = NULL, *scene_check = NULL;
    const char *export_card = NULL, *export_slots = NULL;
    const char *capture = NULL, *capture_dir = NULL;
    const char *dump = NULL, *dump_dir = NULL;
    const char *poke = NULL;
    const char *dump_after = NULL;
    bool dump_heap = true;
    bool verify = false, setup = false, check_data = false, check_scenes = false, check_disc = false;
    bool check_canon = false;
    bool stepped = false, trace_sound = false;
    int boot_check = 0; /* vblanks after the first swap; 0 = no --boot-check */
    int boot_loads = 0;
    int hardware_speed = -1; /* -1: as the mode has it */
    const char *aspect_arg = NULL;
    const char *crash_test = NULL;

    nw_log_init(argc, argv);
    nw_update_args(argc, argv);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf(NW_APP_NAME " %s\n", nw_build_id);
            return 0;
        }
        if ((strcmp(argv[i], "--import") == 0 || strcmp(argv[i], "--import-save") == 0 ||
             strcmp(argv[i], "--data-dir") == 0 ||
             strcmp(argv[i], "--scene-check") == 0 || strcmp(argv[i], "--capture") == 0 ||
             strcmp(argv[i], "--capture-dir") == 0 || strcmp(argv[i], "--hardware-speed") == 0 ||
             strcmp(argv[i], "--dump") == 0 || strcmp(argv[i], "--dump-dir") == 0 ||
             strcmp(argv[i], "--poke") == 0 || strcmp(argv[i], "--dump-after") == 0 ||
             strcmp(argv[i], "--level") == 0 || strcmp(argv[i], "--pad-script") == 0 ||
             strcmp(argv[i], "--aspect") == 0 || strcmp(argv[i], "--revision") == 0 ||
             strcmp(argv[i], "--overlay") == 0 || strcmp(argv[i], "--log") == 0 ||
             strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "--crash-test") == 0) &&
            i + 1 >= argc) {
            fprintf(stderr, NW_APP_NAME ": %s needs a value\n", argv[i]);
            return 2;
        }
        if (strcmp(argv[i], "--export-save") == 0 && i + 2 >= argc) {
            fprintf(stderr, NW_APP_NAME ": --export-save needs a card and the slots\n");
            return 2;
        }
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--import") == 0) {
            import = argv[++i];
        } else if (strcmp(argv[i], "--import-save") == 0) {
            import_save = argv[++i];
        } else if (strcmp(argv[i], "--export-save") == 0) {
            export_card = argv[++i];
            export_slots = argv[++i];
        } else if (strcmp(argv[i], "--data-dir") == 0) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--verify") == 0) {
            verify = true;
        } else if (strcmp(argv[i], "--setup") == 0) {
            setup = true;
        } else if (strcmp(argv[i], "--check-data") == 0) {
            check_data = true;
        } else if (strcmp(argv[i], "--check-scenes") == 0) {
            check_scenes = true;
        } else if (strcmp(argv[i], "--check-disc") == 0) {
            check_disc = true;
        } else if (strcmp(argv[i], "--check-canon") == 0) {
            check_canon = true;
        } else if (strcmp(argv[i], "--scene") == 0) {
            scene = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "";
        } else if (strcmp(argv[i], "--scene-check") == 0) {
            scene_check = argv[++i];
        } else if (strcmp(argv[i], "--boot") == 0) {
            /* the default now; kept for older shortcuts and scripts */
        } else if (strcmp(argv[i], "--boot-check") == 0) {
            boot_check = i + 1 < argc && argv[i + 1][0] != '-' ? atoi(argv[++i]) : 50;
            if (boot_check <= 0) {
                fprintf(stderr, NW_APP_NAME ": --boot-check takes a positive number of vblanks\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--loads") == 0) {
            boot_loads = i + 1 < argc ? atoi(argv[++i]) : 0;
            if (boot_loads <= 0) {
                fprintf(stderr, NW_APP_NAME ": --loads takes a positive number of background loads\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--stepped") == 0) {
            stepped = true;
        } else if (strcmp(argv[i], "--trace-sound") == 0) {
            trace_sound = true;
        } else if (strcmp(argv[i], "--capture") == 0) {
            capture = argv[++i];
        } else if (strcmp(argv[i], "--capture-dir") == 0) {
            capture_dir = argv[++i];
        } else if (strcmp(argv[i], "--dump") == 0) {
            dump = argv[++i];
        } else if (strcmp(argv[i], "--dump-dir") == 0) {
            dump_dir = argv[++i];
        } else if (strcmp(argv[i], "--dump-no-heap") == 0) {
            dump_heap = false;
        } else if (strcmp(argv[i], "--poke") == 0) {
            poke = argv[++i];
        } else if (strcmp(argv[i], "--dump-after") == 0) {
            dump_after = argv[++i];
        } else if (strcmp(argv[i], "--level") == 0) {
            nw_run_level = argv[++i];
        } else if (strcmp(argv[i], "--pad-script") == 0) {
            nw_pad_script = argv[++i];
        } else if (strcmp(argv[i], "--overlay") == 0) {
            if (nw_overlay_count == NW_VFS_OVERLAYS) {
                fprintf(stderr, NW_APP_NAME ": at most %d --overlay folders\n", NW_VFS_OVERLAYS);
                return 2;
            }
            nw_overlays[nw_overlay_count++] = argv[++i];
        } else if (strcmp(argv[i], "--hardware-speed") == 0) {
            const char *value = argv[++i];
            if (strcmp(value, "on") != 0 && strcmp(value, "off") != 0) {
                fprintf(stderr, NW_APP_NAME ": --hardware-speed takes on or off\n");
                return 2;
            }
            hardware_speed = strcmp(value, "on") == 0;
        } else if (strcmp(argv[i], "--aspect") == 0) {
            aspect_arg = argv[++i];
        } else if (strcmp(argv[i], "--revision") == 0) {
            if (!nw_rev_set(argv[++i])) {
                fprintf(stderr, NW_APP_NAME ": --revision takes 1.03 or late\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--log") == 0) {
            nw_log_arg = argv[++i];
        } else if (strcmp(argv[i], "--log-file") == 0) {
            nw_log_file_arg = argv[++i];
        } else if (strcmp(argv[i], "--gl-debug") == 0) {
            nw_gl_debug_arg = true;
        } else if (strcmp(argv[i], "--crash-test") == 0) {
            crash_test = argv[++i];
        }
    }
    if (nw_log_arg && !nw_log_configure(nw_log_arg)) {
        fprintf(stderr, NW_APP_NAME ": --log takes levels like info or info,gfx=debug,decomp=trace\n");
        return 2;
    }
    if (nw_log_file_arg && !nw_log_open_file(nw_log_file_arg)) {
        fprintf(stderr, NW_APP_NAME ": --log-file %s: %s\n", nw_log_file_arg, SDL_GetError());
        return 2;
    }
    nw_platform_gl_debug(nw_gl_debug_arg);
#ifdef NW_WITH_GAME
    nw_crash_on_state(nw_boot_crash_state);
    nw_game_on_fatal(nw_crash_fatal);
#endif
    if (crash_test) {
        char *root = data_dir ? nw_data_root(data_dir) : NULL;
        if (root) {
            nw_log_open(root);
        }
        SDL_free(root);
        nw_crash_arm();
        nw_crash_test(crash_test);
    }
    /* The checks, tests and comparisons read the retail disc. */
    if (nw_overlay_count && (boot_check || capture || dump || check_data || check_scenes || check_disc || check_canon ||
                             scene || scene_check || verify || import || import_save || export_card)) {
        fprintf(stderr, NW_APP_NAME ": --overlay is for the game window only\n");
        return 2;
    }
    if (import) {
        return nw_import(import, data_dir);
    }
    if (import_save || export_card) {
#ifdef NW_WITH_GAME
        return import_save ? nw_import_save(import_save, data_dir) : nw_export_save(export_card, export_slots, data_dir);
#else
        fprintf(stderr, NW_APP_NAME ": built without the decomp (NUWORLD_DECOMP=OFF)\n");
        return 2;
#endif
    }
    if (verify) {
        return nw_verify(data_dir);
    }
    NwPlatform window = { 0 };
    if (!(check_data || check_scenes || check_disc || check_canon || scene || scene_check || boot_check || capture ||
          dump)) {
        char *root = nw_data_root(data_dir);
        bool ask = setup || !root || !nw_extract_has_disc(root);
        SDL_free(root);
        int code = ask ? nw_setup(data_dir, max_frames, &window) : -1;
        if (code >= 0) {
            return code;
        }
    }
#ifdef NW_WITH_GAME
    if (check_scenes) {
        return nw_check_scenes(data_dir);
    }
    if (check_disc) {
        return nw_check_disc(data_dir);
    }
    if (check_canon) {
        return nw_check_canon(data_dir);
    }
    if (scene_check) {
        return nw_scene_check(argv[0], data_dir, scene_check);
    }
    if (scene) {
        return nw_scene(data_dir, scene, max_frames);
    }
    if (trace_sound) {
        nw_trace_sound_start();
    }
    if ((poke && !nw_poke_start(poke)) || (dump_after && !nw_dump_after(dump_after))) {
        return 2;
    }
    /* PCSX2 comparison runs keep its load times, the window takes settings.toml's, the checks load at once. */
    int speed = hardware_speed >= 0 ? hardware_speed : trace_sound || capture || dump ? 1 : -1;
    /* The checks and comparisons keep the PS2's 4:3 unless --aspect names another. */
    float aspect = -1.0f;
    if (aspect_arg && !nw_settings_aspect(aspect_arg, &aspect)) {
        fprintf(stderr, NW_APP_NAME ": --aspect takes auto, W:H or a ratio\n");
        return 2;
    }
    if (aspect > 0.0f) {
        nw_wide_set(aspect, 16.0f / 9.0f);
    }
    if (capture) {
        return nw_capture(data_dir, capture, capture_dir, speed > 0);
    }
    if (dump) {
        return nw_dump_run(data_dir, dump, dump_dir, dump_heap, speed > 0);
    }
    return check_data   ? nw_check_data(data_dir)
           : boot_check ? nw_boot_check(data_dir, boot_check, boot_loads,
                                        stepped ? NW_CLOCK_STEPPED : NW_CLOCK_REALTIME, speed > 0)
                        : nw_boot(data_dir, speed, aspect, window.window ? &window : NULL);
#else
    fprintf(stderr, NW_APP_NAME ": built without the decomp (NUWORLD_DECOMP=OFF)\n");
    return 2;
#endif
}
