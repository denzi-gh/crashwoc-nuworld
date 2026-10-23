/* The anim v1 records (nuanimdata_s and below) as host copies, for the .ani loader and the .gsc animation library
   (src/port/nuanim.c). */
#ifndef NUWORLD_PORT_NUANIM_H
#define NUWORLD_PORT_NUANIM_H

#include <stddef.h>

/* A format's pointers: at() the size bytes a pointer field of the image addresses (NULL for 0), alloc() zeroed
   host records. */
typedef struct NwAnimFile {
    char *(*at)(struct NwAnimFile *f, char *field, size_t size);
    void *(*alloc)(struct NwAnimFile *f, size_t size, size_t align);
    int node_names; /* relocated (the .ani), or the file's value (the animlib) */
} NwAnimFile;

/* Retail's NuAnimDataFixPtrs walk from the record at p: chunk keys and curves keep the file's values. */
struct nuanimdata_s *nw_anim_data(NwAnimFile *f, char *p);

#endif
