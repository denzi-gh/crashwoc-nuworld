#include "save/savestore.h"

#include <SDL3/SDL.h>

#include "save/ps2card.h"

#define NW_SAVE_SIZE (NW_SAVE_RECORDS * NW_SAVE_GAME_SIZE) /* the card's save file */
#define NW_SAVE_COMMENT                                                                                              \
    "Crash Bandicoot: The Wrath of Cortex save (crashwoc-nuworld).\n"                                               \
    "Levels are keyed by their path, hubs by their number; keys this build does not know are kept."

typedef struct NwSaveFiles {
    char raw[32]; /* the card's file name */
    char *main, *bak, *tmp, *bad;
} NwSaveFiles;

static void nw_save_files_free(NwSaveFiles *f)
{
    SDL_free(f->main);
    SDL_free(f->bak);
    SDL_free(f->tmp);
    SDL_free(f->bad);
    SDL_zerop(f);
}

static bool nw_save_files(NwSaveFiles *f, const char *dir, int slot)
{
    SDL_zerop(f);
    if (slot < 0 || slot >= NW_SAVE_CARD_SLOTS) {
        return SDL_SetError("no card slot %d", slot);
    }
    SDL_snprintf(f->raw, sizeof(f->raw), NW_SAVE_CARD_NAME "slot%02d", slot);
    if (SDL_asprintf(&f->main, "%s%s.toml", dir, f->raw) < 0 || SDL_asprintf(&f->bak, "%s.bak", f->main) < 0 ||
        SDL_asprintf(&f->tmp, "%s.tmp", f->main) < 0 || SDL_asprintf(&f->bad, "%s.corrupt", f->main) < 0) {
        nw_save_files_free(f);
        return false;
    }
    return true;
}

/* Puts "path: " before the SDL error. */
static void nw_save_blame(const char *path)
{
    char why[512];
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    SDL_SetError("%s: %s", path, why);
}

/* path as a save document: 1 read, 0 no file, -1 unusable, -2 a newer format (both with an SDL error). Usable means
   that it parses and that its records decode. */
static int nw_save_open(const char *path, const NwSaveRegistry *reg, NwDoc *doc)
{
    if (!SDL_GetPathInfo(path, NULL)) {
        return 0;
    }
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    uint8_t *games = SDL_malloc(NW_SAVE_SIZE);
    int r = -1;
    if (text && games && nw_doc_init(doc)) {
        if (nw_doc_parse(doc, text, size)) {
            r = nw_save_decode(doc, reg, games, NW_SAVE_RECORDS) ? 1 : nw_save_format(doc) > NW_SAVE_FORMAT ? -2 : -1;
        }
        if (r != 1) {
            nw_doc_free(doc);
        }
    }
    SDL_free(games);
    SDL_free(text);
    if (r != 1) {
        nw_save_blame(path);
    }
    return r;
}

/* doc as the file: written to .tmp, the current file copied to .bak when keep, then the rename over it. */
static bool nw_save_write(const NwSaveFiles *f, const NwDoc *doc, bool keep)
{
    size_t size = 0;
    char *text = nw_doc_write(doc, NW_SAVE_COMMENT, &size);
    bool ok = text && SDL_SaveFile(f->tmp, text, size);
    SDL_free(text);
    if (ok && keep && !SDL_CopyFile(f->main, f->bak)) {
        ok = false;
        nw_save_blame(f->bak);
    }
    if (ok && !SDL_RenamePath(f->tmp, f->main)) {
        ok = false;
        nw_save_blame(f->main);
    }
    if (!ok) {
        char why[512];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        SDL_RemovePath(f->tmp);
        SDL_SetError("%s", why);
    }
    return ok;
}

/* The current file before it is replaced: kept (copied to .bak by the write) when it is a save, moved to .corrupt
   when it is not; false with an SDL error when it cannot be moved. */
