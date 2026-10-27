/* The audio device: the SPU2's output, 48 kHz stereo, on an SDL audio stream. */
#ifndef NUWORLD_PLATFORM_AUDIO_H
#define NUWORLD_PLATFORM_AUDIO_H

#include <stdbool.h>

/* Opens the default playback device and takes the SPU2's output; false with an SDL error (the game runs silent). */
bool nw_audio_open(void);
void nw_audio_close(void);

/* The port's buses, 0-100 over the game's own volumes: master scales the output, voice the game's voice stream
   (NUSOUND channel 4). The main thread; the device need not be open. */
void nw_audio_set_buses(int master, int voice);

#endif
