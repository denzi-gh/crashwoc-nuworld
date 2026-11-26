#ifndef NW_PORT_CUTPLAY_H
#define NW_PORT_CUTPLAY_H
#include <stdbool.h>
#include <stddef.h>
#include "gamelib/gcutscn.h"
#include "nu3d/nuhgobj.h"

/* A CUT camera or skeletal clip replayed on the host's clock (seconds). Inputs are borrowed scalars. */
typedef struct {
    const char *id, *source, *character;  /* the host's scene ID; the cut character for a clip */
    const float *origin, *delay, *enabled; /* NULL: the time it was played, 0, enabled */
    const char *const *joints;            /* "Name" or "Prefix*" */
    size_t joint_count;
    float first, last, rate;              /* CUT frames: from first, at rate per second, up to last */
    const char *before_source;            /* keep the cut's own animation in that scene up to frame before */
    float before;
    int camera;                           /* -1: the source's selected camera */
    bool skeletal, preserve_root;
} NwMotion;

typedef struct {
    void *context;
    struct instNUGCUTSCENE_s *(*scene)(void *context, const char *id);
    struct instNUGCUTSCENE_s *(*current)(void *context);
    bool (*active)(void *context);
    void (*prepare)(void *context);
    void (*multiply)(struct numtx_s *, const struct numtx_s *, const struct numtx_s *);
    void (*inverse)(struct numtx_s *, const struct numtx_s *);
} NwCutPlaybackHost;
typedef struct {
    const NwMotion *motion;
    float seconds, started;
} NwCutPlaybackRun;
typedef struct {
    NwCutPlaybackHost host;
    NwCutPlaybackRun camera, *clips;
    size_t clip_count, clip_capacity, pose_capacity;
    struct numtx_s camera_pose, *pose;
    bool camera_valid;
    char error[256];
} NwCutPlayback;

void nw_cutplay_init(NwCutPlayback *player, NwCutPlaybackHost host);
void nw_cutplay_destroy(NwCutPlayback *player);
void nw_cutplay_install(NwCutPlayback *player); /* Current host of the game's EvalAnim2 hook; NULL detaches. */
/* Starts a camera (replacing the current one) or a clip at the host time started. */
bool nw_cutplay_play(NwCutPlayback *player, const NwMotion *motion, float started);
void nw_cutplay_clear(NwCutPlayback *player);
/* The host's time now: clips follow it, the camera samples its pose. */
bool nw_cutplay_update(NwCutPlayback *player, float seconds);
bool nw_cutplay_camera(const NwCutPlayback *player, struct numtx_s *out);
bool nw_cutplay_apply(NwCutPlayback *player, struct NUHGOBJ_s *model, float source_frame, struct numtx_s *pose);
bool nw_cutplay_camera_sample(struct instNUGCUTSCENE_s *scene, int index, float frame, struct numtx_s *out);
#endif
