/* game/font3d's Text3D under the port (port/units.toml override): the glyph codes of the pad's buttons draw the
   prompts the input hands it. */
#ifndef NUWORLD_PORT_FONT3D_H
#define NUWORLD_PORT_FONT3D_H

#include "platform/prompts.h"

/* Any thread: the glyphs Text3D draws from its next call; retail's until the first. */
void nw_font3d_prompts(const NwPrompts *p);

#endif
