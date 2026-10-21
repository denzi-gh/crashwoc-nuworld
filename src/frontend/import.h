/* Disc import for the first-run wizard: the import folder listing and a background import job. SDL errors are per
   thread, so a job keeps its own error text. */
#ifndef NUWORLD_FRONTEND_IMPORT_H
#define NUWORLD_FRONTEND_IMPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vfs/manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NwImageList {
    char **names; /* file names, sorted case-insensitively */
    size_t count;
} NwImageList;

/* .iso, .bin or .cue, any case. */
bool nw_import_is_image(const char *name);

/* Disc images in dir: .iso and .cue files, and .bin files without a .cue of the same name. A missing dir is an
   empty list. */
bool nw_import_scan(const char *dir, NwImageList *list);
void nw_import_list_free(NwImageList *list);

typedef enum NwImportState {
    NW_IMPORT_RUNNING,
    NW_IMPORT_DONE,
    NW_IMPORT_FAILED,
    NW_IMPORT_CANCELLED,
} NwImportState;

typedef struct NwImport NwImport;

/* Opens image and runs nw_extract_disc into root on a new thread. m must outlive the job. */
NwImport *nw_import_start(const char *image, const char *root, const NwManifest *m);

/* State and bytes copied of the total so far (total is 0 while the disc is still being checked). */
NwImportState nw_import_poll(NwImport *job, uint64_t *done, uint64_t *total);

/* Why the job failed, once it is NW_IMPORT_FAILED. */
const char *nw_import_error(const NwImport *job);

/* Asks the job to stop; it ends as NW_IMPORT_CANCELLED unless it already finished. */
void nw_import_cancel(NwImport *job);

/* Cancels a running job, waits for it and frees it. */
void nw_import_free(NwImport *job);

#ifdef __cplusplus
}
#endif

#endif
