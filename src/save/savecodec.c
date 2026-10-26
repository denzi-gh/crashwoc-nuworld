#include "save/savecodec.h"

#include <SDL3/SDL.h>

typedef enum NwSaveKind {
    NW_SAVE_U8,
    NW_SAVE_S8,
    NW_SAVE_U16,
    NW_SAVE_U32,
    NW_SAVE_FLAG, /* a u8 written as a bool while it is 0 or 1 */
    NW_SAVE_TEXT, /* bytes, written as a string without its trailing NULs */
} NwSaveKind;

typedef struct NwSaveField {
    const char *name;
    uint16_t at;
    uint8_t size;
    uint8_t kind;
    uint8_t hex;   /* hex digits written; 0 = decimal */
    bool optional; /* written only when nonzero */
} NwSaveField;

#define NW_SAVE_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const NwSaveField nw_save_slot_fields[] = {
    { "empty", NW_SAVE_EMPTY, 1, NW_SAVE_FLAG, 0, false },
    { "name", NW_SAVE_NAME, NW_SAVE_NAME_SIZE, NW_SAVE_TEXT, 0, false },
    { "lives", NW_SAVE_LIVES, 1, NW_SAVE_U8, 0, false },
    { "wumpas", NW_SAVE_WUMPAS, 1, NW_SAVE_U8, 0, false },
    { "mask", NW_SAVE_MASK, 1, NW_SAVE_U8, 0, false },
    { "powerbits", NW_SAVE_POWERBITS, 1, NW_SAVE_U8, 2, false },
    { "cutbits", NW_SAVE_CUTBITS, 4, NW_SAVE_U32, 8, false },
    /* derived (CalculateGamePercentage), kept so that a card's records come back unchanged */
    { "percent", NW_SAVE_PERCENT, 1, NW_SAVE_U8, 0, false },
    { "crystals", NW_SAVE_CRYSTALS, 1, NW_SAVE_U8, 0, false },
    { "relics", NW_SAVE_RELICS, 1, NW_SAVE_U8, 0, false },
    { "crate_gems", NW_SAVE_CRATE_GEMS, 1, NW_SAVE_U8, 0, false },
    { "bonus_gems", NW_SAVE_BONUS_GEMS, 1, NW_SAVE_U8, 0, false },
    { "gems", NW_SAVE_GEMS, 1, NW_SAVE_U8, 0, false },
    { "gembits", NW_SAVE_GEMBITS, 1, NW_SAVE_U8, 2, false },
};

static const NwSaveField nw_save_option_fields[] = {
    { "vibration", NW_SAVE_VIBRATION, 1, NW_SAVE_U8, 0, false },
    { "surround", NW_SAVE_SURROUND, 1, NW_SAVE_U8, 0, false },
    { "sfx_volume", NW_SAVE_SFX_VOLUME, 1, NW_SAVE_U8, 0, false },
    { "music_volume", NW_SAVE_MUSIC_VOLUME, 1, NW_SAVE_U8, 0, false },
    { "screen_x", NW_SAVE_SCREEN_X, 1, NW_SAVE_S8, 0, false },
    { "screen_y", NW_SAVE_SCREEN_Y, 1, NW_SAVE_S8, 0, false },
    { "language", NW_SAVE_LANGUAGE, 1, NW_SAVE_U8, 0, false },
};

static const NwSaveField nw_save_hub_fields[] = {
    { "flags", NW_SAVE_HUB_FLAGS, 1, NW_SAVE_U8, 2, false },
    { "crystals", NW_SAVE_HUB_CRYSTALS, 1, NW_SAVE_U8, 0, false },
    { "pad", NW_SAVE_HUB_PAD, 2, NW_SAVE_U16, 4, true },
};

static const NwSaveField nw_save_level_fields[] = {
    { "flags", NW_SAVE_LEVEL_FLAGS, 2, NW_SAVE_U16, 4, false },
    { "pad", NW_SAVE_LEVEL_PAD, 2, NW_SAVE_U16, 4, true },
};

