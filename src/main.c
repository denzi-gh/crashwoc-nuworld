#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.h"
#include "audio/nusound.h"
#include "frontend/ui.h"
#include "frontend/wizard.h"
#include "gfx/gl.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "vfs/extract.h"
#ifdef NW_WITH_GAME
#include "data/image.h"
#include "frontend/debug.h"
#include "frontend/settings.h"
#include "gfx/backend.h"
#include "gfx/gsc.h"
#include "gfx/view.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/capture.h"
#include "port/dump.h"
#include "port/game.h"
#include "port/level.h"
#include "port/options.h"
#include "port/saveload.h"
#include "port/scene.h"
#include "port/trace.h"
#include "shim/shim.h"
#include "vfs/sha256.h"
#include "vfs/vfs.h"
#endif

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

/* --data-dir, else <exe dir>/user/ in portable mode, else the pref path; created with import/ and saves/. */
static char *nw_data_root(const char *data_dir)
{
    char *root = data_dir ? nw_paths_dir(data_dir, NULL, true) : nw_paths_data_root(SDL_GetBasePath(), NULL);
    if (root && !nw_paths_create_layout(root)) {
        SDL_free(root);
        root = NULL;
    }
    if (!root) {
        fprintf(stderr, NW_APP_NAME ": no data folder: %s\n", SDL_GetError());
    }
    return root;
}

/* Headless disc import; returns the exit code. */
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
    bool ok = nw_disc_open(&disc, image);
    if (ok) {
        ok = nw_iso_index(&iso, &disc);
        if (ok) {
            printf("  %s, %u sectors, %u files\n", nw_disc_format_name(disc.format), disc.sectors, iso.count);
            ok = nw_extract_disc(&disc, &iso, &nw_disc_manifest, root, nw_print_progress, &percent);
            if (percent >= 0) {
                printf("\n");
            }
            nw_iso_free(&iso);
        }
        nw_disc_close(&disc);
    }
    if (ok) {
        printf("Imported into %s" NW_DISC_DIR "/\n", root);
    } else {
        fflush(stdout);
        fprintf(stderr, NW_APP_NAME ": import failed: %s\n", SDL_GetError());
    }
    SDL_free(root);
    return ok ? 0 : 1;
}

/* Re-hashes the imported disc; returns the exit code. */
static int nw_verify(const char *data_dir)
{
    int percent = -1;
    char *root = nw_data_root(data_dir);
    if (!root) {
        return 1;
    }

    printf("Verifying %s" NW_DISC_DIR "/\n", root);
    bool ok = nw_extract_verify(root, &nw_disc_manifest, nw_print_progress, &percent);
    if (percent >= 0) {
        printf("\n");
    }
    if (ok) {
        printf("All files match PAL v" NW_DISC_VER "\n");
    } else {
        fflush(stdout);
        fprintf(stderr, NW_APP_NAME ": verify failed: %s\n", SDL_GetError());
    }
    SDL_free(root);
    return ok ? 0 : 1;
}

#ifdef NW_WITH_GAME
/* A PS2 save (a raw BESLES-50386slot00 or a .ps2 card image) as the game's save; returns the exit code: 2 when the
   card has no save. */
static int nw_import_save(const char *file, const char *data_dir)
{
    char *root = nw_data_root(data_dir);
    char *saves = root ? nw_paths_dir(root, NW_PATHS_SAVES, false) : NULL;
    NwSaveResult r = saves ? nw_saveload_import(saves, file) : NW_SAVE_FAILED;
    if (r == NW_SAVE_IMPORTED) {
        printf("Imported %s into %s\n", file, saves);
    } else if (root) {
        fprintf(stderr, NW_APP_NAME ": save import failed: %s\n", SDL_GetError());
    }
    SDL_free(saves);
    SDL_free(root);
    return r == NW_SAVE_IMPORTED ? 0 : r == NW_SAVE_NONE ? 2 : 1;
}

