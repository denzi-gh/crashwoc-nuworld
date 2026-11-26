/* What the files of the PC cutscene system share, and nothing else: the reader of a CSP's bytes, the schema that
   binds its fields by name, the arena a definition lives in, and the definition and instance structures. The order
   of a definition's sections, and of an instance's updates, is written once, in scene.c. */
#ifndef NUGCUTPC_INTERNAL_H
#define NUGCUTPC_INTERNAL_H

#include <string.h>

#include "port/gcutpc/nugcutpc.h"

#define NUGCUTPC_NONE 0xFFFFFFFFu /* a reference or string that points nowhere */
#define NUGCUTPC_FIRST_FRAME 1.0f

/* Little-endian reads; the bounds are the caller's to check. */
static inline uint16_t NuGCutPCRead16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static inline uint32_t NuGCutPCRead32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t NuGCutPCRead64(const uint8_t *p)
{
    return (uint64_t)NuGCutPCRead32(p) | (uint64_t)NuGCutPCRead32(p + 4) << 32;
}

static inline float NuGCutPCReadF32(const uint8_t *p)
{
    uint32_t bits = NuGCutPCRead32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* error.c: writes the reason and returns false, so that a check reads "return NuGCutPCFail(...)". */
bool NuGCutPCFail(NUGCUTPCERROR_s *error, const char *format, ...);

/* arena.c: the memory of one definition, given back all at once. */
typedef struct NUGCUTPCARENA_s NUGCUTPCARENA_s;
NUGCUTPCARENA_s *NuGCutPCArenaCreate(void);
void NuGCutPCArenaDestroy(NUGCUTPCARENA_s *arena);
void *NuGCutPCArenaAlloc(NUGCUTPCARENA_s *arena, size_t count, size_t size); /* zeroed; NULL when out of memory */

/* schema.c: record types. A reader describes the C structure it fills; the file describes what it wrote. */
typedef enum NuGCutPCFieldType {
    NUGCUTPC_U8 = 1,
    NUGCUTPC_U16,
    NUGCUTPC_U32,
    NUGCUTPC_I32,
    NUGCUTPC_F32,
    NUGCUTPC_STR, /* filled as a const char *, NULL for none */
    NUGCUTPC_REF, /* filled as a uint32_t, NUGCUTPC_NONE for none */
} NuGCutPCFieldType;

typedef struct NUGCUTPCFIELD_s {
    const char *name;
    NuGCutPCFieldType type;
    uint16_t count;
    size_t offset;  /* in the C structure */
    float fallback; /* the value when the file does not have the field (strings: NULL, references: none) */
} NUGCUTPCFIELD_s;

typedef struct NUGCUTPCRECORD_s {
    const char *name;
    size_t size; /* of the C structure */
    const NUGCUTPCFIELD_s *fields;
    size_t field_count;
} NUGCUTPCRECORD_s;

#define NUGCUTPC_FIELD(type, member, kind, n, value) { #member, kind, n, offsetof(type, member), value }
#define NUGCUTPC_RECORD(name, type, fields) { name, sizeof(type), fields, sizeof(fields) / sizeof(*(fields)) }

typedef struct NUGCUTPCWRITTENFIELD_s {
    const char *name;
    NuGCutPCFieldType type;
    uint16_t count;
    uint32_t offset;
} NUGCUTPCWRITTENFIELD_s;

typedef struct NUGCUTPCWRITTENTYPE_s {
    const char *name;
    uint32_t size;
    const NUGCUTPCWRITTENFIELD_s *fields;
    uint32_t field_count;
} NUGCUTPCWRITTENTYPE_s;

/* file.c: an opened CSP. */
typedef struct NUGCUTPCSECTION_s {
    char id[5];
    uint32_t version;
    const uint8_t *data;
    size_t size;
} NUGCUTPCSECTION_s;

typedef struct NUGCUTPCFILE_s {
    const uint8_t *data;
    size_t size;
    const NUGCUTPCSECTION_s *sections;
    uint32_t section_count;
    const NUGCUTPCWRITTENTYPE_s *types; /* SCHM */
    uint32_t type_count;
    const char *strings;                /* STRS, copied into the arena and closed by a 0 */
    size_t strings_size;
    NUGCUTPCARENA_s *arena;
    NUGCUTPCERROR_s *error;
} NUGCUTPCFILE_s;

bool NuGCutPCFileOpen(NUGCUTPCFILE_s *file, const void *data, size_t size, NUGCUTPCARENA_s *arena,
                      NUGCUTPCERROR_s *error);
const NUGCUTPCSECTION_s *NuGCutPCFileSection(const NUGCUTPCFILE_s *file, const char *id);

bool NuGCutPCSchemaRead(NUGCUTPCFILE_s *file, const NUGCUTPCSECTION_s *schm);
/* The records of the reader's type in a section, in the arena with their fields bound by name: *records NULL and
   *count 0 when the file has no such table (or no such section). */
bool NuGCutPCTableRead(NUGCUTPCFILE_s *file, const char *section, const NUGCUTPCRECORD_s *record, void **records,
                       uint32_t *count);

/* The definition. */
typedef enum NuGCutPCCurveKind {
    NUGCUTPC_CONSTANT,
    NUGCUTPC_STEP,
    NUGCUTPC_LINEAR,
    NUGCUTPC_HERMITE,
    NUGCUTPC_BEZIER,
    NUGCUTPC_CURVE_KINDS,
} NuGCutPCCurveKind;

typedef struct NUGCUTPCKEY_s {
    float t, v;
    float slope;                          /* hermite */
    float in_dt, in_dv, out_dt, out_dv;   /* bezier handles, relative to the key */
} NUGCUTPCKEY_s;

typedef struct NUGCUTPCCURVE_s {
    NuGCutPCCurveKind kind;
    float constant;
    const NUGCUTPCKEY_s *keys;
    uint32_t key_count;
} NUGCUTPCCURVE_s;

typedef enum NuGCutPCTransformKind {
    NUGCUTPC_CAMERA,
    NUGCUTPC_RIGID,
    NUGCUTPC_CHARACTER,
    NUGCUTPC_LOCATOR,
    NUGCUTPC_JOINT,
    NUGCUTPC_FACE,
    NUGCUTPC_TRANSFORM_KINDS,
} NuGCutPCTransformKind;

/* NUGCUTPCTRANSFORM_s.flags */
#define NUGCUTPC_ROTATES 0x1
#define NUGCUTPC_SCALES 0x8
#define NUGCUTPC_JOINT_ORIENT 0x20

/* The channels of the camera, rigid, character, locator and joint kinds, in their order (docs/csp-spec.md). */
typedef enum NuGCutPCChannel {
    NUGCUTPC_TX, NUGCUTPC_TY, NUGCUTPC_TZ,
    NUGCUTPC_RX, NUGCUTPC_RY, NUGCUTPC_RZ,
    NUGCUTPC_SX, NUGCUTPC_SY, NUGCUTPC_SZ,
} NuGCutPCChannel;

typedef struct NUGCUTPCTRANSFORM_s {
    NuGCutPCTransformKind kind;
    uint8_t flags;
    float length;
    const NUGCUTPCCURVE_s *const *channels;
    uint32_t channel_count;
} NUGCUTPCTRANSFORM_s;

typedef struct NUGCUTPCCAMERA_s {
    uint32_t id;
    const char *name;
    uint8_t flags;
    NUGCUTPCMTX_s matrix;
    const NUGCUTPCTRANSFORM_s *transform; /* NULL: the matrix */
    float fov, near_clip, far_clip, focus; /* 0: the game's */
} NUGCUTPCCAMERA_s;

typedef struct NUGCUTPCCAMSYS_s {
    const NUGCUTPCCAMERA_s *cameras;
    uint32_t count;
    int32_t initial;
    const NUGCUTPCCURVE_s *switches; /* a step curve of camera indices, NULL: the initial one throughout */
} NUGCUTPCCAMSYS_s;

typedef struct NUGCUTPCRIGID_s {
    const char *name;
    const char *scene; /* NULL: the main scene */
    uint8_t flags;
    NUGCUTPCMTX_s matrix;
    const NUGCUTPCTRANSFORM_s *transform; /* NULL: the matrix */
    const NUGCUTPCCURVE_s *visibility;    /* a step curve, NULL: the flag */
} NUGCUTPCRIGID_s;

typedef struct NUGCUTPCRIGIDSYS_s {
    const NUGCUTPCRIGID_s *rigids;
    uint32_t count;
} NUGCUTPCRIGIDSYS_s;

struct NUGCUTPCSCENE_s {
    NUGCUTPCARENA_s *arena;
    const char *name;
    float duration, units;
    const NUGCUTPCCURVE_s *curves;
    uint32_t curve_count;
    const NUGCUTPCTRANSFORM_s *transforms;
    uint32_t transform_count;
    NUGCUTPCCAMSYS_s cameras;
    NUGCUTPCRIGIDSYS_s rigids;
};

/* Section loaders, called by scene.c in the order of the file's dependencies. */
bool NuGCutPCCurveSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene);
bool NuGCutPCTransformSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene);
bool NuGCutPCCamSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene);
bool NuGCutPCRigidSysLoad(NUGCUTPCFILE_s *file, NUGCUTPCSCENE_s *scene);

