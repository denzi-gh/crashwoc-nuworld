/* game/font3d's Text3D override (port/units.toml): the pad buttons' glyph codes draw the prompts the input hands it. */
#ifndef NUWORLD_PORT_FONT3D_H
#define NUWORLD_PORT_FONT3D_H

#include "platform/prompts.h"

/* Text3D's colour for a disabled entry*/
#define NW_FONT3D_GREY 5

/* Any thread: the glyphs Text3D draws from its next call; retail's until the first. */
void nw_font3d_prompts(const NwPrompts *p);
/* NuMtlRelease: the icons' materials and textures went with the level's. */
void nw_font3d_release(void);

#endif