/* The boot ELF of the imported disc, checked against the manifest; NULL with SDL_GetError() otherwise. */
static void *nw_read_elf(const NwVfs *vfs, size_t *size)
{
    const NwManifestEntry *e = nw_manifest_find(&nw_disc_manifest, NW_DISC_BOOT);
    NwVfsFile *f = nw_vfs_open(vfs, "cdrom0:\\" NW_DISC_BOOT ";1", NW_VFS_READ);
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
        SDL_SetError(NW_DISC_BOOT " is not the PAL v" NW_DISC_VER " ELF (run --verify)");
        return NULL;
    }
    *size = (size_t)n;
    return data;
}

/* Builds the data image from the imported disc and reports it; returns the exit code (2: no usable data). */
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
    NwImageStats stats;
    char err[512];
    bool ok = nw_image_load(&nw_image_tables, elf, size, &stats, err, sizeof(err));
    nw_image_report(&stats, stdout);
    if (!ok) {
        fflush(stdout);
        fprintf(stderr, NW_APP_NAME ": data image: %s\n", err);
    } else {
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
    }
    nw_image_free();
    SDL_free(elf);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return ok ? 0 : 1;
}

/* The decomp's RNC unpacker (NuFileLoadBuffer's). */
void NuPPUnpack(unsigned char *src, unsigned char *out);

/* A .gsc of the imported disc, unpacked when it is RNC-packed; NULL with SDL_GetError() otherwise. */
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
    *size = (size_t)n;
    return data;
}

