/* Record types (docs/csp-spec.md, SCHM): the file says how it wrote each record, the reader says which C structure it
   fills, and the fields are bound by name. A field the file lacks takes the reader's fallback, a field the reader
   does not know is skipped, a field with another type or count is an error. */
#include "port/gcutpc/internal.h"

#define TYPE_CODES 7

typedef struct Cursor {
    const uint8_t *data;
    size_t size, pos;
    NUGCUTPCFILE_s *file;
} Cursor;

static size_t field_size(NuGCutPCFieldType type)
{
    return type == NUGCUTPC_U8 ? 1 : type == NUGCUTPC_U16 ? 2 : 4;
}

static bool cursor_take(Cursor *c, size_t n, const uint8_t **out)
{
    if (n > c->size - c->pos) {
        return NuGCutPCFail(c->file->error, "SCHM ends early");
    }
    *out = c->data + c->pos;
    c->pos += n;
    return true;
}

static bool cursor_u32(Cursor *c, uint32_t *out)
{
    const uint8_t *p;
    if (!cursor_take(c, 4, &p)) {
        return false;
    }
    *out = NuGCutPCRead32(p);
    return true;
}

static bool cursor_name(Cursor *c, const char **out)
{
    const uint8_t *p;
    if (!cursor_take(c, 2, &p)) {
        return false;
    }
    uint16_t length = NuGCutPCRead16(p);
    if (!cursor_take(c, length, &p)) {
        return false;
    }
    char *name = NuGCutPCArenaAlloc(c->file->arena, (size_t)length + 1, 1);
    if (!name) {
        return NuGCutPCFail(c->file->error, "out of memory");
    }
    memcpy(name, p, length);
    *out = name;
    return true;
}

static bool read_field(Cursor *c, const NUGCUTPCWRITTENTYPE_s *type, NUGCUTPCWRITTENFIELD_s *field)
{
    const uint8_t *p;
    if (!cursor_name(c, &field->name) || !cursor_take(c, 8, &p)) {
        return false;
    }
    uint8_t code = p[0];
    field->count = NuGCutPCRead16(p + 2);
    field->offset = NuGCutPCRead32(p + 4);
    if (code < 1 || code > TYPE_CODES) {
        return NuGCutPCFail(c->file->error, "SCHM: %s.%s has type code %u", type->name, field->name, code);
    }
    field->type = (NuGCutPCFieldType)code;
    uint64_t end = (uint64_t)field->offset + (uint64_t)field_size(field->type) * field->count;
    if (end > type->size) {
        return NuGCutPCFail(c->file->error, "SCHM: %s.%s lies outside its record", type->name, field->name);
    }
    return true;
}

static bool read_type(Cursor *c, NUGCUTPCWRITTENTYPE_s *type)
{
    if (!cursor_name(c, &type->name) || !cursor_u32(c, &type->size) || !cursor_u32(c, &type->field_count)) {
        return false;
    }
    if (type->field_count > (c->size - c->pos) / 10) { /* a field takes at least 10 bytes */
        return NuGCutPCFail(c->file->error, "SCHM: %s has more fields than SCHM can hold", type->name);
    }
    NUGCUTPCWRITTENFIELD_s *fields = NuGCutPCArenaAlloc(c->file->arena, type->field_count, sizeof(*fields));
    if (!fields) {
        return NuGCutPCFail(c->file->error, "out of memory");
    }
    for (uint32_t i = 0; i < type->field_count; i++) {
        if (!read_field(c, type, &fields[i])) {
            return false;
        }
    }
    type->fields = fields;
    return true;
}

bool NuGCutPCSchemaRead(NUGCUTPCFILE_s *file, const NUGCUTPCSECTION_s *schm)
{
    Cursor c = { schm->data, schm->size, 0, file };
    uint32_t count;
    if (!cursor_u32(&c, &count)) {
        return false;
    }
    if (count > (c.size - c.pos) / 10) { /* a type takes at least 10 bytes */
        return NuGCutPCFail(file->error, "SCHM: %u record types do not fit", count);
    }
    NUGCUTPCWRITTENTYPE_s *types = NuGCutPCArenaAlloc(file->arena, count, sizeof(*types));
    if (!types && count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!read_type(&c, &types[i])) {
            return false;
        }
    }
    file->types = types;
    file->type_count = count;
    return true;
}

/* Tables. */

typedef struct Table {
    const NUGCUTPCWRITTENTYPE_s *type;
    uint32_t count;
    const uint8_t *records;
} Table;

/* The section's table of the named record type; found false when it has none. */
static bool find_table(NUGCUTPCFILE_s *file, const NUGCUTPCSECTION_s *s, const char *name, Table *out, bool *found)
{
    *found = false;
    if (s->size < 4) {
        return NuGCutPCFail(file->error, "%s: no table count", s->id);
    }
    uint32_t tables = NuGCutPCRead32(s->data);
    size_t pos = 4;
    for (uint32_t i = 0; i < tables; i++) {
        if (s->size - pos < 8) {
            return NuGCutPCFail(file->error, "%s: a table runs past the section", s->id);
        }
        uint32_t type_index = NuGCutPCRead32(s->data + pos), count = NuGCutPCRead32(s->data + pos + 4);
        pos += 8;
        if (type_index >= file->type_count) {
            return NuGCutPCFail(file->error, "%s: record type %u is not in SCHM", s->id, type_index);
        }
        const NUGCUTPCWRITTENTYPE_s *type = &file->types[type_index];
        if (type->size && count > (s->size - pos) / type->size) {
            return NuGCutPCFail(file->error, "%s: the table of %s runs past the section", s->id, type->name);
        }
        if (!*found && strcmp(type->name, name) == 0) {
            *out = (Table){ type, count, s->data + pos };
            *found = true;
        }
        pos += (size_t)type->size * count;
    }
    return true;
}