/* References between sections: NUGCUTPC_NONE gives NULL when optional, an index past the table is an error. */
bool NuGCutPCCurveRef(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, uint32_t ref, bool optional,
                      const char *what, const NUGCUTPCCURVE_s **out);
bool NuGCutPCTransformRef(NUGCUTPCFILE_s *file, const NUGCUTPCSCENE_s *scene, uint32_t ref,
                          NuGCutPCTransformKind kind, const char *what, const NUGCUTPCTRANSFORM_s **out);

/* Evaluation. */
float NuGCutPCCurveEval(const NUGCUTPCCURVE_s *curve, float time);
void NuGCutPCTransformMatrix(const NUGCUTPCTRANSFORM_s *transform, float time, NUGCUTPCMTX_s *out);

/* The instance: each system's state, updated by scene.c in a fixed order. */
typedef struct instNUGCUTPCCAMSYS_s {
    int shown;
    NUGCUTPCMTX_s matrix;
} instNUGCUTPCCAMSYS_s;

typedef struct instNUGCUTPCRIGIDSYS_s {
    NUGCUTPCMTX_s *matrices;
    bool *visible;
} instNUGCUTPCRIGIDSYS_s;

struct instNUGCUTPCSCENE_s {
    const NUGCUTPCSCENE_s *def;
    float time;
    bool looping, running, finished;
    instNUGCUTPCCAMSYS_s cameras;
    instNUGCUTPCRIGIDSYS_s rigids;
};