/* Every .gsc of the imported disc through the NU20 reader and the VIF decoder; exits 0, 1, or 2 without the disc. */
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
    for (size_t i = 0; i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
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

/* The decomp's main (port/units.toml renames it), started as retail's boot starts it. */
int nw_game_main(int argc, char **argv);

static char nw_game_arg0[] = "cdrom0:\\" NW_DISC_BOOT ";1";
static char *nw_game_argv[] = { nw_game_arg0, NULL };

#define NW_VBLANK_HZ 50                /* PAL */
#define NW_BOOT_CHECK_MS 30000         /* the watchdog of --boot-check; at the hardware speed BGLoadSfx alone takes 13 s */
#define NW_GAME_PARK_MS 1000           /* how long the game thread gets to reach a yield point and park */
#define NW_SCENE_CHECK_FRAMES 3        /* the frames --scene-check lets a scene draw */
#define NW_CAPTURE_MS 300000           /* the watchdog of --capture */
#define NW_DUMP_MS 3600000             /* the watchdog of --dump */
#define NW_LEVEL_CHECK_MS 600000       /* the watchdog of --boot-check with --level */

static const char *nw_run_level; /* --level */

/* What the game thread needs before it starts, in order: the data folder (its saves folder is the memory card), the
   file system (the sce file shim's too), newlib's state, the data image from the boot ELF, the vblank clock, the
   sound module on both (at the hardware speed or with instant loads), --level's request. The file system stays open
   for the game. Returns 0, else the exit code: 2 without usable data or level, 1 when the image or the clock
   fails. */
static int nw_boot_prepare(const char *data_dir, NwClockMode mode, bool hardware_speed)
{
    static NwVfs vfs;
    size_t size = 0;
    char *root = nw_data_root(data_dir);
    bool vfs_open = root && nw_vfs_init(&vfs, root);
    void *elf = vfs_open ? nw_read_elf(&vfs, &size) : NULL;
    char *saves = root ? nw_paths_dir(root, NW_PATHS_SAVES, false) : NULL;
    nw_saveload_init(saves);
    SDL_free(saves);
    SDL_free(root);
    if (!elf) {
        fprintf(stderr, NW_APP_NAME ": no data image: %s\n", SDL_GetError());
        if (vfs_open) {
            nw_vfs_shutdown(&vfs);
        }
        return 2;
    }
    nw_shim_file_init(&vfs);
    nw_newlib_init();
    NwImageStats stats;
    char err[512];
    bool ok = nw_image_load(&nw_image_tables, elf, size, &stats, err, sizeof(err));
    SDL_free(elf);
    if (!ok) {
        fprintf(stderr, NW_APP_NAME ": data image: %s\n", err);
        return 1;
    }
    if (!nw_game_init(mode, NW_VBLANK_HZ)) {
        fprintf(stderr, NW_APP_NAME ": no vblank clock: %s\n", SDL_GetError());
        return 1;
    }
    nw_nusound_init(&vfs, nw_game_us);
    nw_nusound_set_hardware_speed(hardware_speed);
    if (nw_run_level && !nw_level_start(nw_run_level)) {
        fprintf(stderr, NW_APP_NAME ": --level %s: no such level on the disc\n", nw_run_level);
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

/* Stops the game thread for the exit; one that does not reach a yield point in time is left running, and the
   process must then end with _Exit, not unwind under it. */
static bool nw_game_parked(void)
{
    NwGameState state = nw_game_state() == NW_GAME_RUNNING ? nw_game_stop(NW_GAME_PARK_MS) : nw_game_state();
    if (state == NW_GAME_RUNNING) {
        fprintf(stderr, NW_APP_NAME ": the game thread did not stop within %d ms\n", NW_GAME_PARK_MS);
        return false;
    }
    return true;
}

static int nw_boot_exit(int code, bool parked)
{
    if (!parked) {
        fflush(stdout);
        fflush(stderr);
        _Exit(code);
    }
    return code;
}

/* The stubs and near-match functions this run reached, in first-hit order (stdout; tools/stub_hits.py reads it). */
static void nw_print_stub_hits(const char *mode, NwGameState state)
{
    unsigned probes = 0;
    printf("%s: stub and near-match hits in first-hit order (order, stub|near, unit, function, retail address, "
           "hits):\n", mode);
    unsigned stubs = nw_stub_report(stdout, &probes);
    printf("%s: %u stubs and %u near-match functions hit; the game thread %s\n", mode, stubs, probes,
           nw_game_state_name(state));
}

/* Headless: runs the game with every stub hit logged and passed until `vblanks` vblanks after its first screen
   swap (with --level, after its level runs), or with `loads`, until the game parks itself after that many
   background loads; then prints the stub hits in first-hit order (stdout). Returns the exit code: 0 when it got there, 1 on a fatal error, an early return or
   the watchdog, 2 without usable data. */
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
    nw_print_stub_hits("boot-check", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    } else if (!reached && state == NW_GAME_PARKED) {
        fprintf(stderr, NW_APP_NAME ": boot-check: the watchdog stopped the game after %llu ms\n",
                (unsigned long long)watchdog);
    }
    return nw_boot_exit(reached && state == NW_GAME_PARKED ? 0 : 1, parked);
}

static bool nw_window_open(NwPlatform *p, const char *data_dir)
{
    char *root = nw_data_root(data_dir);
    if (!root || !nw_platform_init(p, NW_APP_NAME, 1280, 720)) {
        SDL_free(root);
        return false;
    }
    bool ok = nw_backend_init();
    if (ok) {
        ok = nw_debug_init(p, root);
        if (!ok) {
            nw_backend_shutdown();
        }
    }
    SDL_free(root);
    if (!ok) {
        fprintf(stderr, NW_APP_NAME ": %s\n", SDL_GetError());
        nw_platform_shutdown(p);
    }
    return ok;
}

static void nw_window_close(NwPlatform *p)
{
    nw_debug_shutdown();
    nw_backend_shutdown();
    nw_platform_shutdown(p);
}

/* The data folder's settings.toml read over s or written from it; a failure is reported and passed. */
static void nw_boot_settings(const char *data_dir, NwSettings *s, bool save)
{
    char *root = nw_data_root(data_dir);
    if (root && !(save ? nw_settings_save(s, root) : nw_settings_load(s, root))) {
        fprintf(stderr, NW_APP_NAME ": %s%s: %s\n", root, NW_PATHS_SETTINGS, SDL_GetError());
    }
    SDL_free(root);
}

/* The port's master and voice buses (settings.toml) around the game's own music and SFX volumes, which its options
   menu also writes, at Done. */
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
}

/* The disc's levels; a click takes Crash there at once. */
static void nw_boot_levels(void *user)
{
    static char filter[64];
    (void)user;
    nw_debug_filter("filter", filter, sizeof(filter));
    nw_debug_list_begin("levels", 16);
    for (int i = 0; i < nw_level_count(); i++) {
        const char *name = nw_level_name(i);
        if (name && SDL_strcasestr(name, filter) && nw_debug_selectable(name, i == nw_level_current())) {
            nw_level_load(i);
        }
    }
    nw_debug_list_end();
    int pending = nw_level_pending();
    if (pending >= 0) {
        nw_debug_text("entering %s", nw_level_name(pending));
    }
}

/* Windowed: the game runs (stubs logged and passed) in the window, which reports its frames in the title; at the
   end it prints the stub hits in first-hit order (stdout). hardware_speed -1 takes settings.toml's, which the debug
   menu changes and saves, like the buses. Returns the exit code. */
static int nw_boot(const char *data_dir, int hardware_speed)
{
    NwSettings settings;
    nw_settings_defaults(&settings);
    nw_boot_settings(data_dir, &settings, false);
    if (hardware_speed >= 0) {
        settings.hardware_speed = hardware_speed;
    }
    int code = nw_boot_prepare(data_dir, NW_CLOCK_REALTIME, settings.hardware_speed);
    if (code) {
        return code;
    }
    NwPlatform platform;
    if (!nw_window_open(&platform, data_dir)) {
        return 1;
    }
    nw_debug_toggle("Game", "Simulate original hardware speed", &settings.hardware_speed);
    nw_debug_panel("Game", "Audio", nw_boot_audio, &settings, false);
    nw_debug_panel("Game", "Load Level", nw_boot_levels, NULL, false);
    bool applied = settings.hardware_speed;
    int buses[2] = { settings.master_volume, settings.voice_volume };
    nw_audio_set_buses(buses[0], buses[1]);
    if (!nw_audio_open()) {
        fprintf(stderr, NW_APP_NAME ": no sound: %s\n", SDL_GetError());
    }
    nw_stub_set_continue(true);
    if (!nw_game_start(nw_game_main, 1, nw_game_argv)) {
        fprintf(stderr, NW_APP_NAME ": no game thread: %s\n", SDL_GetError());
        nw_audio_close();
        nw_window_close(&platform);
        return 1;
    }
    uint64_t titled = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!nw_debug_event(&event) && event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            nw_input_event(&event);
        }
        nw_input_poll(!nw_debug_wants_keyboard());
        if (settings.hardware_speed != applied) {
            applied = settings.hardware_speed;
            nw_nusound_set_hardware_speed(applied);
            nw_boot_settings(data_dir, &settings, true);
        }
        if (settings.master_volume != buses[0] || settings.voice_volume != buses[1]) {
            buses[0] = settings.master_volume;
            buses[1] = settings.voice_volume;
            nw_audio_set_buses(buses[0], buses[1]);
            nw_boot_settings(data_dir, &settings, true);
        }
        if (SDL_GetTicks() - titled >= 500) {
            char title[160];
            NwGameState state = nw_game_state();
            SDL_snprintf(title, sizeof(title), NW_APP_NAME " - game %s, frame %llu, vblank %llu",
                         nw_game_state_name(state), (unsigned long long)nw_game_swaps(),
                         (unsigned long long)nw_game_edges());
            SDL_SetWindowTitle(platform.window, title);
            titled = SDL_GetTicks();
        }
        int width, height;
        SDL_GetWindowSizeInPixels(platform.window, &width, &height);
        nw_backend_frame(width, height);
        nw_debug_frame();
        SDL_GL_SwapWindow(platform.window);
    }
    bool parked = nw_game_parked();
    NwGameState state = nw_game_state();
    fflush(stderr);
    nw_print_stub_hits("boot", state);
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, NW_APP_NAME ": %s\n", nw_game_error());
    }
    nw_input_shutdown();
    nw_audio_close();
    nw_window_close(&platform);
    return nw_boot_exit(state == NW_GAME_FATAL ? 1 : 0, parked);
}