bool nw_save_registry_init(NwSaveRegistry *reg, const char *const *paths, size_t levels, size_t hubs)
{
    SDL_zerop(reg);
    reg->levels = SDL_calloc(levels ? levels : 1, sizeof(*reg->levels));
    reg->hubs = SDL_calloc(hubs ? hubs : 1, sizeof(*reg->hubs));
    if (!reg->levels || !reg->hubs) {
        nw_save_registry_free(reg);
        return false;
    }
    for (; reg->level_count < levels; reg->level_count++) {
        size_t i = reg->level_count;
        const char *path = paths ? paths[i] : NULL;
        char *key = path && *path ? SDL_strdup(path) : NULL;
        for (char *c = key; c && *c; c++) {
            *c = *c == '\\' ? '/' : (char)SDL_tolower((unsigned char)*c);
        }
        for (size_t j = 0; key && j < i; j++) {
            if (SDL_strcmp(reg->levels[j], key) == 0) {
                SDL_free(key);
                key = NULL;
            }
        }
        if (!key && SDL_asprintf(&key, "#%u", (unsigned)i) < 0) {
            nw_save_registry_free(reg);
            return false;
        }
        reg->levels[i] = key;
    }
    for (; reg->hub_count < hubs; reg->hub_count++) {
        if (SDL_asprintf(&reg->hubs[reg->hub_count], "%u", (unsigned)reg->hub_count) < 0) {
            nw_save_registry_free(reg);
            return false;
        }
    }
    return true;
}

void nw_save_registry_free(NwSaveRegistry *reg)
{
    for (size_t i = 0; reg->levels && i < reg->level_count; i++) {
        SDL_free(reg->levels[i]);
    }
    for (size_t i = 0; reg->hubs && i < reg->hub_count; i++) {
        SDL_free(reg->hubs[i]);
    }
    SDL_free(reg->levels);
    SDL_free(reg->hubs);
    SDL_zerop(reg);
}

/* The records hold one entry per registry key, and two records must not share one. */
static bool nw_save_registry_ok(const NwSaveRegistry *reg)
{
    if (reg->level_count != NW_SAVE_LEVELS || reg->hub_count != NW_SAVE_HUBS) {
        return SDL_SetError("the registry has %u levels and %u hubs, the records %d and %d",
                            (unsigned)reg->level_count, (unsigned)reg->hub_count, NW_SAVE_LEVELS, NW_SAVE_HUBS);
    }
    for (size_t i = 0; i < reg->level_count; i++) {
        for (size_t j = 0; j < i; j++) {
            if (SDL_strcmp(reg->levels[i], reg->levels[j]) == 0) {
                return SDL_SetError("two levels have the key \"%s\"", reg->levels[i]);
            }
        }
    }
    for (size_t i = 0; i < reg->hub_count; i++) {
        for (size_t j = 0; j < i; j++) {
            if (SDL_strcmp(reg->hubs[i], reg->hubs[j]) == 0) {
                return SDL_SetError("two hubs have the key \"%s\"", reg->hubs[i]);
            }
        }
    }
    return true;
}

int64_t nw_save_format(const NwDoc *doc)
{
    const NwDocValue *v = nw_doc_get(doc->tables[0], "format");
    return v && v->type == NW_DOC_INT ? v->i : -1;
}

static uint32_t nw_save_read(const uint8_t *p, size_t size)
{
    uint32_t v = 0;
    for (size_t i = size; i-- > 0;) {
        v = v << 8 | p[i];
    }
    return v;
}

static void nw_save_write(uint8_t *p, size_t size, uint32_t v)
{
    for (size_t i = 0; i < size; i++, v >>= 8) {
        p[i] = (uint8_t)v;
    }
}

static bool nw_save_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (p[i]) {
            return false;
        }
    }
    return true;
}

/* Bytes without their trailing NULs as a string value. */
static bool nw_save_text(NwDocValue *v, const uint8_t *p, size_t size)
{
    while (size && !p[size - 1]) {
        size--;
    }
    return nw_doc_str(v, p, size);
}

static bool nw_save_set_fields(NwDoc *doc, NwDocTable *t, const NwSaveField *fields, size_t n, const uint8_t *rec)
{
    for (size_t i = 0; i < n; i++) {
        const NwSaveField *f = &fields[i];
        uint32_t u = f->kind == NW_SAVE_TEXT ? 0 : nw_save_read(rec + f->at, f->size);
        NwDocValue v;
        if (f->kind == NW_SAVE_TEXT) {
            if (!nw_save_text(&v, rec + f->at, f->size)) {
                return false;
            }
        } else if (f->optional && !u) {
            nw_doc_unset(t, f->name);
            continue;
        } else if (f->kind == NW_SAVE_S8) {
            v = nw_doc_int((int8_t)u, 0);
        } else if (f->kind == NW_SAVE_FLAG && u <= 1) {
            v = nw_doc_bool(u != 0);
        } else {
            v = nw_doc_int(u, f->hex);
        }
        if (!nw_doc_set(doc, t, f->name, &v)) {
            return false;
        }
    }
    return true;
}

