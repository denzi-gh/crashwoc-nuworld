/* .looks files: the port's shader recipes (gfx/material_schema.h) for a model's materials, the file beside the model
   on the disc, and the signals (port/signals.h) that move their fields. The text: docs/ats-format.md. */
#ifndef NW_PORT_LOOKFILE_H
#define NW_PORT_LOOKFILE_H
#include <stdbool.h>
#include <stddef.h>

typedef struct NwLookFile NwLookFile;

/* Reads a file's text; path names it in errors ("path:line: reason", in SDL_GetError). NULL on an error. The signals
   its drives read must exist already. */
NwLookFile *nw_lookfile_read(const char *path, const char *text, size_t size);
void nw_lookfile_free(NwLookFile *file);
/* The recipe a material slot wears: an object's (object, in a scene), else the model's (object NULL). -1: none. */
int nw_lookfile_slot(const NwLookFile *file, const char *object, int slot);
/* split: the joints PREFIXfirst..last of a model each get their own copy of material slot, the group's slots 1... */
typedef struct {
    int slot, first, last;
    const char *prefix, *group;
} NwLookSplit;
/* The file's k-th split (from 0); false past the last. */
bool nw_lookfile_split(const NwLookFile *file, int k, NwLookSplit *out);
/* count: the recipe slot n (from 1) of group wears now, lit while its signal is above n - 0.5; -1: no count. */
int nw_lookfile_counted(const NwLookFile *file, const char *group, int n);
/* A recipe by name; -1: none. */
int nw_lookfile_find(const NwLookFile *file, const char *name);
/* A recipe with its drives applied from the signals now; NULL if a signal is not finite. The values are the file's
   scratch, until the next call. */
const float *nw_lookfile_frame(NwLookFile *file, int look);

#endif
