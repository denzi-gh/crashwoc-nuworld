/* CSP files for tests (docs/csp-spec.md), written field by field by name with the spec's layout rules, so that the
   reader is checked against the spec rather than against another reader. A test may add record types, fields and
   sections of its own, as a newer writer would. */
#ifndef CSP_BUILDER_H
#define CSP_BUILDER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CSP_U8 = 1, CSP_U16, CSP_U32, CSP_I32, CSP_F32, CSP_STR, CSP_REF };
#define CSP_NONE 0xFFFFFFFFu
#define CSP_MAX_FIELDS 24
#define CSP_MAX_TYPES 32
#define CSP_MAX_SECTIONS 16
#define CSP_MAX_TABLES 8

typedef struct CspField {
    const char *name;
    int type;
    int count;
} CspField;

typedef struct CspType {
    const char *name;
    CspField fields[CSP_MAX_FIELDS];
    uint32_t offsets[CSP_MAX_FIELDS];
    int nfields;
    uint32_t size;
} CspType;

typedef struct CspTable {
    int type;
    uint32_t count;
    uint8_t *records;
} CspTable;

typedef struct CspSection {
    char id[5];
    CspTable tables[CSP_MAX_TABLES];
    int ntables;
    const uint8_t *raw; /* a raw section instead of tables */
    size_t raw_size;
} CspSection;

typedef struct CspBuilder {
    CspType types[CSP_MAX_TYPES];
    int ntypes;
    CspSection sections[CSP_MAX_SECTIONS];
    int nsections;
    char strings[8192];
    size_t strings_size;
    uint16_t major;
} CspBuilder;

static int csp_field_size(int type)
{
    return type == CSP_U8 ? 1 : type == CSP_U16 ? 2 : 4;
}

/* A record type, laid out as the spec says: each field on a multiple of its size, the record on a multiple of 4. */
static int csp_type(CspBuilder *b, const char *name, const CspField *fields, int nfields)
{
    CspType *t = &b->types[b->ntypes];
    t->name = name;
    t->nfields = nfields;
    uint32_t pos = 0;
    for (int i = 0; i < nfields; i++) {
        uint32_t align = (uint32_t)csp_field_size(fields[i].type);
        pos += (align - pos % align) % align;
        t->fields[i] = fields[i];
        t->offsets[i] = pos;
        pos += align * (uint32_t)fields[i].count;
    }
    t->size = pos + (4 - pos % 4) % 4;
    return b->ntypes++;
}

#define CSP_TYPE(b, name, ...)                                                                                        \
    csp_type(b, name, (const CspField[]){ __VA_ARGS__ }, sizeof((const CspField[]){ __VA_ARGS__ }) / sizeof(CspField))

/* The record types of CSP 1.0 that phase 2 reads. */
static void csp_spec_types(CspBuilder *b)
{
    CSP_TYPE(b, "meta", { "name", CSP_STR, 1 }, { "duration", CSP_F32, 1 }, { "units", CSP_F32, 1 });
    CSP_TYPE(b, "curve", { "kind", CSP_U8, 1 }, { "constant", CSP_F32, 1 }, { "first", CSP_U32, 1 },
             { "count", CSP_U32, 1 });
    CSP_TYPE(b, "key_step", { "t", CSP_F32, 1 }, { "v", CSP_F32, 1 });
    CSP_TYPE(b, "key_linear", { "t", CSP_F32, 1 }, { "v", CSP_F32, 1 });
    CSP_TYPE(b, "key_hermite", { "t", CSP_F32, 1 }, { "v", CSP_F32, 1 }, { "slope", CSP_F32, 1 });
    CSP_TYPE(b, "key_bezier", { "t", CSP_F32, 1 }, { "v", CSP_F32, 1 }, { "in_dt", CSP_F32, 1 },
             { "in_dv", CSP_F32, 1 }, { "out_dt", CSP_F32, 1 }, { "out_dv", CSP_F32, 1 });
    CSP_TYPE(b, "transform", { "kind", CSP_U8, 1 }, { "flags", CSP_U8, 1 }, { "length", CSP_F32, 1 },
             { "first", CSP_U32, 1 }, { "count", CSP_U32, 1 });
    CSP_TYPE(b, "channel", { "curve", CSP_REF, 1 });
    CSP_TYPE(b, "camera_system", { "initial", CSP_I32, 1 }, { "switches", CSP_REF, 1 });
    CSP_TYPE(b, "camera", { "id", CSP_U32, 1 }, { "name", CSP_STR, 1 }, { "flags", CSP_U8, 1 },
             { "matrix", CSP_F32, 16 }, { "transform", CSP_REF, 1 }, { "fov", CSP_F32, 1 }, { "near", CSP_F32, 1 },
             { "far", CSP_F32, 1 }, { "focus", CSP_F32, 1 });
    CSP_TYPE(b, "rigid", { "name", CSP_STR, 1 }, { "scene", CSP_STR, 1 }, { "flags", CSP_U8, 1 },
             { "matrix", CSP_F32, 16 }, { "transform", CSP_REF, 1 }, { "visibility", CSP_REF, 1 });
}

