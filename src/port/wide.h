/* Hor+ widescreen: the session's view over retail's 4:3, which the decomp's NW_WIDE seams (nu_port.h) read. Set
   before the game thread starts, then changed between its frames on request; untouched it stays retail. */
#ifndef NUWORLD_PORT_WIDE_H
#define NUWORLD_PORT_WIDE_H

#define NW_WIDE_MIN (4.0f / 3.0f)
#define NW_WIDE_MAX (32.0f / 9.0f)

/* aspect: the shown picture's w/h, clamped to NW_WIDE_MIN..NW_WIDE_MAX; hud: the widest w/h the HUD spreads to,
   0 for the picture's edges. */
void nw_wide_set(float aspect, float hud);
/* The GS frame's width: 640 at 4:3, even so that its centre is a whole pixel. Any thread. */
int nw_wide_width(void);
/* Asks the game thread for nw_wide_set(aspect, hud) at its next frame; the last request wins. Any thread. */
void nw_wide_request(float aspect, float hud);
/* Takes a request at the end of a frame (NuRndrSwapScreen) outside a background load: the view, then what the game
   took from the frame's width at its start and at the level's. Game thread. */
void nw_wide_poll(void);
/* The VU0 clip test's box: whether it is inside retail's 4:3 frustum, for NW_WIDE_SEEN. */
void nw_wide_clip(int retail_visible);

#endif
