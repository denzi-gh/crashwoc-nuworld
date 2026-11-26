/* Rigids (docs/csp-spec.md, RIGS): the scenes' objects the cutscene moves and shows. An animated rigid is its
   transform's matrix moved by its base matrix's position. */
#include <stdlib.h>

#include "port/gcutpc/internal.h"

#define RIGID_VISIBLE 0x1

typedef struct RigidRecord {
    const char *name;
    const char *scene;
    uint8_t flags;
    float matrix[16];
    uint32_t transform;
    uint32_t visibility;
} RigidRecord;

static const NUGCUTPCFIELD_s rigid_fields[] = {
    NUGCUTPC_FIELD(RigidRecord, name, NUGCUTPC_STR, 1, 0),
    NUGCUTPC_FIELD(RigidRecord, scene, NUGCUTPC_STR, 1, 0),
    NUGCUTPC_FIELD(RigidRecord, flags, NUGCUTPC_U8, 1, 0),
    NUGCUTPC_FIELD(RigidRecord, matrix, NUGCUTPC_F32, 16, 0),
    NUGCUTPC_FIELD(RigidRecord, transform, NUGCUTPC_REF, 1, 0),
    NUGCUTPC_FIELD(RigidRecord, visibility, NUGCUTPC_REF, 1, 0),
};
static const NUGCUTPCRECORD_s rigid_record = NUGCUTPC_RECORD("rigid", RigidRecord, rigid_fields);

static bool load_rigid(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, const RigidRecord *rec,
                       NUGCUTPCRIGID_s *out)
{
    *out = (NUGCUTPCRIGID_s){ .name = rec->name, .scene = rec->scene, .flags = rec->flags };
    memcpy(out->matrix.m, rec->matrix, sizeof(out->matrix.m));
    if (!NuGCutPCTransformRef(file, scene, rec->transform, NUGCUTPC_RIGID, "a rigid", &out->transform) ||
        !NuGCutPCCurveRef(file, scene, rec->visibility, true, "a rigid's visibility", &out->visibility)) {
        return false;
    }
    if (out->visibility && out->visibility->kind != NUGCUTPC_STEP) {
        return NuGCutPCFail(file->error, "rigid %s: its visibility is not a step curve", rec->name ? rec->name : "");
    }
    return true;
}

bool NuGCutPCRigidSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene)
{
    void *records;
    uint32_t count;
    if (!NuGCutPCTableRead(file, "RIGS", &rigid_record, &records, &count)) {
        return false;
    }
    NUGCUTPCRIGID_s *rigids = NuGCutPCArenaAlloc(file->arena, count, sizeof(*rigids));
    if (!rigids && count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!load_rigid(file, scene, (const RigidRecord *)records + i, &rigids[i])) {
            return false;
        }
    }
    scene->rigids.rigids = rigids;
    scene->rigids.count = count;
    return true;
}

/* The definition at a time. */

uint32_t NuGCutPCRigidCount(const NUGCUTPCSCENE_s *scene)
{
    return scene->rigids.count;
}

const char *NuGCutPCRigidName(const NUGCUTPCSCENE_s *scene, uint32_t rigid)
{
    return scene->rigids.rigids[rigid].name;
}

bool NuGCutPCRigidVisible(const NUGCUTPCSCENE_s *scene, uint32_t rigid, float time)
{
    const NUGCUTPCRIGID_s *r = &scene->rigids.rigids[rigid];
    if (!r->visibility) {
        return (r->flags & RIGID_VISIBLE) != 0;
    }
    return NuGCutPCCurveEval(r->visibility, time) != 0;
}

void NuGCutPCRigidMatrix(const NUGCUTPCSCENE_s *scene, uint32_t rigid, float time, NUGCUTPCMTX_s *out)
{
    const NUGCUTPCRIGID_s *r = &scene->rigids.rigids[rigid];
    if (!r->transform) {
        *out = r->matrix;
        return;
    }
    NuGCutPCTransformMatrix(r->transform, time, out);
    for (int i = 12; i < 15; i++) {
        out->m[i] += r->matrix.m[i];
    }
}

/* The instance. */

bool instNuGCutPCRigidSysCreate(instNUGCUTPCSCENE_s *inst)
{
    uint32_t count = inst->def->rigids.count;
    instNUGCUTPCRIGIDSYS_s *sys = &inst->rigids;
    sys->matrices = calloc(count ? count : 1, sizeof(*sys->matrices));
    sys->visible = calloc(count ? count : 1, sizeof(*sys->visible));
    return sys->matrices && sys->visible;
}

void instNuGCutPCRigidSysDestroy(instNUGCUTPCSCENE_s *inst)
{
    free(inst->rigids.matrices);
    free(inst->rigids.visible);
    inst->rigids = (instNUGCUTPCRIGIDSYS_s){ 0 };
}

void instNuGCutPCRigidSysUpdate(instNUGCUTPCSCENE_s *inst)
{
    instNUGCUTPCRIGIDSYS_s *sys = &inst->rigids;
    for (uint32_t i = 0; i < inst->def->rigids.count; i++) {
        sys->visible[i] = NuGCutPCRigidVisible(inst->def, i, inst->time);
        NuGCutPCRigidMatrix(inst->def, i, inst->time, &sys->matrices[i]);
    }
}
