/* A cutscene: its definition, loaded section by section, and an instance playing it. The order of the sections and
   of the systems' updates is written here and nowhere else. */
#include <math.h>
#include <stdlib.h>

#include "port/gcutpc/internal.h"

typedef struct MetaRecord {
    const char *name;
    float duration;
    float units;
} MetaRecord;

static const NUGCUTPCFIELD_s meta_fields[] = {
    NUGCUTPC_FIELD(MetaRecord, name, NUGCUTPC_STR, 1, 0),
    NUGCUTPC_FIELD(MetaRecord, duration, NUGCUTPC_F32, 1, 0),
    NUGCUTPC_FIELD(MetaRecord, units, NUGCUTPC_F32, 1, 1),
};
static const NUGCUTPCRECORD_s meta_record = NUGCUTPC_RECORD("meta", MetaRecord, meta_fields);

static bool load_meta(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene)
{
    void *records;
    uint32_t count;
    if (!NuGCutPCTableRead(file, "META", &meta_record, &records, &count)) {
        return false;
    }
    if (count != 1) {
        return NuGCutPCFail(file->error, "META: %u meta records, one expected", count);
    }
    const MetaRecord *meta = records;
    if (!(meta->duration >= NUGCUTPC_FIRST_FRAME) || !isfinite(meta->duration)) {
        return NuGCutPCFail(file->error, "META: a duration of %g frames", meta->duration);
    }
    scene->name = meta->name;
    scene->duration = meta->duration;
    scene->units = meta->units;
    return true;
}

/* Each loader may use what those before it loaded: curves, then the transforms made of them, then the parts. */
typedef bool (*SectionLoader)(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene);
static const SectionLoader loaders[] = {
    load_meta,
    NuGCutPCCurveSysLoad,
    NuGCutPCTransformSysLoad,
    NuGCutPCCamSysLoad,
    NuGCutPCRigidSysLoad,
};

NUGCUTPCSCENE_s *NuGCutPCSceneLoad(const void *data, size_t size, NUGCUTPCERROR_s *error)
{
    NUGCUTPCERROR_s ignored;
    error = error ? error : &ignored;
    error->text[0] = '\0';
    NUGCUTPCARENA_s *arena = NuGCutPCArenaCreate();
    NUGCUTPCSCENE_s *scene = arena ? NuGCutPCArenaAlloc(arena, 1, sizeof(*scene)) : NULL;
    if (!scene) {
        NuGCutPCArenaDestroy(arena);
        NuGCutPCFail(error, "out of memory");
        return NULL;
    }
    scene->arena = arena;
    NUGCUTPCFILE_s file;
    bool ok = NuGCutPCFileOpen(&file, data, size, arena, error);
    for (size_t i = 0; ok && i < sizeof(loaders) / sizeof(*loaders); i++) {
        ok = loaders[i](&file, scene);
    }
    if (!ok) {
        NuGCutPCArenaDestroy(arena);
        return NULL;
    }
    return scene;
}

void NuGCutPCSceneFree(NUGCUTPCSCENE_s *scene)
{
    if (scene) {
        NuGCutPCArenaDestroy(scene->arena); /* the scene lives in its own arena */
    }
}

const char *NuGCutPCSceneName(const NUGCUTPCSCENE_s *scene)
{
    return scene->name;
}

float NuGCutPCSceneDuration(const NUGCUTPCSCENE_s *scene)
{
    return scene->duration;
}

/* Instances. */

static void update_systems(instNUGCUTPCSCENE_s *inst)
{
    instNuGCutPCCamSysUpdate(inst);
    instNuGCutPCRigidSysUpdate(inst);
}

instNUGCUTPCSCENE_s *instNuGCutPCSceneCreate(const NUGCUTPCSCENE_s *scene)
{
    instNUGCUTPCSCENE_s *inst = calloc(1, sizeof(*inst));
    if (!inst) {
        return NULL;
    }
    inst->def = scene;
    inst->time = NUGCUTPC_FIRST_FRAME;
    inst->cameras.shown = NUGCUTPC_NO_CAMERA;
    if (!instNuGCutPCRigidSysCreate(inst)) {
        instNuGCutPCSceneDestroy(inst);
        return NULL;
    }
    return inst;
}

void instNuGCutPCSceneDestroy(instNUGCUTPCSCENE_s *inst)
{
    if (inst) {
        instNuGCutPCRigidSysDestroy(inst);
        free(inst);
    }
}

void instNuGCutPCSceneSetLooping(instNUGCUTPCSCENE_s *inst, bool looping)
{
    inst->looping = looping;
}

void instNuGCutPCSceneStart(instNUGCUTPCSCENE_s *inst)
{
    inst->time = NUGCUTPC_FIRST_FRAME;
    inst->running = true;
    inst->finished = false;
    update_systems(inst);
}

/* On by some frames; at the end a looping cutscene starts again from frame 1, another one stops on its last. */
void instNuGCutPCSceneUpdate(instNUGCUTPCSCENE_s *inst, float frames)
{
    if (!inst->running) {
        return;
    }
    float duration = inst->def->duration, span = duration - NUGCUTPC_FIRST_FRAME;
    inst->time += frames;
    if (inst->time > duration) {
        if (inst->looping && span > 0) {
            inst->time = NUGCUTPC_FIRST_FRAME + fmodf(inst->time - NUGCUTPC_FIRST_FRAME, span);
        } else {
            inst->time = duration;
            inst->running = false;
            inst->finished = true;
        }
    }
    update_systems(inst);
}

void instNuGCutPCSceneEnd(instNUGCUTPCSCENE_s *inst)
{
    inst->running = false;
    inst->finished = true;
}

float instNuGCutPCSceneTime(const instNUGCUTPCSCENE_s *inst)
{
    return inst->time;
}

bool instNuGCutPCSceneIsFinished(const instNUGCUTPCSCENE_s *inst)
{
    return inst->finished;
}

int instNuGCutPCSceneCamera(const instNUGCUTPCSCENE_s *inst, NUGCUTPCMTX_s *matrix)
{
    if (matrix && inst->cameras.shown != NUGCUTPC_NO_CAMERA) {
        *matrix = inst->cameras.matrix;
    }
    return inst->cameras.shown;
}

bool instNuGCutPCSceneRigid(const instNUGCUTPCSCENE_s *inst, uint32_t rigid, NUGCUTPCMTX_s *matrix)
{
    if (matrix) {
        *matrix = inst->rigids.matrices[rigid];
    }
    return inst->rigids.visible[rigid];
}