void instNuGCutPCCamSysUpdate(instNUGCUTPCSCENE_s *inst);
bool instNuGCutPCRigidSysCreate(instNUGCUTPCSCENE_s *inst);
void instNuGCutPCRigidSysDestroy(instNUGCUTPCSCENE_s *inst);
void instNuGCutPCRigidSysUpdate(instNUGCUTPCSCENE_s *inst);

/* txt.c: the line reader of the .TXT files. A vocabulary is a table of words; each word has a small function that
   reads its values from the line into the target, and a block word opens the element its block's words fill. */
#define NUGCUTPC_TXT_WORDS 32   /* words on one line */
#define NUGCUTPC_TXT_LINE 1024  /* characters on one line */
#define NUGCUTPC_TXT_VOCABULARY 64

typedef struct NUGCUTPCTXT_s NUGCUTPCTXT_s;
typedef struct NUGCUTPCVOCABULARY_s NUGCUTPCVOCABULARY_s;

typedef struct NUGCUTPCWORD_s {
    const char *name;
    bool (*read)(NUGCUTPCTXT_s *txt, void *target);   /* a word with values */
    void *(*open)(NUGCUTPCTXT_s *txt, void *target);  /* a block word: the element its block fills */
    const NUGCUTPCVOCABULARY_s *block;
    bool repeat;
} NUGCUTPCWORD_s;

struct NUGCUTPCVOCABULARY_s {
    const NUGCUTPCWORD_s *words;
    size_t count;
};

struct NUGCUTPCTXT_s {
    const char *text, *where;
    size_t size, pos;
    int line;
    char buffer[NUGCUTPC_TXT_LINE];
    char *words[NUGCUTPC_TXT_WORDS];
    int count, next; /* the line's words, and the next one a value is read from */
    uint32_t *capacity; /* of the list of the word being read (the vocabulary's words, by index) */
    NUGCUTPCARENA_s *arena;
    NUGCUTPCERROR_s *error;
};

bool NuGCutPCTxtRead(NUGCUTPCTXT_s *txt, const NUGCUTPCVOCABULARY_s *vocabulary, void *target);
bool NuGCutPCTxtFail(NUGCUTPCTXT_s *txt, const char *format, ...);

/* The next value of the line; each fails with the line's number when there is none or it is not one. */
bool NuGCutPCTxtFloat(NUGCUTPCTXT_s *txt, float *out);
bool NuGCutPCTxtInt(NUGCUTPCTXT_s *txt, int32_t *out);
bool NuGCutPCTxtByte(NUGCUTPCTXT_s *txt, int16_t *out); /* 0-255 */
bool NuGCutPCTxtString(NUGCUTPCTXT_s *txt, const char **out);
bool NuGCutPCTxtChoice(NUGCUTPCTXT_s *txt, const char *const *names, int count, int32_t *out);
bool NuGCutPCTxtOption(NUGCUTPCTXT_s *txt, const char *name); /* the next word is name: taken */
bool NuGCutPCTxtMore(const NUGCUTPCTXT_s *txt);               /* values left on the line */

/* A new zeroed element at the end of the list of the word being read. */
void *NuGCutPCTxtPush(NUGCUTPCTXT_s *txt, void **items, uint32_t *count, size_t size);
#define NUGCUTPC_PUSH(txt, items, count) NuGCutPCTxtPush((txt), (void **)&(items), &(count), sizeof(*(items)))

#endif /* NUGCUTPC_INTERNAL_H */
