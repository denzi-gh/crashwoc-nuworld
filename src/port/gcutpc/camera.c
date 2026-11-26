/* Cameras (docs/csp-spec.md, CAMS): which camera is shown, and where each one is. */
#include "port/gcutpc/internal.h"

typedef struct CamSysRecord {
    int32_t initial;
    uint32_t switches;
} CamSysRecord;

typedef struct CameraRecord {
    uint32_t id;
    const char *name;
    uint8_t flags;
    float matrix[16];
    uint32_t transform;
    float fov, near, far, focus;
} CameraRecord;

static const NUGCUTPCFIELD_s camsys_fields[] = {
    NUGCUTPC_FIELD(CamSysRecord, initial, NUGCUTPC_I32, 1, -1),
    NUGCUTPC_FIELD(CamSysRecord, switches, NUGCUTPC_REF, 1, 0),
};
static const NUGCUTPCFIELD_s camera_fields[] = {
    NUGCUTPC_FIELD(CameraRecord, id, NUGCUTPC_U32, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, name, NUGCUTPC_STR, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, flags, NUGCUTPC_U8, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, matrix, NUGCUTPC_F32, 16, 0),
    NUGCUTPC_FIELD(CameraRecord, transform, NUGCUTPC_REF, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, fov, NUGCUTPC_F32, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, near, NUGCUTPC_F32, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, far, NUGCUTPC_F32, 1, 0),
    NUGCUTPC_FIELD(CameraRecord, focus, NUGCUTPC_F32, 1, 0),
};
static const NUGCUTPCRECORD_s camsys_record = NUGCUTPC_RECORD("camera_system", CamSysRecord, camsys_fields);
static const NUGCUTPCRECORD_s camera_record = NUGCUTPC_RECORD("camera", CameraRecord, camera_fields);

static bool load_switches(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene)
{
    void *records;
    uint32_t count;
    if (!NuGCutPCTableRead(file, "CAMS", &camsys_record, &records, &count)) {
        return false;
    }
    if (count == 0) {
        scene->cameras.initial = NUGCUTPC_NO_CAMERA;
        return true;
    }
    const CamSysRecord *sys = records;
    scene->cameras.initial = sys->initial;
    if (!NuGCutPCCurveRef(file, scene, sys->switches, true, "the camera switches", &scene->cameras.switches)) {
        return false;
    }
    if (scene->cameras.switches && scene->cameras.switches->kind != NUGCUTPC_STEP) {
        return NuGCutPCFail(file->error, "the camera switches are not a step curve");
    }
    return true;
}

static bool load_camera(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, const CameraRecord *rec,
                        NUGCUTPCCAMERA_s *out)
{
    *out = (NUGCUTPCCAMERA_s){ .id = rec->id, .name = rec->name, .flags = rec->flags, .fov = rec->fov,
                               .near_clip = rec->near, .far_clip = rec->far, .focus = rec->focus };
    memcpy(out->matrix.m, rec->matrix, sizeof(out->matrix.m));
    return NuGCutPCTransformRef(file, scene, rec->transform, NUGCUTPC_CAMERA, "a camera", &out->transform);
}

bool NuGCutPCCamSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene)
{
    void *records;
    uint32_t count;
    if (!load_switches(file, scene) || !NuGCutPCTableRead(file, "CAMS", &camera_record, &records, &count)) {
        return false;
    }
    NUGCUTPCCAMERA_s *cameras = NuGCutPCArenaAlloc(file->arena, count, sizeof(*cameras));
    if (!cameras && count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!load_camera(file, scene, (const CameraRecord *)records + i, &cameras[i])) {
            return false;
        }
    }
    scene->cameras.cameras = cameras;
    scene->cameras.count = count;
    return true;
}

/* The definition at a time. */

uint32_t NuGCutPCCameraCount(const NUGCUTPCSCENE_s *scene)
{
    return scene->cameras.count;
}

int NuGCutPCCameraShown(const NUGCUTPCSCENE_s *scene, float time)
{
    const NUGCUTPCCAMSYS_s *sys = &scene->cameras;
    float index = sys->switches ? NuGCutPCCurveEval(sys->switches, time) : (float)sys->initial;
    if (!(index >= 0 && index < (float)sys->count)) {
        return NUGCUTPC_NO_CAMERA;
    }
    return (int)index;
}

void NuGCutPCCameraMatrix(const NUGCUTPCSCENE_s *scene, uint32_t camera, float time, NUGCUTPCMTX_s *out)
{
    const NUGCUTPCCAMERA_s *cam = &scene->cameras.cameras[camera];
    if (cam->transform) {
        NuGCutPCTransformMatrix(cam->transform, time, out);
    } else {
        *out = cam->matrix;
    }
}

/* The instance. */

void instNuGCutPCCamSysUpdate(instNUGCUTPCSCENE_s *inst)
{
    instNUGCUTPCCAMSYS_s *cams = &inst->cameras;
    cams->shown = NuGCutPCCameraShown(inst->def, inst->time);
    if (cams->shown != NUGCUTPC_NO_CAMERA) {
        NuGCutPCCameraMatrix(inst->def, (uint32_t)cams->shown, inst->time, &cams->matrix);
    }
}
