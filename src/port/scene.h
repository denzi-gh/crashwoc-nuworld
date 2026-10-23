/* --scene on the game thread (src/port/scene.c): the engine set up as the decomp's main sets it up, one scene drawn
   by NuGScnRndr3, or one character (.ghg) by NuHGobjRndr, at 50 Hz under the viewer's camera. */
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
    uint32_t objects;
    uint32_t instances;
    uint64_t tris;
    uint32_t seq; /* loads so far */
    bool drawn;   /* false: nothing to draw */
} NwSceneInfo;

/* Game thread entry; it draws the scenes nw_scene_load asks for. */
int nw_scene_main(int argc, char **argv);
/* Any thread: false until the first load. */
bool nw_scene_info(NwSceneInfo *info);
/* The camera for the next tick. */
void nw_scene_set_camera(const NwSceneCam *cam);
/* Any thread: a disc path (either slash), loaded in place of the current scene. */
void nw_scene_load(const char *path);

#endif