static int csp_type_named(const CspBuilder *b, const char *name)
{
    for (int i = 0; i < b->ntypes; i++) {
        if (strcmp(b->types[i].name, name) == 0) {
            return i;
        }
    }
    fprintf(stderr, "csp_builder: no record type %s\n", name);
    exit(2);
}

static CspSection *csp_section(CspBuilder *b, const char *id)
{
    for (int i = 0; i < b->nsections; i++) {
        if (strcmp(b->sections[i].id, id) == 0) {
            return &b->sections[i];
        }
    }
    CspSection *s = &b->sections[b->nsections++];
    memcpy(s->id, id, 4);
    return s;
}

/* A table of count zeroed records of a type, in a section. */
static CspTable *csp_table(CspBuilder *b, const char *section, const char *type, uint32_t count)
{
    CspSection *s = csp_section(b, section);
    CspTable *t = &s->tables[s->ntables++];
    t->type = csp_type_named(b, type);
    t->count = count;
    t->records = calloc(count ? count : 1, b->types[t->type].size);
    return t;
}

static void csp_raw(CspBuilder *b, const char *id, const void *data, size_t size)
{
    CspSection *s = csp_section(b, id);
    s->raw = data;
    s->raw_size = size;
}

static uint8_t *csp_field_at(CspBuilder *b, CspTable *t, uint32_t record, const char *field, int index, int *type)
{
    const CspType *rt = &b->types[t->type];
    for (int i = 0; i < rt->nfields; i++) {
        if (strcmp(rt->fields[i].name, field) == 0) {
            *type = rt->fields[i].type;
            return t->records + (size_t)record * rt->size + rt->offsets[i] + (size_t)index * csp_field_size(*type);
        }
    }
    fprintf(stderr, "csp_builder: %s has no field %s\n", rt->name, field);
    exit(2);
}

static void csp_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t csp_string(CspBuilder *b, const char *text)
{
    uint32_t offset = (uint32_t)b->strings_size;
    size_t n = strlen(text) + 1;
    memcpy(b->strings + b->strings_size, text, n);
    b->strings_size += n;
    return offset;
}

/* A field (or one value of an array field) of a record: numbers, references (CSP_NONE for none) and strings. */
static void csp_set(CspBuilder *b, CspTable *t, uint32_t record, const char *field, int index, double value)
{
    int type;
    uint8_t *p = csp_field_at(b, t, record, field, index, &type);
    if (type == CSP_U8) {
        p[0] = (uint8_t)value;
    } else if (type == CSP_U16) {
        p[0] = (uint8_t)(int)value;
        p[1] = (uint8_t)((int)value >> 8);
    } else if (type == CSP_F32) {
        float f = (float)value;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        csp_put_u32(p, bits);
    } else {
        csp_put_u32(p, value < 0 ? (uint32_t)(int32_t)value : (uint32_t)value);
    }
}

static void csp_set_str(CspBuilder *b, CspTable *t, uint32_t record, const char *field, const char *text)
{
    int type;
    uint8_t *p = csp_field_at(b, t, record, field, 0, &type);
    csp_put_u32(p, text ? csp_string(b, text) : CSP_NONE);
}

static void csp_set_matrix(CspBuilder *b, CspTable *t, uint32_t record, const float m[16])
{
    for (int i = 0; i < 16; i++) {
        csp_set(b, t, record, "matrix", i, m[i]);
    }
}

