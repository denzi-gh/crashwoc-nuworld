/* --scene on the game thread (src/port/scene.c): the engine set up as the decomp's main sets it up, one scene drawn
   by NuGScnRndr3 at 50 Hz under the viewer's camera. */
#ifndef NUWORLD_PORT_SCENE_H
#define NUWORLD_PORT_SCENE_H

#include <stdbool.h>
#include <stdint.h>

/* A fly camera in world space: yaw 0 and pitch 0 look along +z. */
typedef struct NwSceneCam {
    float pos[3], yaw, pitch;
} NwSceneCam;

/* What the viewer starts from once the scene is loaded. */
typedef struct NwSceneInfo {
    NwSceneCam cam; /* behind the median instance */
    float speed;    /* a quarter of the instances' spread a second */
    uint32_t instances;
    uint64_t tris;
} NwSceneInfo;

/* Game thread entry: argv[1] is the scene as NuFileOpen takes it. */
int nw_scene_main(int argc, char **argv);
/* Any thread: false until the scene is loaded. */
bool nw_scene_info(NwSceneInfo *info);
/* The camera for the next tick. */
void nw_scene_set_camera(const NwSceneCam *cam);

#endif
