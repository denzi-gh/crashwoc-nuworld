#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.h"
#include "frontend/ui.h"
#include "frontend/wizard.h"
#include "gfx/gl.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "vfs/extract.h"
#ifdef NW_WITH_GAME
#include "data/image.h"
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
    }
    nw_image_free();
    SDL_free(elf);
    nw_vfs_shutdown(&vfs);
    SDL_free(root);
    return ok ? 0 : 1;
}
#endif

int main(int argc, char *argv[])
{
    int max_frames = 0; /* 0 = run until the window is closed */
    const char *import = NULL, *data_dir = NULL;
    bool verify = false, setup = false, check_data = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf(NW_APP_NAME " %s\n", NUWORLD_VERSION);
            return 0;
        }
        if ((strcmp(argv[i], "--import") == 0 || strcmp(argv[i], "--data-dir") == 0) && i + 1 >= argc) {
            fprintf(stderr, NW_APP_NAME ": %s needs a value\n", argv[i]);
            return 2;
        }
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--import") == 0) {
            import = argv[++i];
        } else if (strcmp(argv[i], "--data-dir") == 0) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--verify") == 0) {
            verify = true;
        } else if (strcmp(argv[i], "--setup") == 0) {
            setup = true;
        } else if (strcmp(argv[i], "--check-data") == 0) {
            check_data = true;
        }
    }
    if (import) {
        return nw_import(import, data_dir);
    }
    if (verify) {
        return nw_verify(data_dir);
    }
    if (check_data) {
#ifdef NW_WITH_GAME
        return nw_check_data(data_dir);
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
