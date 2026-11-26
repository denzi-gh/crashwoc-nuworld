/* The CSP container (docs/csp-spec.md, paragraph 2): the header, the table of sections, and the two sections every
   file has besides META: SCHM, the record types it was written with, and STRS, its strings. Every size and offset is
   checked against the file before it is used. */
#include "port/gcutpc/internal.h"

#define CSP_MAGIC "CUTP"
#define CSP_MAJOR 1
#define HEADER_SIZE 32
#define SECTION_ENTRY 24

static bool open_header(NUGCUTPCFILE_s *file, uint32_t *section_count)
{
    if (file->size < HEADER_SIZE) {
        return NuGCutPCFail(file->error, "shorter than the CSP header (%zu bytes)", file->size);
    }
    if (memcmp(file->data, CSP_MAGIC, 4) != 0) {
        return NuGCutPCFail(file->error, "not a CSP file: no \"CUTP\" signature");
    }
    uint16_t major = NuGCutPCRead16(file->data + 4);
    if (major > CSP_MAJOR) {
        return NuGCutPCFail(file->error, "CSP version %u: this reader knows up to %u", major, CSP_MAJOR);
    }
    *section_count = NuGCutPCRead32(file->data + 8);
    if (*section_count > (file->size - HEADER_SIZE) / SECTION_ENTRY) {
        return NuGCutPCFail(file->error, "the table of %u sections runs past the end of the file", *section_count);
    }
    return true;
}

static bool open_sections(NUGCUTPCFILE_s *file, uint32_t count)
{
    NUGCUTPCSECTION_s *sections = NuGCutPCArenaAlloc(file->arena, count, sizeof(*sections));
    if (!sections && count) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = file->data + HEADER_SIZE + (size_t)i * SECTION_ENTRY;
        NUGCUTPCSECTION_s *s = &sections[i];
        memcpy(s->id, entry, 4);
        s->version = NuGCutPCRead32(entry + 4);
        uint64_t offset = NuGCutPCRead64(entry + 8), size = NuGCutPCRead64(entry + 16);
        if (offset > file->size || size > file->size - offset) {
            return NuGCutPCFail(file->error, "section %s runs past the end of the file", s->id);
        }
        s->data = file->data + offset;
        s->size = (size_t)size;
    }
    file->sections = sections;
    file->section_count = count;
    return true;
}

static bool open_strings(NUGCUTPCFILE_s *file, const NUGCUTPCSECTION_s *strs)
{
    /* One byte more than the section, always 0: a string read from it ends inside the copy. */
    char *copy = NuGCutPCArenaAlloc(file->arena, strs->size + 1, 1);
    if (!copy) {
        return NuGCutPCFail(file->error, "out of memory");
    }
    memcpy(copy, strs->data, strs->size);
    file->strings = copy;
    file->strings_size = strs->size;
    return true;
}

bool NuGCutPCFileOpen(NUGCUTPCFILE_s *file, const void *data, size_t size, NUGCUTPCARENA_s *arena,
                      NUGCUTPCERROR_s *error)
{
    *file = (NUGCUTPCFILE_s){ .data = data, .size = size, .arena = arena, .error = error };
    uint32_t count;
    if (!open_header(file, &count) || !open_sections(file, count)) {
        return false;
    }
    static const char *const required[] = { "META", "SCHM", "STRS" };
    for (size_t i = 0; i < sizeof(required) / sizeof(*required); i++) {
        if (!NuGCutPCFileSection(file, required[i])) {
            return NuGCutPCFail(error, "no %s section", required[i]);
        }
    }
    return NuGCutPCSchemaRead(file, NuGCutPCFileSection(file, "SCHM")) &&
           open_strings(file, NuGCutPCFileSection(file, "STRS"));
}

const NUGCUTPCSECTION_s *NuGCutPCFileSection(const NUGCUTPCFILE_s *file, const char *id)
{
    for (uint32_t i = 0; i < file->section_count; i++) {
        if (memcmp(file->sections[i].id, id, 4) == 0) {
            return &file->sections[i];
        }
    }
    return NULL;
}
