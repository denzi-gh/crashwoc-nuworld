/* The VU1 microcode as GLSL: needs a current GL 3.3 context. */
#ifndef NUWORLD_GFX_VU_H
#define NUWORLD_GFX_VU_H

#include <stdbool.h>

/* What vu1init leaves for an object: VF01-04, the local matrix times the screen matrix (PS2 row-major). */
typedef struct NwVuObject {
    float lv[16];
} NwVuObject;

bool nw_vu_init(void);
void nw_vu_shutdown(void);
/* vu1init and clipinit alike: the GPU clips, so clip and trivclip need no counterpart. */
void nw_vu1init(NwVuObject *o, const float local[16], const float view[16]);
void nw_vu_use(const NwVuObject *o);
/* The NwVifVertex attributes of the bound VAO and VBO. */
void nw_vu_attribs(void);

#endif
