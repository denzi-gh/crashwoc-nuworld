#ifndef NUWORLD_PORT_TEXT3D_H
#define NUWORLD_PORT_TEXT3D_H

#include <stdbool.h>
#include <stddef.h>

/* Encode plain UTF-8 using the original font's uppercase glyphs. */
bool nw_text3d_encode(const char *text, char *out, size_t size);

#endif
