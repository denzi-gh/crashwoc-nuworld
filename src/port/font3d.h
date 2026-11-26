#ifndef NUWORLD_PORT_FONT3D_H
#define NUWORLD_PORT_FONT3D_H

#include "platform/prompts.h"

#define NW_FONT3D_GREY 5

/* Any thread: the glyphs Text3D draws from its next call; retail's until the first. */
void nw_font3d_prompts(const NwPrompts *p);
/* Any thread: the style those prompts are of (the device used last), for text that names a key; and its reading. */
void nw_font3d_prompt_style(NwPromptStyle style);
NwPromptStyle nw_font3d_prompt_style_of(void);
/* NuMtlRelease: the icons' materials and textures went with the level's. */
void nw_font3d_release(void);

#endif
