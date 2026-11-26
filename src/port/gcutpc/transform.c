/* Transforms (docs/csp-spec.md, XFRM): channels of curves, and the matrix they make. Everything is in the game's
   space already: no mirroring, and angles are plain floats. */
#include <math.h>

#include "port/gcutpc/internal.h"

typedef struct TransformRecord {
    uint8_t kind;
    uint8_t flags;
    float length;
    uint32_t first, count;
} TransformRecord;

typedef struct ChannelRecord {
    uint32_t curve;
} ChannelRecord;

static const NUGCUTPCFIELD_s transform_fields[] = {
    NUGCUTPC_FIELD(TransformRecord, kind, NUGCUTPC_U8, 1, 0),
    NUGCUTPC_FIELD(TransformRecord, flags, NUGCUTPC_U8, 1, 0),
    NUGCUTPC_FIELD(TransformRecord, length, NUGCUTPC_F32, 1, 0),
    NUGCUTPC_FIELD(TransformRecord, first, NUGCUTPC_U32, 1, 0),
    NUGCUTPC_FIELD(TransformRecord, count, NUGCUTPC_U32, 1, 0),
};
static const NUGCUTPCFIELD_s channel_fields[] = {
    NUGCUTPC_FIELD(ChannelRecord, curve, NUGCUTPC_REF, 1, 0),
};
static const NUGCUTPCRECORD_s transform_record = NUGCUTPC_RECORD("transform", TransformRecord, transform_fields);
static const NUGCUTPCRECORD_s channel_record = NUGCUTPC_RECORD("channel", ChannelRecord, channel_fields);

/* How many channels a transform of each kind can have (a face: one a blend shape layer, as many as it has). */
static const uint32_t max_channels[NUGCUTPC_TRANSFORM_KINDS] = {
    [NUGCUTPC_CAMERA] = 9, [NUGCUTPC_RIGID] = 9, [NUGCUTPC_CHARACTER] = 10,
    [NUGCUTPC_LOCATOR] = 8, [NUGCUTPC_JOINT] = 9, [NUGCUTPC_FACE] = UINT32_MAX,
};

static const char *const kind_names[NUGCUTPC_TRANSFORM_KINDS] = {
    "camera", "rigid", "character", "locator", "joint", "face",
};

static bool load_transform(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, uint32_t index,
                           const TransformRecord *rec, const ChannelRecord *channels, uint32_t channel_count,
                           NUGCUTPCTRANSFORM_s *out)
{
    if (rec->kind >= NUGCUTPC_TRANSFORM_KINDS) {
        return NuGCutPCFail(file->error, "transform %u: kind %u unknown", index, rec->kind);
    }
    if (rec->first > channel_count || rec->count > channel_count - rec->first) {
        return NuGCutPCFail(file->error, "transform %u: channels outside their table", index);
    }
    if (rec->count > max_channels[rec->kind]) {
        return NuGCutPCFail(file->error, "transform %u: %u channels for a %s", index, rec->count,
                            kind_names[rec->kind]);
    }
    const NUGCUTPCCURVE_s **curves = NuGCutPCArenaAlloc(file->arena, rec->count, sizeof(*curves));
    if (!curves && rec->count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t c = 0; c < rec->count; c++) {
        if (!NuGCutPCCurveRef(file, scene, channels[rec->first + c].curve, false, "a transform's channel",
                              &curves[c])) {
            return false;
        }
    }
    *out = (NUGCUTPCTRANSFORM_s){ (NuGCutPCTransformKind)rec->kind, rec->flags, rec->length, curves, rec->count };
    return true;
}

bool NuGCutPCTransformSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene)
{
    void *records, *channels;
    uint32_t count, channel_count;
    if (!NuGCutPCTableRead(file, "XFRM", &transform_record, &records, &count) ||
        !NuGCutPCTableRead(file, "XFRM", &channel_record, &channels, &channel_count)) {
        return false;
    }
    NUGCUTPCTRANSFORM_s *transforms = NuGCutPCArenaAlloc(file->arena, count, sizeof(*transforms));
    if (!transforms && count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!load_transform(file, scene, i, (const TransformRecord *)records + i, channels, channel_count,
                            &transforms[i])) {
            return false;
        }
    }
    scene->transforms = transforms;
    scene->transform_count = count;
    return true;
}

bool NuGCutPCTransformRef(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, uint32_t ref,
                          NuGCutPCTransformKind kind, const char *what, const NUGCUTPCTRANSFORM_s **out)
{
    *out = NULL;
    if (ref == NUGCUTPC_NONE) {
        return true;
    }
    if (ref >= scene->transform_count) {
        return NuGCutPCFail(file->error, "%s: transform %u of %u", what, ref, scene->transform_count);
    }
    if (scene->transforms[ref].kind != kind) {
        return NuGCutPCFail(file->error, "%s: transform %u is a %s, not a %s", what, ref,
                            kind_names[scene->transforms[ref].kind], kind_names[kind]);
    }
    *out = &scene->transforms[ref];
    return true;
}

/* The matrix. */

static float channel(const NUGCUTPCTRANSFORM_s *t, NuGCutPCChannel c, float time, float fallback)
{
    return (uint32_t)c < t->channel_count ? NuGCutPCCurveEval(t->channels[c], time) : fallback;
}

/* The rows of the rotation for rx ry rz (radians), as docs/csp-spec.md writes them. */
static void rotation(float rx, float ry, float rz, float r[3][3])
{
    float sx = sinf(rx), cx = cosf(rx), sy = sinf(ry), cy = cosf(ry), sz = sinf(rz), cz = cosf(rz);
    r[0][0] = cy * cz;                r[0][1] = cy * sz;                r[0][2] = -sy;
    r[1][0] = sx * sy * cz - cx * sz; r[1][1] = sx * sy * sz + cx * cz; r[1][2] = sx * cy;
    r[2][0] = cx * sy * cz + sx * sz; r[2][1] = cx * sy * sz - sx * cz; r[2][2] = cx * cy;
}

void NuGCutPCTransformMatrix(const NUGCUTPCTRANSFORM_s *t, float time, NUGCUTPCMTX_s *out)
{
    if (time > t->length) {
        time = t->length; /* after its last frame, an animation stays where it ended */
    }
    float r[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    if (t->flags & NUGCUTPC_ROTATES) {
        rotation(channel(t, NUGCUTPC_RX, time, 0), channel(t, NUGCUTPC_RY, time, 0), channel(t, NUGCUTPC_RZ, time, 0),
                 r);
    }
    float scale[3] = { 1, 1, 1 };
    if (t->flags & NUGCUTPC_SCALES) {
        for (int i = 0; i < 3; i++) {
            scale[i] = channel(t, (NuGCutPCChannel)(NUGCUTPC_SX + i), time, 1);
        }
    }
    float *m = out->m;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[4 * i + j] = r[i][j] * scale[i];
        }
        m[4 * i + 3] = 0;
    }
    m[12] = channel(t, NUGCUTPC_TX, time, 0);
    m[13] = channel(t, NUGCUTPC_TY, time, 0);
    m[14] = channel(t, NUGCUTPC_TZ, time, 0);
    m[15] = 1;
}
