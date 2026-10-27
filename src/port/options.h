/* The game's own sound options, as its options menu sets them (Game.music_volume, sfx_volume): the port's music and
   SFX buses. The game reads music every frame and SFX when a sound starts (voices too); any thread. */
#ifndef NUWORLD_PORT_OPTIONS_H
#define NUWORLD_PORT_OPTIONS_H

#include <stdbool.h>

/* 0-100 */
int nw_options_volume(bool music);
void nw_options_set_volume(bool music, int volume);

#endif
