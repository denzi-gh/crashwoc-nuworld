/* The plane (GLIDER): steering in Tornado Alley, Atmospheric Pressure and the unused AIRSHIP, aim in Tornado Alley. */
#ifndef NUWORLD_GAMEPLAY_PLANE_H
#define NUWORLD_GAMEPLAY_PLANE_H

#include <stdbool.h>

struct GLIDERSTRUCT;
struct nupad_s;

/* ControlGlider: true when it ran retail with the plane's stick and easing. */
bool nw_plane_control(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
/* GliderFire: true when it ran retail and aimed the new shot. */
bool nw_plane_fire(struct GLIDERSTRUCT *glider, struct nupad_s *pad);

#endif