static bool nw_save_set_times(NwDoc *doc, NwDocTable *t, const uint8_t *level)
{
    NwDocValue times;
    if (!nw_doc_array(&times, NW_SAVE_TIMES)) {
        return false;
    }
    for (size_t i = 0; i < NW_SAVE_TIMES; i++) {
        const uint8_t *time = level + NW_SAVE_LEVEL_TIME + i * NW_SAVE_TIME_SIZE;
        NwDocValue *pair = &times.items[i];
        if (!nw_doc_array(pair, 2) ||
            !nw_save_text(&pair->items[0], time + NW_SAVE_TIME_NAME, NW_SAVE_TIME_NAME_SIZE)) {
            nw_doc_value_free(&times);
            return false;
        }
        pair->items[1] = nw_doc_int(nw_save_read(time + NW_SAVE_TIME_ITIME, 4), 0);
    }
    return nw_doc_set(doc, t, "time", &times);
}

/* A hub or level record: set when it holds anything; else its fields go, and its table when nothing is left. */
static bool nw_save_put_record(NwDoc *doc, const char *const *path, const uint8_t *rec, size_t size,
                               const NwSaveField *fields, size_t n, bool times)
{
    NwDocTable *t = nw_doc_table(doc, path, 4);
    if (nw_save_zero(rec, size)) {
        if (t) {
            for (size_t i = 0; i < n; i++) {
                nw_doc_unset(t, fields[i].name);
            }
            nw_doc_unset(t, "time");
            if (!t->count) {
                nw_doc_drop_table(doc, t);
            }
        }
        return true;
    }
    if (!t && !(t = nw_doc_add_table(doc, path, 4))) {
        return false;
    }
    return nw_save_set_fields(doc, t, fields, n, rec) && (!times || nw_save_set_times(doc, t, rec));
}

bool nw_save_merge(NwDoc *doc, const NwSaveRegistry *reg, const uint8_t *games, size_t count)
{
    if (!nw_save_registry_ok(reg)) {
        return false;
    }
    int64_t format = nw_save_format(doc);
    if (format > NW_SAVE_FORMAT) {
        return SDL_SetError("save format %" SDL_PRIs64 " is newer than this build's %d", format, NW_SAVE_FORMAT);
    }
    NwDocValue v = nw_doc_int(NW_SAVE_FORMAT, 0);
    if (!nw_doc_set(doc, doc->tables[0], "format", &v)) {
        return false;
    }
    for (size_t s = 0; s < count; s++) {
        const uint8_t *game = games + s * NW_SAVE_GAME_SIZE;
        char index[16];
        SDL_snprintf(index, sizeof(index), "%u", (unsigned)s);
        const char *path[4] = { "slot", index, "options", NULL };
        NwDocTable *t = nw_doc_add_table(doc, path, 2);
        if (!t || !nw_save_set_fields(doc, t, nw_save_slot_fields, NW_SAVE_COUNT(nw_save_slot_fields), game)) {
            return false;
        }
        t = nw_doc_add_table(doc, path, 3);
        if (!t || !nw_save_set_fields(doc, t, nw_save_option_fields, NW_SAVE_COUNT(nw_save_option_fields), game)) {
            return false;
        }
        path[2] = "hub";
        for (size_t h = 0; h < reg->hub_count; h++) {
            path[3] = reg->hubs[h];
            if (!nw_save_put_record(doc, path, game + NW_SAVE_HUB + h * NW_SAVE_HUB_SIZE, NW_SAVE_HUB_SIZE,
                                    nw_save_hub_fields, NW_SAVE_COUNT(nw_save_hub_fields), false)) {
                return false;
            }
        }
        path[2] = "level";
        for (size_t l = 0; l < reg->level_count; l++) {
            path[3] = reg->levels[l];
            if (!nw_save_put_record(doc, path, game + NW_SAVE_LEVEL + l * NW_SAVE_LEVEL_SIZE, NW_SAVE_LEVEL_SIZE,
                                    nw_save_level_fields, NW_SAVE_COUNT(nw_save_level_fields), true)) {
                return false;
            }
        }
    }
    return true;
}

/* Decoding: where names the table in errors. */

static bool nw_save_bad(const char *where, const char *name, const char *want)
{
    return SDL_SetError("[%s] %s: expected %s", where, name, want);
}

static bool nw_save_get_fields(const NwDocTable *t, const NwSaveField *fields, size_t n, uint8_t *rec,
                               const char *where)
{
    static const int64_t hi[] = { 0xFF, 0x7F, 0xFFFF, 0xFFFFFFFF, 0xFF };
    for (size_t i = 0; t && i < n; i++) {
        const NwSaveField *f = &fields[i];
        const NwDocValue *v = nw_doc_get(t, f->name);
        if (!v) {
            continue;
        }
        if (f->kind == NW_SAVE_TEXT) {
            if (v->type != NW_DOC_STR || v->len > f->size) {
                return nw_save_bad(where, f->name, "a string of at most its size in bytes");
            }
            SDL_memcpy(rec + f->at, v->bytes, v->len);
            continue;
        }
        int64_t lo = f->kind == NW_SAVE_S8 ? -0x80 : 0;
        bool type = v->type == NW_DOC_INT || (f->kind == NW_SAVE_FLAG && v->type == NW_DOC_BOOL);
        if (!type || v->i < lo || v->i > hi[f->kind]) {
            return nw_save_bad(where, f->name, "an integer in the field's range");
        }
        nw_save_write(rec + f->at, f->size, (uint32_t)v->i);
    }
    return true;
}

