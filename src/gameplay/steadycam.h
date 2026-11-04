/* [game] camera_stability: how much of a jump the game camera follows, from the PS2's (0) to none (100). */
#ifndef NUWORLD_GAMEPLAY_STEADYCAM_H
#define NUWORLD_GAMEPLAY_STEADYCAM_H

#include <stdbool.h>

struct cammtx_s;
struct obj_s;

typedef struct NwSteadyCam {
    bool valid;
    float floor;   /* the height he left the floor at, lowered as he falls below it */
    float x, y, z; /* last frame's position */
} NwSteadyCam;

/* The height the camera sees for a body at x, y, z this frame; standing on a floor or hanging it is y. */
float nw_steadycam_follow(NwSteadyCam *s, float x, float y, float z, bool standing, int stability);

/* Around MoveGameCamera: obj is seen at that height, then put back. */
void nw_steadycam_before(const struct cammtx_s *cam, struct obj_s *obj);
void nw_steadycam_after(struct obj_s *obj);

#endif
