/* NuGCutPC: the PC cutscene system, reading CSP files (docs/csp-spec.md).

   It stands apart from the PS2 cutscenes (gamelib/gcutscn, src/port/gcutscn.c): nothing here includes or calls the
   legacy code, and nothing of the PS2 engine's behaviour is carried over. Pure C: no files, no SDL, no game state.
   A definition is loaded from the bytes of a .CSP; an instance plays it.

   Phase 2 of docs/csp-format.md: curves, transforms, cameras and rigids. */
#ifndef NUGCUTPC_H
#define NUGCUTPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NUGCUTPC_ERROR_SIZE 256
#define NUGCUTPC_NO_CAMERA (-1)

/* A 4x4 matrix by rows, as the game keeps one: rows 0-2 the local axes in the world, row 3 the position. */
typedef struct NUGCUTPCMTX_s {
    float m[16];
} NUGCUTPCMTX_s;

typedef struct NUGCUTPCERROR_s {
    char text[NUGCUTPC_ERROR_SIZE];
} NUGCUTPCERROR_s;

typedef struct NUGCUTPCSCENE_s NUGCUTPCSCENE_s;         /* a loaded .CSP: read only */
typedef struct instNUGCUTPCSCENE_s instNUGCUTPCSCENE_s; /* a definition being played */

/* Definitions. */

/* The CSP in data (size bytes, which the definition does not keep), or NULL with the reason in error. */
NUGCUTPCSCENE_s *NuGCutPCSceneLoad(const void *data, size_t size, NUGCUTPCERROR_s *error);
void NuGCutPCSceneFree(NUGCUTPCSCENE_s *scene);

const char *NuGCutPCSceneName(const NUGCUTPCSCENE_s *scene); /* NULL when the file has none */
float NuGCutPCSceneDuration(const NUGCUTPCSCENE_s *scene);   /* its last frame; the first is 1 */

uint32_t NuGCutPCCameraCount(const NUGCUTPCSCENE_s *scene);
int NuGCutPCCameraShown(const NUGCUTPCSCENE_s *scene, float time); /* an index, or NUGCUTPC_NO_CAMERA */
void NuGCutPCCameraMatrix(const NUGCUTPCSCENE_s *scene, uint32_t camera, float time, NUGCUTPCMTX_s *out);

uint32_t NuGCutPCRigidCount(const NUGCUTPCSCENE_s *scene);
const char *NuGCutPCRigidName(const NUGCUTPCSCENE_s *scene, uint32_t rigid);
bool NuGCutPCRigidVisible(const NUGCUTPCSCENE_s *scene, uint32_t rigid, float time);
void NuGCutPCRigidMatrix(const NUGCUTPCSCENE_s *scene, uint32_t rigid, float time, NUGCUTPCMTX_s *out);

/* Instances. */

instNUGCUTPCSCENE_s *instNuGCutPCSceneCreate(const NUGCUTPCSCENE_s *scene);
void instNuGCutPCSceneDestroy(instNUGCUTPCSCENE_s *inst);
void instNuGCutPCSceneSetLooping(instNUGCUTPCSCENE_s *inst, bool looping);
void instNuGCutPCSceneStart(instNUGCUTPCSCENE_s *inst); /* at frame 1 */
void instNuGCutPCSceneUpdate(instNUGCUTPCSCENE_s *inst, float frames); /* on by that many of its frames */
void instNuGCutPCSceneEnd(instNUGCUTPCSCENE_s *inst);

float instNuGCutPCSceneTime(const instNUGCUTPCSCENE_s *inst);
bool instNuGCutPCSceneIsFinished(const instNUGCUTPCSCENE_s *inst);
int instNuGCutPCSceneCamera(const instNUGCUTPCSCENE_s *inst, NUGCUTPCMTX_s *matrix); /* the camera shown, or none */
bool instNuGCutPCSceneRigid(const instNUGCUTPCSCENE_s *inst, uint32_t rigid, NUGCUTPCMTX_s *matrix); /* visible? */

#endif /* NUGCUTPC_H */