static bool nw_save_get_times(const NwDocTable *t, uint8_t *level, const char *where)
{
    const NwDocValue *v = t ? nw_doc_get(t, "time") : NULL;
    if (!v) {
        return true;
    }
    if (v->type != NW_DOC_ARRAY || v->len > NW_SAVE_TIMES) {
        return nw_save_bad(where, "time", "an array of at most 3 [name, itime]");
    }
    for (size_t i = 0; i < v->len; i++) {
        const NwDocValue *pair = &v->items[i];
        uint8_t *time = level + NW_SAVE_LEVEL_TIME + i * NW_SAVE_TIME_SIZE;
        if (pair->type != NW_DOC_ARRAY || pair->len != 2 || pair->items[0].type != NW_DOC_STR ||
            pair->items[0].len > NW_SAVE_TIME_NAME_SIZE || pair->items[1].type != NW_DOC_INT ||
            pair->items[1].i < 0 || pair->items[1].i > 0xFFFFFFFF) {
            return nw_save_bad(where, "time", "[name of at most 4 bytes, itime]");
        }
        SDL_memcpy(time + NW_SAVE_TIME_NAME, pair->items[0].bytes, pair->items[0].len);
        nw_save_write(time + NW_SAVE_TIME_ITIME, 4, (uint32_t)pair->items[1].i);
    }
    return true;
}

bool nw_save_decode(const NwDoc *doc, const NwSaveRegistry *reg, uint8_t *games, size_t count)
{
    if (!nw_save_registry_ok(reg)) {
        return false;
    }
    int64_t format = nw_save_format(doc);
    if (format < 1) {
        return SDL_SetError("not a save: no format key");
    }
    if (format > NW_SAVE_FORMAT) {
        return SDL_SetError("save format %" SDL_PRIs64 " is newer than this build's %d", format, NW_SAVE_FORMAT);
    }
    SDL_memset(games, 0, count * NW_SAVE_GAME_SIZE);
    for (size_t s = 0; s < count; s++) {
        uint8_t *game = games + s * NW_SAVE_GAME_SIZE;
        char index[16], where[256];
        SDL_snprintf(index, sizeof(index), "%u", (unsigned)s);
        const char *path[4] = { "slot", index, "options", NULL };
        const NwDocTable *t = nw_doc_table(doc, path, 2);
        SDL_snprintf(where, sizeof(where), "slot.%s", index);
        if (!t) {
            game[NW_SAVE_EMPTY] = 1;
        } else if (!nw_save_get_fields(t, nw_save_slot_fields, NW_SAVE_COUNT(nw_save_slot_fields), game, where)) {
            return false;
        }
        SDL_snprintf(where, sizeof(where), "slot.%s.options", index);
        if (!nw_save_get_fields(nw_doc_table(doc, path, 3), nw_save_option_fields,
                                NW_SAVE_COUNT(nw_save_option_fields), game, where)) {
            return false;
        }
        path[2] = "hub";
        for (size_t h = 0; h < reg->hub_count; h++) {
            path[3] = reg->hubs[h];
            SDL_snprintf(where, sizeof(where), "slot.%s.hub.%s", index, path[3]);
            if (!nw_save_get_fields(nw_doc_table(doc, path, 4), nw_save_hub_fields, NW_SAVE_COUNT(nw_save_hub_fields),
                                    game + NW_SAVE_HUB + h * NW_SAVE_HUB_SIZE, where)) {
                return false;
            }
        }
        path[2] = "level";
        for (size_t l = 0; l < reg->level_count; l++) {
            uint8_t *level = game + NW_SAVE_LEVEL + l * NW_SAVE_LEVEL_SIZE;
            path[3] = reg->levels[l];
            SDL_snprintf(where, sizeof(where), "slot.%s.level.%s", index, path[3]);
            t = nw_doc_table(doc, path, 4);
            if (!nw_save_get_fields(t, nw_save_level_fields, NW_SAVE_COUNT(nw_save_level_fields), level, where) ||
                !nw_save_get_times(t, level, where)) {
                return false;
            }
        }
    }
    return true;
}
