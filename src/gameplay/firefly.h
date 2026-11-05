/* The firefly in That Sinking Feeling: lock_on, steering. */
#ifndef NUWORLD_GAMEPLAY_FIREFLY_H
#define NUWORLD_GAMEPLAY_FIREFLY_H

#include <stdbool.h>

struct GLIDERSTRUCT;
struct nupad_s;
struct nuvec_s;

typedef enum NwFireflyLock {
    NW_FIREFLY_NONE,
    NW_FIREFLY_TARGET,  /* what X locks */
    NW_FIREFLY_RELOAD,  /* what X locks after the reload */
    NW_FIREFLY_LOCKING,
    NW_FIREFLY_LOCKED,
    NW_FIREFLY_LOST,    /* let go too early or lost */
} NwFireflyLock;

/* ControlGlider, GliderFire and DrawTorpedoTarget: true when the firefly's ran, retail's included. */
bool nw_firefly_control(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
bool nw_firefly_fire(struct GLIDERSTRUCT *glider, struct nupad_s *pad);
bool nw_firefly_marker(void);
/* What the marker shows, where, and how far the lock, the reload or the fade has run (0-1). */
NwFireflyLock nw_firefly_lock(struct nuvec_s *at, float *progress);
/* NuMtlRelease freed the marker's material. */
void nw_firefly_release(void);

#endif