/* The file: header, section table, the sections (SCHM and STRS from what was built), each on 16 bytes. */
typedef struct CspBytes {
    uint8_t *data;
    size_t size, cap;
} CspBytes;

static void csp_append(CspBytes *out, const void *data, size_t size)
{
    if (out->size + size > out->cap) {
        out->cap = (out->size + size) * 2 + 256;
        out->data = realloc(out->data, out->cap);
    }
    memcpy(out->data + out->size, data, size);
    out->size += size;
}

static void csp_append_u32(CspBytes *out, uint32_t v)
{
    uint8_t p[4];
    csp_put_u32(p, v);
    csp_append(out, p, 4);
}

static void csp_append_name(CspBytes *out, const char *name)
{
    uint16_t n = (uint16_t)strlen(name);
    uint8_t len[2] = { (uint8_t)n, (uint8_t)(n >> 8) };
    csp_append(out, len, 2);
    csp_append(out, name, n);
}

static CspBytes csp_schema_bytes(const CspBuilder *b)
{
    CspBytes out = { 0 };
    csp_append_u32(&out, (uint32_t)b->ntypes);
    for (int i = 0; i < b->ntypes; i++) {
        const CspType *t = &b->types[i];
        csp_append_name(&out, t->name);
        csp_append_u32(&out, t->size);
        csp_append_u32(&out, (uint32_t)t->nfields);
        for (int f = 0; f < t->nfields; f++) {
            csp_append_name(&out, t->fields[f].name);
            uint8_t code[4] = { (uint8_t)t->fields[f].type, 0, (uint8_t)t->fields[f].count,
                                (uint8_t)(t->fields[f].count >> 8) };
            csp_append(&out, code, 4);
            csp_append_u32(&out, t->offsets[f]);
        }
    }
    return out;
}

static CspBytes csp_tables_bytes(const CspBuilder *b, const CspSection *s)
{
    CspBytes out = { 0 };
    csp_append_u32(&out, (uint32_t)s->ntables);
    for (int i = 0; i < s->ntables; i++) {
        const CspTable *t = &s->tables[i];
        csp_append_u32(&out, (uint32_t)t->type);
        csp_append_u32(&out, t->count);
        csp_append(&out, t->records, (size_t)t->count * b->types[t->type].size);
    }
    return out;
}

static uint8_t *csp_finish(CspBuilder *b, size_t *size)
{
    CspBytes schema = csp_schema_bytes(b);
    csp_raw(b, "SCHM", schema.data, schema.size);
    csp_raw(b, "STRS", b->strings, b->strings_size);
    CspBytes out = { 0 };
    uint8_t header[32] = { 'C', 'U', 'T', 'P', (uint8_t)(b->major ? b->major : 1), 0, 0, 0 };
    csp_put_u32(header + 8, (uint32_t)b->nsections);
    csp_append(&out, header, sizeof(header));
    size_t table_at = out.size;
    uint8_t blank[24] = { 0 };
    for (int i = 0; i < b->nsections; i++) {
        csp_append(&out, blank, sizeof(blank));
    }
    for (int i = 0; i < b->nsections; i++) {
        const CspSection *s = &b->sections[i];
        while (out.size % 16) {
            csp_append(&out, "", 1);
        }
        CspBytes tables = { 0 };
        if (!s->raw) {
            tables = csp_tables_bytes(b, s);
        }
        size_t offset = out.size, length = s->raw ? s->raw_size : tables.size;
        csp_append(&out, s->raw ? s->raw : tables.data, length);
        free(tables.data);
        uint8_t *entry = out.data + table_at + 24 * (size_t)i;
        memcpy(entry, s->id, 4);
        csp_put_u32(entry + 4, 1);
        csp_put_u32(entry + 8, (uint32_t)offset);
        csp_put_u32(entry + 16, (uint32_t)length);
    }
    free(schema.data);
    *size = out.size;
    return out.data;
}

static void csp_free(CspBuilder *b)
{
    for (int i = 0; i < b->nsections; i++) {
        for (int t = 0; t < b->sections[i].ntables; t++) {
            free(b->sections[i].tables[t].records);
        }
    }
}

#endif /* CSP_BUILDER_H */
