/* The port's shader recipes by field name: what each value of a material's effect (gfx/vu.c u_fx) is called in a
   .looks file (port/lookfile.c) and in an editor. u_fx[0] is the shader; the fields name the rest. */
#ifndef NW_GFX_MATERIAL_SCHEMA_H
#define NW_GFX_MATERIAL_SCHEMA_H
#include <stddef.h>

#define NW_MATERIAL_VALUES 16 /* gfx/rndr.h NW_MTL_FX_LEN */

typedef struct {
    const char *name;
    float value;
} NwFieldChoice;

typedef struct {
    const char *name;
    int at, count;                /* its place in u_fx and its components */
    const NwFieldChoice *choices; /* named values instead of numbers (NULL-terminated), or NULL */
} NwMaterialField;

typedef struct {
    const char *name;
    float id; /* u_fx[0] */
    const NwMaterialField *fields;
    size_t field_count;
} NwMaterialShader;

/* gloss: painted surfaces. A negative mode replaces the lamp's value: the gradients and UNIVERSAL's ring. */
static const NwFieldChoice nw_gloss_modes[] = {{"gradient", -2}, {"wrath", -1}, {"ring", -3}, {NULL, 0}};
static const NwMaterialField nw_gloss_fields[] = {
    {"height", 1, 1}, {"flip", 1, 1}, {"highlight", 2, 1}, {"power", 3, 1}, {"lit", 4, 3}, {"rim", 7, 1},
    {"shade", 8, 3}, {"mode", 11, 1, nw_gloss_modes}, {"lamp", 11, 1}, {"sides", 12, 3}, {"reveal", 12, 1},
    {"flash", 13, 1}, {"glow", 12, 3}, {"width", 15, 1}, {"motion", 15, 1}, {"pulse", 15, 1},
};
/* metal: the ring and the tubes, with the panel's electricity. */
static const NwMaterialField nw_metal_fields[] = {
    {"softbox", 1, 1}, {"rim", 2, 1}, {"tube", 3, 1}, {"tint", 4, 3}, {"texture", 7, 1}, {"clock", 8, 1},
    {"electricity", 9, 1}, {"strike", 10, 1}, {"glow", 12, 3}, {"width", 15, 1},
};
/* vortex: the title's water. The drain and half axes in its plane, the clock and how far it has formed, its light,
   the strengths of its bolts, rocks, fire, clouds and splashes, the first strike's time. */
static const NwMaterialField nw_vortex_fields[] = {
    {"clock", 1, 1}, {"fill", 2, 1}, {"water", 3, 1}, {"drain", 4, 2}, {"axes", 6, 2}, {"bolts", 8, 1},
    {"rocks", 9, 1}, {"fire", 10, 1}, {"clouds", 11, 1}, {"splash", 12, 1}, {"strike", 13, 1},
};

#define NW_SHADER(name, id, fields) {name, id, fields, sizeof(fields) / sizeof(fields[0])}
static const NwMaterialShader nw_material_shaders[] = {
    NW_SHADER("gloss", 3, nw_gloss_fields),
    NW_SHADER("metal", 2, nw_metal_fields),
    NW_SHADER("vortex", 1, nw_vortex_fields),
};
#undef NW_SHADER

#endif
