/* The port's .ghg loader (src/port/nuhgobj.c): what it keeps outside the caller's buffer. */
#ifndef NUWORLD_PORT_NUHGOBJ_H
#define NUWORLD_PORT_NUHGOBJ_H

#include <stddef.h>
#include <stdint.h>

struct NUHGOBJ_s;
union variptr_u;

/* The objects NuGHGRead made that NuHGobjDestroy has not freed yet. */
uint32_t nw_ghg_count(void);

/* NuGHGRead of an image in memory, copied into the buffer; name is for errors. */
struct NUHGOBJ_s *nw_ghg_read_image(union variptr_u *buffer, const void *image, size_t size, const char *name);

#endif