/* --capture: the game on the stepped clock in a hidden window; the frames around the requested vblanks of the title
   cut go to dir as <vblank>.png, their label lines and then the stub hits to stdout. Returns 0 once every one is
   past, 1 on a failed save, a stopped game or the watchdog, 2 without usable data or vblanks. */
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
        const NwPacket *p = nw_backend_frame(width, height);
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

/* --dump: the game headless on the stepped clock; its state at the requested vblanks goes to dir as <vblank>.nwd
   (src/port/dump.h), then the stub hits to stdout. Returns 0 once every one is written, 1 on a failed one, a stopped
   game or the watchdog, 2 without usable data or vblanks. */
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

/* A .gsc or .ghg of the imported disc by its manifest path (either slash); NULL when it is not on the disc. */
static const NwManifestEntry *nw_scene_entry(const char *path)
{
    char name[NW_VFS_PATH_MAX];
    SDL_strlcpy(name, path, sizeof(name));
    for (char *c = name; *c; c++) {
        *c = *c == '\\' ? '/' : *c;
    }
    const NwManifestEntry *e = nw_manifest_find(&nw_disc_manifest, name);
    if (!e) {
        fprintf(stderr, NW_APP_NAME ": %s is not on the disc\n", path);
        return NULL;
    }
    return e;
}

