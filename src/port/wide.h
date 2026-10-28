/* Hor+ widescreen: the session's view over retail's 4:3, which the decomp's NW_WIDE seams (nu_port.h) read. Set
   once before the game thread starts; untouched it stays retail. */
#ifndef NUWORLD_PORT_WIDE_H
#define NUWORLD_PORT_WIDE_H

#define NW_WIDE_MIN (4.0f / 3.0f)
#define NW_WIDE_MAX (32.0f / 9.0f)

/* aspect: the shown picture's w/h, clamped to NW_WIDE_MIN..NW_WIDE_MAX; hud: the widest w/h the HUD spreads to,
   0 for the picture's edges. */
void nw_wide_set(float aspect, float hud);
/* The GS frame's width: 640 at 4:3, even so that its centre is a whole pixel. */
int nw_wide_width(void);

#endif
