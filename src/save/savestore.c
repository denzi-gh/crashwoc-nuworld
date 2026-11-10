#include "save/savestore.h"

#include <SDL3/SDL.h>

#include "save/ps2card.h"

#define NW_SAVE_PS2_SIZE (NW_SAVE_PS2_GAMES * NW_SAVE_GAME_SIZE) /* a PS2 save's file */
#define NW_SAVE_PS2_FILE NW_SAVE_PS2_NAME "slot00"                /* its name, as the game wrote it */
#define NW_SAVE_TIME_KEY "saved"
#define NW_SAVE_COMMENT                                                                                              \
    "Crash Bandicoot: The Wrath of Cortex save (crashwoc-nuworld).\n"                                               \
    "Levels are keyed by their path, hubs by their number; keys this build does not know are kept."

typedef struct NwSaveFiles {
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

/* base.toml in dir and its siblings. */
static bool nw_save_files_at(NwSaveFiles *f, const char *dir, const char *base)
{
    SDL_zerop(f);
    if (SDL_asprintf(&f->main, "%s%s.toml", dir, base) < 0 || SDL_asprintf(&f->bak, "%s.bak", f->main) < 0 ||
        SDL_asprintf(&f->tmp, "%s.tmp", f->main) < 0 || SDL_asprintf(&f->bad, "%s.corrupt", f->main) < 0) {
        nw_save_files_free(f);
        return false;
    }
    return true;
}

static bool nw_save_files(NwSaveFiles *f, const char *dir, int n)
{
    char base[16];
    SDL_zerop(f);
    if (n < 0 || n >= NW_SAVE_GAMES) {
        return SDL_SetError("no save %d", n + 1);
    }
    SDL_snprintf(base, sizeof(base), "save%03d", n + 1);
    return nw_save_files_at(f, dir, base);
}

/* Puts "path: " before the SDL error. */
static void nw_save_blame(const char *path)
{
    char why[512];
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    SDL_SetError("%s: %s", path, why);
}

static bool nw_save_empty(const uint8_t *game)
{
    return game[NW_SAVE_EMPTY] != 0;
}

/* 1 read, 0 no file, -1 unusable (does not parse or decode), -2 a newer format; the last two set an SDL error. */
static int nw_save_open(const char *path, const NwSaveRegistry *reg, NwDoc *doc)
{
    if (!SDL_GetPathInfo(path, NULL)) {
        return 0;
    }
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    uint8_t *games = SDL_malloc(NW_SAVE_PS2_SIZE);
    int r = -1;
    if (text && games && nw_doc_init(doc)) {
        if (nw_doc_parse(doc, text, size)) {
            r = nw_save_decode(doc, reg, games, NW_SAVE_PS2_GAMES) ? 1 : nw_save_format(doc) > NW_SAVE_FORMAT ? -2 : -1;
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

/* f's file into doc: NW_SAVE_OK, NW_SAVE_BACKUP (the .bak; the SDL error says why the file did not load),
   NW_SAVE_NONE (no file) or NW_SAVE_FAILED (an SDL error); doc is set for the OK ones. */
static NwSaveResult nw_save_read(const NwSaveFiles *f, const NwSaveRegistry *reg, NwDoc *doc)
{
    char why[512];
    int got = nw_save_open(f->main, reg, doc);
    if (got != -1) {
        return got == 1 ? NW_SAVE_OK : got == 0 ? NW_SAVE_NONE : NW_SAVE_FAILED;
    }
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    got = nw_save_open(f->bak, reg, doc);
    SDL_SetError("%s", why);
    return got == 1 ? NW_SAVE_BACKUP : NW_SAVE_FAILED;
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

static bool nw_save_time_valid(const NwSaveTime *t)
{
    return t->year >= 1 && t->year <= 9999 && t->month >= 1 && t->month <= 12 && t->day >= 1 && t->day <= 31 &&
           t->hour >= 0 && t->hour <= 23 && t->minute >= 0 && t->minute <= 59 && t->second >= 0 && t->second <= 60;
}

/* The document's "saved" as "YYYY-MM-DDTHH:MM:SS". */
static bool nw_save_time_set(NwDoc *doc, const NwSaveTime *t)
{
    char text[32];
    NwDocValue v;
    if (!nw_save_time_valid(t)) {
        return SDL_SetError("no save time %d-%d-%d %d:%d:%d", t->year, t->month, t->day, t->hour, t->minute,
                            t->second);
    }
    int len = SDL_snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d", t->year, t->month, t->day, t->hour,
                           t->minute, t->second);
    return nw_doc_str(&v, text, (size_t)len) && nw_doc_set(doc, doc->tables[0], NW_SAVE_TIME_KEY, &v);
}

/* The document's "saved"; all zero when it has none that reads. */
static void nw_save_time_get(const NwDoc *doc, NwSaveTime *t)
{
    const NwDocValue *v = nw_doc_get(doc->tables[0], NW_SAVE_TIME_KEY);
    char text[32];
    SDL_zerop(t);
    if (v && v->type == NW_DOC_STR && v->len < sizeof(text)) {
        SDL_memcpy(text, v->bytes, v->len);
        text[v->len] = 0;
        if (SDL_sscanf(text, "%d-%d-%dT%d:%d:%d", &t->year, &t->month, &t->day, &t->hour, &t->minute, &t->second) !=
                6 ||
            !nw_save_time_valid(t)) {
            SDL_zerop(t);
        }
    }
}

void nw_save_time_from(int64_t time, NwSaveTime *t)
{
    SDL_DateTime dt;
    SDL_zerop(t);
    if (SDL_TimeToDateTime(time, &dt, true)) {
        *t = (NwSaveTime){ dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second };
    }
}

NwSaveResult nw_save_load(const char *dir, int n, const NwSaveRegistry *reg, uint8_t *game, NwSaveTime *saved)
{
    NwSaveFiles f;
    NwDoc doc;
    uint8_t tmp[NW_SAVE_GAME_SIZE];
    char why[512];
    if (!nw_save_files(&f, dir, n)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r = nw_save_read(&f, reg, &doc);
    if (r == NW_SAVE_OK || r == NW_SAVE_BACKUP) {
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        if (!nw_save_decode(&doc, reg, tmp, 1)) {
            r = NW_SAVE_FAILED;
        } else if (nw_save_empty(tmp)) { /* never written so: a stray file counts as no save */
            r = NW_SAVE_NONE;
        } else {
            SDL_memcpy(game, tmp, sizeof(tmp));
            if (saved) {
                nw_save_time_get(&doc, saved);
            }
            SDL_SetError("%s", why);
        }
        nw_doc_free(&doc);
    }
    nw_save_files_free(&f);
    return r;
}

bool nw_save_store(const char *dir, int n, const NwSaveRegistry *reg, const uint8_t *game, const NwSaveTime *saved)
{
    NwSaveFiles f;
    NwDoc doc;
    NwSaveTime now;
    bool keep = false, ok = false;
    if (nw_save_empty(game)) {
        return SDL_SetError("save %d: an empty game", n + 1);
    }
    if (!saved) {
        SDL_Time t = 0;
        SDL_GetCurrentTime(&t);
        nw_save_time_from(t, &now);
        saved = &now;
    }
    if (!nw_save_files(&f, dir, n)) {
        return false;
    }
    int got = nw_save_open(f.main, reg, &doc);
    if (got != -2 && nw_save_set_aside(&f, got, &keep)) { /* a newer build's save stays as it is */
        if (got == -1 && nw_save_open(f.bak, reg, &doc) != 1) { /* what the .bak keeps beyond the record */
            got = 0;
        } else if (got == -1) {
            got = 1;
        }
        if (got == 1 || nw_doc_init(&doc)) {
            ok = nw_save_merge(&doc, reg, game, 1) && nw_save_time_set(&doc, saved) && nw_save_write(&f, &doc, keep);
            nw_doc_free(&doc);
        }
    }
    nw_save_files_free(&f);
    return ok;
}

NwSaveResult nw_save_remove(const char *dir, int n)
{
    NwSaveFiles f;
    if (!nw_save_files(&f, dir, n)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r = NW_SAVE_NONE;
    if (SDL_GetPathInfo(f.main, NULL)) {
        r = SDL_RenamePath(f.main, f.bak) ? NW_SAVE_OK : NW_SAVE_FAILED;
        if (r == NW_SAVE_FAILED) {
            nw_save_blame(f.bak);
        }
    }
    nw_save_files_free(&f);
    return r;
}

bool nw_save_exists(const char *dir, int n)
{
    NwSaveFiles f;
    bool there = nw_save_files(&f, dir, n) && SDL_GetPathInfo(f.main, NULL);
    nw_save_files_free(&f);
    return there;
}

bool nw_save_started(const char *dir)
{
    bool started = false;
    for (int n = 0; n < NW_SAVE_GAMES && !started; n++) {
        NwSaveFiles f;
        started = nw_save_files(&f, dir, n) && (SDL_GetPathInfo(f.main, NULL) || SDL_GetPathInfo(f.bak, NULL));
        nw_save_files_free(&f);
    }
    return started;
}

int nw_save_scan(const char *dir, int *last)
{
    int latest = -1;
    SDL_Time best = 0;
    if (last) {
        *last = -1;
    }
    for (int n = 0; n < NW_SAVE_GAMES; n++) {
        NwSaveFiles f;
        SDL_PathInfo info;
        if (nw_save_files(&f, dir, n) && SDL_GetPathInfo(f.main, &info)) {
            if (last) {
                *last = n;
            }
            if (latest < 0 || info.modify_time >= best) { /* the same time: the later save */
                latest = n;
                best = info.modify_time;
            }
        }
        nw_save_files_free(&f);
    }
    return latest;
}

/* -- conversion ------------------------------------------------------------------------------------------------- */

/* The checked games of the PS2 save in file, a raw save or a card image; SDL_free them. NULL with *none when the
   card holds no such save, else with an SDL error. */
static uint8_t *nw_save_ps2_games(const char *file, const NwSaveRegistry *reg, bool *none)
{
    size_t size = 0, len = 0;
    bool missing = false;
    uint8_t *data = SDL_LoadFile(file, &size), *raw = NULL;
    *none = false;
    if (data && nw_ps2card_is_card(data, size)) {
        raw = nw_ps2card_file(data, size, NW_SAVE_PS2_NAME, NW_SAVE_PS2_FILE, &len, &missing);
        *none = missing;
    } else if (data && size == NW_SAVE_PS2_SIZE) {
        raw = data;
        len = size;
        data = NULL;
    } else if (data) {
        SDL_SetError("neither a PS2 save (%u bytes) nor a formatted PS2 memory card image", (unsigned)NW_SAVE_PS2_SIZE);
    }
    SDL_free(data);
    if (raw && len != NW_SAVE_PS2_SIZE) {
        SDL_SetError("/%s/%s is %u bytes, a save %u", NW_SAVE_PS2_NAME, NW_SAVE_PS2_FILE, (unsigned)len,
                     (unsigned)NW_SAVE_PS2_SIZE);
    } else if (raw && nw_save_check(reg, raw, NW_SAVE_PS2_GAMES)) {
        return raw;
    }
    SDL_free(raw);
    if (!*none) {
        nw_save_blame(file);
    }
    return NULL;
}

static int SDLCALL nw_save_by_name(const void *a, const void *b)
{
    return SDL_strcmp(*(char *const *)a, *(char *const *)b);
}

/* The folder's PS2 save: its raw file, else the first card image by name that holds it; NULL when there is none.
   A file that does not convert is logged and passed over, so a stray file never stops the saves. */
static uint8_t *nw_save_ps2_folder(const char *dir, const NwSaveRegistry *reg)
{
    uint8_t *games = NULL;
    int n = 0;
    char *path = NULL;
    bool none = false;
    if (SDL_asprintf(&path, "%s%s", dir, NW_SAVE_PS2_FILE) >= 0 && SDL_GetPathInfo(path, NULL) &&
        !(games = nw_save_ps2_games(path, reg, &none)) && !none) {
        SDL_Log("saves: not converted: %s", SDL_GetError());
    }
    SDL_free(path);
    char **cards = games ? NULL : SDL_GlobDirectory(dir, "*.ps2", SDL_GLOB_CASEINSENSITIVE, &n);
    if (cards) {
        SDL_qsort(cards, (size_t)n, sizeof(*cards), nw_save_by_name);
    }
    for (int i = 0; cards && i < n && !games; i++) {
        path = NULL;
        if (SDL_asprintf(&path, "%s%s", dir, cards[i]) >= 0 && !(games = nw_save_ps2_games(path, reg, &none)) &&
            !none) {
            SDL_Log("saves: not converted: %s", SDL_GetError());
        }
        SDL_free(path);
    }
    SDL_free(cards);
    return games;
}

/* An older NuWorld build's file of 4 games (or its .bak); NULL when there is none, *failed with an SDL error. */
static uint8_t *nw_save_older(const NwSaveFiles *f, const NwSaveRegistry *reg, bool *failed)
{
    NwDoc doc;
    uint8_t *games = NULL;
    NwSaveResult r = nw_save_read(f, reg, &doc);
    *failed = r == NW_SAVE_FAILED;
    if (r == NW_SAVE_OK || r == NW_SAVE_BACKUP) {
        games = SDL_malloc(NW_SAVE_PS2_SIZE);
        if (!games || !nw_save_decode(&doc, reg, games, NW_SAVE_PS2_GAMES)) {
            SDL_free(games);
            games = NULL;
            *failed = true;
        }
        nw_doc_free(&doc);
    }
    return games;
}

int nw_save_convert(const char *dir, const NwSaveRegistry *reg)
{
    NwSaveFiles older;
    bool failed = false;
    if (nw_save_started(dir) || !nw_save_files_at(&older, dir, NW_SAVE_PS2_FILE)) {
        return 0;
    }
    uint8_t *games = nw_save_older(&older, reg, &failed);
    bool from_older = games != NULL;
    NwSaveTime when = { 0 };
    SDL_PathInfo info;
    if (from_older && SDL_GetPathInfo(older.main, &info)) {
        nw_save_time_from(info.modify_time, &when);
    }
    if (!games && !failed) {
        games = nw_save_ps2_folder(dir, reg);
    }
    int made = 0;
    bool ok = !failed;
    for (int i = 0; games && i < NW_SAVE_PS2_GAMES && ok; i++) {
        const uint8_t *game = games + i * NW_SAVE_GAME_SIZE;
        if (!nw_save_empty(game)) {
            ok = nw_save_store(dir, i, reg, game, when.year ? &when : NULL);
            made += ok;
        }
    }
    char *moved = NULL;
    if (ok && from_older && (SDL_asprintf(&moved, "%s.converted", older.main) < 0 ||
                             !SDL_RenamePath(older.main, moved))) {
        ok = false;
        nw_save_blame(older.main);
    }
    SDL_free(moved);
    SDL_free(games);
    nw_save_files_free(&older);
    return ok ? made : -1;
}

int nw_save_import(const char *dir, const char *file, const NwSaveRegistry *reg)
{
    int free_saves[NW_SAVE_PS2_GAMES], games = 0, found = 0;
    bool none = false;
    uint8_t *raw = nw_save_ps2_games(file, reg, &none);
    if (!raw) {
        return none ? 0 : -1;
    }
    for (int i = 0; i < NW_SAVE_PS2_GAMES; i++) {
        games += !nw_save_empty(raw + i * NW_SAVE_GAME_SIZE);
    }
    for (int n = 0; n < NW_SAVE_GAMES && found < games; n++) {
        if (!nw_save_exists(dir, n)) {
            free_saves[found++] = n;
        }
    }
    int made = 0;
    if (found < games) {
        SDL_SetError("%d games, %d free saves", games, found);
        made = -1;
    }
    for (int i = 0; i < NW_SAVE_PS2_GAMES && made >= 0; i++) {
        const uint8_t *game = raw + i * NW_SAVE_GAME_SIZE;
        if (!nw_save_empty(game)) {
            made = nw_save_store(dir, free_saves[made], reg, game, NULL) ? made + 1 : -1;
        }
    }
    SDL_free(raw);
    if (made < 0) {
        nw_save_blame(file);
    }
    return made;
}