/* Where the file has the reader's field: NULL when it does not have it. */
static bool bind_field(NUGCUTPCFILE_s *file, const NUGCUTPCWRITTENTYPE_s *type, const NUGCUTPCFIELD_s *field,
                       const NUGCUTPCWRITTENFIELD_s **out)
{
    *out = NULL;
    for (uint32_t i = 0; i < type->field_count; i++) {
        const NUGCUTPCWRITTENFIELD_s *w = &type->fields[i];
        if (strcmp(w->name, field->name) != 0) {
            continue;
        }
        if (w->type != field->type || w->count != field->count) {
            return NuGCutPCFail(file->error, "%s.%s: the file has another type or count", type->name, field->name);
        }
        *out = w;
        return true;
    }
    return true;
}

static bool read_string(NUGCUTPCFILE_s *file, uint32_t offset, const char **out)
{
    if (offset == NUGCUTPC_NONE) {
        *out = NULL;
        return true;
    }
    if (offset >= file->strings_size) {
        return NuGCutPCFail(file->error, "a string at %u outside STRS", offset);
    }
    *out = file->strings + offset;
    return true;
}

/* One value of a field into the C structure; from the file at p, or the fallback when p is NULL. */
static bool store_value(NUGCUTPCFILE_s *file, const NUGCUTPCFIELD_s *field, const uint8_t *p, void *dst)
{
    switch (field->type) {
    case NUGCUTPC_U8:
        *(uint8_t *)dst = p ? p[0] : (uint8_t)field->fallback;
        return true;
    case NUGCUTPC_U16:
        *(uint16_t *)dst = p ? NuGCutPCRead16(p) : (uint16_t)field->fallback;
        return true;
    case NUGCUTPC_U32:
        *(uint32_t *)dst = p ? NuGCutPCRead32(p) : (uint32_t)field->fallback;
        return true;
    case NUGCUTPC_I32:
        *(int32_t *)dst = p ? (int32_t)NuGCutPCRead32(p) : (int32_t)field->fallback;
        return true;
    case NUGCUTPC_F32:
        *(float *)dst = p ? NuGCutPCReadF32(p) : field->fallback;
        return true;
    case NUGCUTPC_REF:
        *(uint32_t *)dst = p ? NuGCutPCRead32(p) : NUGCUTPC_NONE;
        return true;
    case NUGCUTPC_STR:
        return read_string(file, p ? NuGCutPCRead32(p) : NUGCUTPC_NONE, (const char **)dst);
    }
    return NuGCutPCFail(file->error, "%s: unknown field type", field->name);
}

static size_t c_size(NuGCutPCFieldType type)
{
    switch (type) {
    case NUGCUTPC_U8:
        return sizeof(uint8_t);
    case NUGCUTPC_U16:
        return sizeof(uint16_t);
    case NUGCUTPC_STR:
        return sizeof(const char *);
    default:
        return sizeof(uint32_t); /* u32, i32, f32 and ref are 4 bytes in C as in the file */
    }
}

static bool store_record(NUGCUTPCFILE_s *file, const NUGCUTPCRECORD_s *record,
                         const NUGCUTPCWRITTENFIELD_s *const *bound, const uint8_t *src, unsigned char *dst)
{
    for (size_t f = 0; f < record->field_count; f++) {
        const NUGCUTPCFIELD_s *field = &record->fields[f];
        for (uint16_t i = 0; i < field->count; i++) {
            const uint8_t *p = bound[f] ? src + bound[f]->offset + i * field_size(field->type) : NULL;
            if (!store_value(file, field, p, dst + field->offset + i * c_size(field->type))) {
                return false;
            }
        }
    }
    return true;
}

#define MAX_FIELDS 32

bool NuGCutPCTableRead(NUGCUTPCFILE_s *file, const char *section, const NUGCUTPCRECORD_s *record, void **records,
                       uint32_t *count)
{
    *records = NULL;
    *count = 0;
    if (record->field_count > MAX_FIELDS) {
        return NuGCutPCFail(file->error, "%s: the reader describes more than %d fields", record->name, MAX_FIELDS);
    }
    const NUGCUTPCSECTION_s *s = NuGCutPCFileSection(file, section);
    if (!s) {
        return true;
    }
    Table table;
    bool found;
    if (!find_table(file, s, record->name, &table, &found)) {
        return false;
    }
    if (!found) {
        return true;
    }
    const NUGCUTPCWRITTENFIELD_s *bound[MAX_FIELDS];
    for (size_t f = 0; f < record->field_count; f++) {
        if (!bind_field(file, table.type, &record->fields[f], &bound[f])) {
            return false;
        }
    }
    unsigned char *out = NuGCutPCArenaAlloc(file->arena, table.count, record->size);
    if (!out && table.count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < table.count; i++) {
        if (!store_record(file, record, bound, table.records + (size_t)i * table.type->size, out + i * record->size)) {
            return false;
        }
    }
    *records = out;
    *count = table.count;
    return true;
}
