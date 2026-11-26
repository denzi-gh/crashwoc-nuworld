#ifndef NUWORLD_PORT_NUHGOBJ_H
#define NUWORLD_PORT_NUHGOBJ_H

#include <stddef.h>
#include <stdint.h>

struct NUHGOBJ_s;
union variptr_u;

uint32_t nw_ghg_count(void);

/* NuGHGRead of an image in memory, copied into the buffer; name is for errors. */
struct NUHGOBJ_s *nw_ghg_read_image(union variptr_u *buffer, const void *image, size_t size, const char *name);

struct numtl_s;
/* Gives the layer-0 geometry of the joints PREFIXfirst..PREFIXlast that uses material SLOT its own copy of it, made
   with NuMtlCreate so the renderer lists it. copies[n - first] receives joint PREFIXn's copy, NULL when the model has
   no such joint. Returns the number of copies, or -1 when the slot is missing or a copy cannot be made. */
int nw_hgobj_own_material(struct NUHGOBJ_s *h, int slot, const char *prefix, int first, int last,
                          struct numtl_s **copies);

#endif
