#ifndef NUWORLD_PORT_FONT3D_H
#define NUWORLD_PORT_FONT3D_H

#include "platform/prompts.h"

#define NW_FONT3D_GREY 5

/* Any thread: the glyphs Text3D draws from its next call; retail's until the first. */
void nw_font3d_prompts(const NwPrompts *p);
/* NuMtlRelease: the icons' materials and textures went with the level's. */
void nw_font3d_release(void);
/* Game thread: restore the returned punctuation state after drawing subtitles. */
bool nw_font3d_subtitle_punctuation(bool enabled);

#endif