static bool nw_save_set_aside(const NwSaveFiles *f, int got, bool *keep)
{
    *keep = got == 1 || got == -2;
    if (got == -1 && !SDL_RenamePath(f->main, f->bad)) {
        nw_save_blame(f->bad);
        return false;
    }
    return true;
}

static bool nw_save_count_ok(size_t count)
{
    return (count >= 1 && count <= NW_SAVE_RECORDS) || SDL_SetError("a save holds 1 to %d records", NW_SAVE_RECORDS);
}

NwSaveResult nw_save_import(const char *dir, int slot, const char *file, const NwSaveRegistry *reg)
{
    NwSaveFiles f;
    NwDoc doc;
    size_t size = 0, len = 0;
    bool missing = false, keep = false;
    if (!nw_save_files(&f, dir, slot)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r = NW_SAVE_FAILED;
    uint8_t *data = SDL_LoadFile(file, &size), *raw = NULL;
    if (data && nw_ps2card_is_card(data, size)) {
        raw = nw_ps2card_file(data, size, NW_SAVE_CARD_NAME, f.raw, &len, &missing);
        if (missing) {
            r = NW_SAVE_NONE;
        }
    } else if (data && size == NW_SAVE_SIZE) {
        raw = data;
        len = size;
        data = NULL;
    } else if (data) {
        SDL_SetError("neither a PS2 save (%u bytes) nor a formatted PS2 memory card", (unsigned)NW_SAVE_SIZE);
    }
    if (raw && len != NW_SAVE_SIZE) {
        SDL_SetError("/%s/%s is %u bytes, a save %u", NW_SAVE_CARD_NAME, f.raw, (unsigned)len, (unsigned)NW_SAVE_SIZE);
    } else if (raw) {
        int got = nw_save_open(f.main, reg, &doc);
        if (got == 1) {
            nw_doc_free(&doc);
        }
        if (nw_save_set_aside(&f, got, &keep) && nw_doc_init(&doc)) {
            if (nw_save_merge(&doc, reg, raw, NW_SAVE_RECORDS) && nw_save_write(&f, &doc, keep)) {
                r = NW_SAVE_IMPORTED;
            }
            nw_doc_free(&doc);
        }
    }
    if (r == NW_SAVE_IMPORTED) {
        SDL_SetError("%s", file);
    } else {
        nw_save_blame(file);
    }
    SDL_free(raw);
    SDL_free(data);
    nw_save_files_free(&f);
    return r;
}

static int SDLCALL nw_save_by_name(const void *a, const void *b)
{
    return SDL_strcmp(*(char *const *)a, *(char *const *)b);
}

/* One file of the folder to import: what cannot be imported is logged and counts as no save, so that a stray file
   never makes the slot fail to load. */
static NwSaveResult nw_save_try_import(const char *dir, const char *name, int slot, const NwSaveRegistry *reg)
{
    char *path = NULL;
    NwSaveResult r = SDL_asprintf(&path, "%s%s", dir, name) < 0 ? NW_SAVE_FAILED : nw_save_import(dir, slot, path, reg);
    SDL_free(path);
    if (r == NW_SAVE_FAILED) {
        SDL_Log("saves: not imported: %s", SDL_GetError());
        r = NW_SAVE_NONE;
    }
    return r;
}

/* The folder's PS2 save for the slot, imported once: its raw file, else the first card image (by name) holding it. */
static NwSaveResult nw_save_import_any(const NwSaveFiles *f, const char *dir, int slot, const NwSaveRegistry *reg)
{
    NwSaveResult r = NW_SAVE_NONE;
    char *raw = NULL;
    if (SDL_asprintf(&raw, "%s%s", dir, f->raw) >= 0 && SDL_GetPathInfo(raw, NULL)) {
        r = nw_save_try_import(dir, f->raw, slot, reg);
    }
    SDL_free(raw);
    int n = 0;
    char **cards = r == NW_SAVE_IMPORTED ? NULL : SDL_GlobDirectory(dir, "*.ps2", SDL_GLOB_CASEINSENSITIVE, &n);
    if (cards) {
        SDL_qsort(cards, (size_t)n, sizeof(*cards), nw_save_by_name);
    }
    for (int i = 0; cards && i < n && r != NW_SAVE_IMPORTED; i++) {
        r = nw_save_try_import(dir, cards[i], slot, reg);
    }
    SDL_free(cards);
    return r;
}

NwSaveResult nw_save_load(const char *dir, int slot, const NwSaveRegistry *reg, uint8_t *games, size_t count)
{
    NwSaveFiles f;
    NwDoc doc;
    char why[512] = "";
    if (!nw_save_count_ok(count) || !nw_save_files(&f, dir, slot)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r = NW_SAVE_OK;
    int got = nw_save_open(f.main, reg, &doc);
    if (got == 0) { /* a deleted save leaves its .bak: no data, and no import over it */
        r = SDL_GetPathInfo(f.bak, NULL) ? NW_SAVE_NONE : nw_save_import_any(&f, dir, slot, reg);
        if (r == NW_SAVE_IMPORTED) {
            SDL_strlcpy(why, SDL_GetError(), sizeof(why));
            got = nw_save_open(f.main, reg, &doc);
        }
    } else if (got == -1) {
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        r = NW_SAVE_BACKUP;
        if ((got = nw_save_open(f.bak, reg, &doc)) != 1) {
            SDL_SetError("%s", why);
        }
    }
    if (r != NW_SAVE_NONE) {
        uint8_t *tmp = got == 1 ? SDL_malloc(count * NW_SAVE_GAME_SIZE) : NULL;
        if (tmp && nw_save_decode(&doc, reg, tmp, count)) {
            SDL_memcpy(games, tmp, count * NW_SAVE_GAME_SIZE);
            if (why[0]) {
                SDL_SetError("%s", why);
            }
        } else {
            r = NW_SAVE_FAILED;
        }
        SDL_free(tmp);
        if (got == 1) {
            nw_doc_free(&doc);
        }
    }
    nw_save_files_free(&f);
    return r;
}

bool nw_save_store(const char *dir, int slot, const NwSaveRegistry *reg, const uint8_t *games, size_t count)
{
    NwSaveFiles f;
    NwDoc doc;
    bool keep = false, ok = false;
    if (!nw_save_count_ok(count) || !nw_save_files(&f, dir, slot)) {
        return false;
    }
    int got = nw_save_open(f.main, reg, &doc);
    if (got == -2) { /* a newer build's save stays as it is */
        nw_save_files_free(&f);
        return false;
    }
    if (nw_save_set_aside(&f, got, &keep)) {
        if (got == -1 && nw_save_open(f.bak, reg, &doc) != 1) { /* what the .bak keeps beyond the records */
            got = 0;
        } else if (got == -1) {
            got = 1;
        }
        if (got == 1 || nw_doc_init(&doc)) {
            ok = nw_save_merge(&doc, reg, games, count) && nw_save_write(&f, &doc, keep);
            nw_doc_free(&doc);
        }
    }
    nw_save_files_free(&f);
    return ok;
}

NwSaveResult nw_save_remove(const char *dir, int slot)
{
    NwSaveFiles f;
    if (!nw_save_files(&f, dir, slot)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r = NW_SAVE_NONE;
    if (SDL_GetPathInfo(f.main, NULL)) {
        r = SDL_RenamePath(f.main, f.bak) ? NW_SAVE_OK : NW_SAVE_FAILED;
    }
    nw_save_files_free(&f);
    return r;
}

bool nw_save_exists(const char *dir, int slot)
{
    NwSaveFiles f;
    bool there = nw_save_files(&f, dir, slot) && SDL_GetPathInfo(f.main, NULL);
    nw_save_files_free(&f);
    return there;
}