/* The scene viewer: the decomp loads and draws the scene on the game thread (stubs logged and passed), the window
   shows its frames under a fly camera; at the end it prints the stub hits (stdout). Returns the exit code: 2 without
   the disc or the scene. An empty path starts on the scene list. */
static int nw_scene(const char *data_dir, const char *path, int max_frames)
{
    const NwManifestEntry *e = *path ? nw_scene_entry(path) : NULL;
    int code = e || !*path ? nw_boot_prepare(data_dir, NW_CLOCK_REALTIME, false) : 2;
    if (code) {
        return code;
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

/* Every .gsc and .ghg of the imported disc through --scene-check, one process each (the decomp loads one scene a
   run). Scenes the NU20 reader finds truncated or without objects, which the game never draws (INTRO/FINAL,
   TITLES/TITLESPL), are skipped. Exits 2 without the disc. */
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
    for (size_t i = 0; i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
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

/* Headless: the scene drawn for a few frames under the viewer's first camera; prints its last frame packet and the
   stub hits (stdout). Returns 0 when the packet holds draws, 1 when not or on a fatal error, 2 without the disc. */
static int nw_scene_check(const char *exe, const char *data_dir, const char *path)
{
    if (strcmp(path, "all") == 0) {
        return nw_scene_check_all(exe, data_dir);
    }
    const NwManifestEntry *e = nw_scene_entry(path);
    int code = e ? nw_boot_prepare(data_dir, NW_CLOCK_REALTIME, false) : 2;
    if (code) {
        return code;
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

int main(int argc, char *argv[])
{
    int max_frames = 0; /* 0 = run until the window is closed */
    const char *import = NULL, *import_save = NULL, *data_dir = NULL, *scene = NULL, *scene_check = NULL;
    const char *capture = NULL, *capture_dir = NULL; /* --capture <vblanks|file> [--capture-dir <dir>] */
    const char *dump = NULL, *dump_dir = NULL;       /* --dump <vblanks|file> [--dump-dir <dir>] [--dump-no-heap] */
    const char *poke = NULL;                         /* --poke <vblank:symbol=value,...> */
    const char *dump_after = NULL;                   /* --dump-after <symbol=value> */
    bool dump_heap = true;
    bool verify = false, setup = false, check_data = false, check_scenes = false, boot = false;
    bool stepped = false, trace_sound = false; /* --boot-check on a stepped clock, with the sound trace */
    int boot_check = 0; /* vblanks after the first swap; 0 = no --boot-check */
    int boot_loads = 0; /* --loads: background loads instead of vblanks */
    int hardware_speed = -1; /* --hardware-speed on|off; -1: as the mode has it */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf(NW_APP_NAME " %s\n", NUWORLD_VERSION);
            return 0;
        }
        if ((strcmp(argv[i], "--import") == 0 || strcmp(argv[i], "--import-save") == 0 ||
             strcmp(argv[i], "--data-dir") == 0 ||
             strcmp(argv[i], "--scene-check") == 0 || strcmp(argv[i], "--capture") == 0 ||
             strcmp(argv[i], "--capture-dir") == 0 || strcmp(argv[i], "--hardware-speed") == 0 ||
             strcmp(argv[i], "--dump") == 0 || strcmp(argv[i], "--dump-dir") == 0 ||
             strcmp(argv[i], "--poke") == 0 || strcmp(argv[i], "--dump-after") == 0 ||
             strcmp(argv[i], "--level") == 0) &&
            i + 1 >= argc) {
            fprintf(stderr, NW_APP_NAME ": %s needs a value\n", argv[i]);
            return 2;
        }
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--import") == 0) {
            import = argv[++i];
        } else if (strcmp(argv[i], "--import-save") == 0) {
            import_save = argv[++i];
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
        } else if (strcmp(argv[i], "--scene") == 0) {
            scene = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "";
        } else if (strcmp(argv[i], "--scene-check") == 0) {
            scene_check = argv[++i];
        } else if (strcmp(argv[i], "--boot") == 0) {
            boot = true;
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
        } else if (strcmp(argv[i], "--hardware-speed") == 0) {
            const char *value = argv[++i];
            if (strcmp(value, "on") != 0 && strcmp(value, "off") != 0) {
                fprintf(stderr, NW_APP_NAME ": --hardware-speed takes on or off\n");
                return 2;
            }
            hardware_speed = strcmp(value, "on") == 0;
        }
    }
    if (import) {
        return nw_import(import, data_dir);
    }
    if (import_save) {
#ifdef NW_WITH_GAME
        return nw_import_save(import_save, data_dir);
#else
        fprintf(stderr, NW_APP_NAME ": built without the decomp (NUWORLD_DECOMP=OFF)\n");
        return 2;
#endif
    }
    if (verify) {
        return nw_verify(data_dir);
    }
    if (check_data || check_scenes || scene || scene_check || boot || boot_check || capture || dump) {
#ifdef NW_WITH_GAME
        if (check_scenes) {
            return nw_check_scenes(data_dir);
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
        /* The runs compared with PCSX2's keep its load times and the game window takes settings.toml's; the checks
           load at once, so they never depend on the player's settings. --hardware-speed decides for all of them. */
        int speed = hardware_speed >= 0 ? hardware_speed : trace_sound || capture || dump ? 1 : -1;
        if (capture) {
            return nw_capture(data_dir, capture, capture_dir, speed > 0);
        }
        if (dump) {
            return nw_dump_run(data_dir, dump, dump_dir, dump_heap, speed > 0);
        }
        return check_data   ? nw_check_data(data_dir)
               : boot_check ? nw_boot_check(data_dir, boot_check, boot_loads,
                                            stepped ? NW_CLOCK_STEPPED : NW_CLOCK_REALTIME, speed > 0)
                            : nw_boot(data_dir, speed);
#else
        fprintf(stderr, NW_APP_NAME ": built without the decomp (NUWORLD_DECOMP=OFF)\n");
        return 2;
#endif
    }

    NwPlatform platform;
    if (!nw_platform_init(&platform, NW_APP_NAME, 1280, 720)) {
        return 1;
    }
    char *root = nw_data_root(data_dir);
    if (!root || !nw_ui_init(&platform)) {
        char msg[512];
        SDL_snprintf(msg, sizeof(msg), "%s:\n%s", root ? "The interface could not start" : "No data folder",
                     SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NW_APP_NAME, msg, platform.window);
        SDL_free(root);
        nw_platform_shutdown(&platform);
        return 1;
    }

    /* First run (no imported disc) or --setup: the wizard. */
    NwWizard *wizard = setup || !nw_extract_has_disc(root) ? nw_wizard_create(platform.window, root) : NULL;
    bool running = true;
    int frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            nw_ui_event(&event);
            if (wizard) {
                nw_wizard_event(wizard, &event);
            }
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
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
        if (wizard) {
            nw_ui_begin();
            NwWizardResult result = nw_wizard_draw(wizard);
            nw_ui_end();
            if (result != NW_WIZARD_OPEN) {
                nw_wizard_destroy(wizard);
                wizard = NULL;
                running = result != NW_WIZARD_QUIT;
            }
        }
        SDL_GL_SwapWindow(platform.window);

        if (max_frames > 0 && ++frames >= max_frames) {
            running = false;
        }
    }

    nw_wizard_destroy(wizard);
    nw_ui_shutdown();
    SDL_free(root);
    nw_platform_shutdown(&platform);
    return 0;
}
