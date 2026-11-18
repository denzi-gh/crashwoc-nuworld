#ifndef NUWORLD_UPDATER_JOB_H
#define NUWORLD_UPDATER_JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define NW_UPDATER_PROGRAM "NuWorld.exe"
#define NW_UPDATER_ARCHIVE "windows-x64.zip"
#else
#define NW_UPDATER_PROGRAM "NuWorld"
#define NW_UPDATER_ARCHIVE "linux-x86_64.tar.gz"
#endif

typedef enum NwUpdaterState {
    NW_UPDATER_WAITING,
    NW_UPDATER_DOWNLOADING,
    NW_UPDATER_INSTALLING,
    NW_UPDATER_DONE,
    NW_UPDATER_FAILED,
    NW_UPDATER_CANCELLED,
} NwUpdaterState;

typedef bool (*NwUpdaterFetch)(void *user, const char *name, const char *path, uint64_t max, NwHttpProgress progress,
                               void *progress_user);

typedef struct NwUpdaterConfig {
    const char *install;
    const char *from;
    const char *to;
    bool wait;
    NwUpdaterFetch fetch;
    void *fetch_user;
    const uint8_t (*keys)[32];
    size_t key_count;
} NwUpdaterConfig;

typedef struct NwUpdater NwUpdater;

bool nw_updater_apply(const NwUpdaterConfig *config);

NwUpdater *nw_updater_start(const NwUpdaterConfig *config);
NwUpdaterState nw_updater_poll(NwUpdater *u, uint64_t *done, uint64_t *total);
const char *nw_updater_error(const NwUpdater *u);
void nw_updater_cancel(NwUpdater *u);
void nw_updater_free(NwUpdater *u);

#ifdef __cplusplus
}
#endif

#endif
