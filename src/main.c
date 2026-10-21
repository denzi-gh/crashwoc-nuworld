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

int main(int argc, char *argv[])
{
    int max_frames = 0; /* 0 = run until the window is closed */
    const char *import = NULL, *data_dir = NULL;
    bool verify = false, setup = false;

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
        }
    }
    if (import) {
        return nw_import(import, data_dir);
    }
    if (verify) {
        return nw_verify(data_dir);
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
