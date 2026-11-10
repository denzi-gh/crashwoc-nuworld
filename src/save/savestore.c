#include "save/savestore.h"

#include <SDL3/SDL.h>

#include "save/ps2card.h"
#include "save/saveio.h"

#define NW_SAVE_PS2_SIZE (NW_SAVE_PS2_GAMES * NW_SAVE_GAME_SIZE) /* a PS2 save's file */
#define NW_SAVE_PS2_FILE NW_SAVE_PS2_NAME "slot00"                /* its name, as the game wrote it */
#define NW_SAVE_PS2_ICON "crash.ico"                               /* the save's icon on the card */
#define NW_SAVE_PS2_ICON_SYS 0x3C4                                 /* icon.sys */
#define NW_SAVE_TIME_KEY "saved"
#define NW_SAVE_COMMENT                                                                                              \
    "Crash Bandicoot: The Wrath of Cortex save (crashwoc-nuworld).\n"                                               \
    "Levels are keyed by their path, hubs by their number; keys this build does not know are kept."

typedef struct NwSaveFiles {
    char *main, *bak, *bak2, *tmp, *bad, *deleted;
} NwSaveFiles;

static void nw_save_files_free(NwSaveFiles *f)
{
    SDL_free(f->main);
    SDL_free(f->bak);
    SDL_free(f->bak2);
    SDL_free(f->tmp);
    SDL_free(f->bad);
    SDL_free(f->deleted);
    SDL_zerop(f);
}

/* base.toml in dir and its siblings. */
static bool nw_save_files_at(NwSaveFiles *f, const char *dir, const char *base)
{
    SDL_zerop(f);
    if (SDL_asprintf(&f->main, "%s%s.toml", dir, base) < 0 || SDL_asprintf(&f->bak, "%s.bak", f->main) < 0 ||
        SDL_asprintf(&f->bak2, "%s.bak2", f->main) < 0 || SDL_asprintf(&f->deleted, "%s.deleted", f->main) < 0 ||
        SDL_asprintf(&f->tmp, "%s.tmp", f->main) < 0 || SDL_asprintf(&f->bad, "%s.corrupt", f->main) < 0) {
        nw_save_files_free(f);
        return false;
    }
    return true;
}

static bool nw_save_files(NwSaveFiles *f, const char *dir, int n)
{
    char base[24];
    SDL_zerop(f);
    if (n < 0 || n >= NW_SAVE_TOTAL) {
        return SDL_SetError("no save %d", n + 1);
    }
    SDL_snprintf(base, sizeof(base), n < NW_SAVE_GAMES ? "save%03d" : "autosave%03d",
                 n < NW_SAVE_GAMES ? n + 1 : n - NW_SAVE_GAMES + 1);
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

/* The envelope is a TOML comment so tools can still read the document. Keeping the length and CRC at the start
   detects truncation even at a syntactically valid table boundary. Legacy, unenveloped saves remain readable. */
#define NW_SAVE_ENVELOPE "# NuWorld integrity 1 "
static bool nw_save_protected(const char *path)
{
    char prefix[sizeof(NW_SAVE_ENVELOPE) - 1];
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;
    size_t n = SDL_ReadIO(io, prefix, sizeof(prefix));
    SDL_CloseIO(io);
    return n == sizeof(prefix) && SDL_memcmp(prefix, NW_SAVE_ENVELOPE, sizeof(prefix)) == 0;
}

static bool nw_save_recoverable(const NwSaveFiles *f)
{
    return nw_save_protected(f->bak) || nw_save_protected(f->bak2);
}

static uint32_t nw_save_crc(const char *text, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint8_t)text[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static bool nw_save_integrity(const char **text, size_t *size)
{
    if (SDL_strncmp(*text, "# NuWorld integrity", 19) != 0) {
        return true;
    }
    unsigned long long length = 0;
    unsigned crc = 0;
    int end = 0;
    if (SDL_sscanf(*text, NW_SAVE_ENVELOPE "%llu %x%n", &length, &crc, &end) != 2 || end <= 0 ||
        (size_t)end >= *size || (*text)[end] != '\n') {
        return SDL_SetError("incomplete save integrity header");
    }
    *text += end + 1;
    *size -= (size_t)end + 1;
    return (length == *size && nw_save_crc(*text, *size) == crc) || SDL_SetError("save integrity check failed");
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
        const char *payload = text;
        if (nw_save_integrity(&payload, &size) && nw_doc_parse(doc, payload, size)) {
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
    if (SDL_GetPathInfo(f->deleted, NULL)) {
        return NW_SAVE_NONE;
    }
    char why[512];
    int got = nw_save_open(f->main, reg, doc);
    if (got == 1 || got == -2) {
        return got == 1 ? NW_SAVE_OK : got == 0 ? NW_SAVE_NONE : NW_SAVE_FAILED;
    }
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    /* Old deleted saves had only .bak; do not resurrect those when upgrading. New protected saves use a tombstone. */
    if (got == 0) {
        if (!nw_save_recoverable(f)) {
            return NW_SAVE_NONE;
        }
        SDL_strlcpy(why, "main save is missing", sizeof(why));
    }
    got = nw_save_open(f->bak, reg, doc);
    if (got != 1 && got != -2) {
        got = nw_save_open(f->bak2, reg, doc);
    }
    SDL_SetError("%s", why);
    return got == 1 ? NW_SAVE_BACKUP : NW_SAVE_FAILED;
}

/* doc as the file: written to .tmp, the current file copied to .bak when keep, then the rename over it. */
static bool nw_save_write(const NwSaveFiles *f, const NwDoc *doc, bool keep, const NwSaveRegistry *reg)
{
    size_t size = 0;
    char *text = nw_doc_write(doc, NW_SAVE_COMMENT, &size);
    char *protected = NULL;
    int length = text ? SDL_asprintf(&protected, NW_SAVE_ENVELOPE "%llu %08x\n%s", (unsigned long long)size,
                                     nw_save_crc(text, size), text) : -1;
    bool ok = length >= 0 && nw_saveio_write(f->tmp, protected, (size_t)length);
    SDL_free(protected);
    SDL_free(text);
    NwDoc checked;
    if (ok) {
        ok = nw_save_open(f->tmp, reg, &checked) == 1;
        if (ok) {
            nw_doc_free(&checked);
        }
    }
    if (ok && keep) {
        int backup = nw_save_open(f->bak, reg, &checked);
        if (backup == 1) {
            nw_doc_free(&checked);
            ok = nw_saveio_copy(f->bak, f->bak2);
        } else if (backup == -2) {
            ok = false;
        }
        ok = ok && nw_saveio_copy(f->main, f->bak);
    }
    ok = ok && nw_saveio_replace(f->tmp, f->main);
    if (ok && SDL_GetPathInfo(f->deleted, NULL)) {
        ok = SDL_RemovePath(f->deleted);
    }
    if (!ok) {
        char why[512];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        SDL_RemovePath(f->tmp);
        SDL_SetError("%s", why);
    }
    return ok;
}

/* Leave the main file in place until replacement: a crash must never leave a gap between valid generations. */
static bool nw_save_set_aside(const NwSaveFiles *f, int got, bool *keep)
{
    *keep = got == 1 || got == -2;
    if (got == -1 && !nw_saveio_copy(f->main, f->bad)) {
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

static void nw_save_meta_get(const NwDoc *doc, NwSaveMeta *meta)
{
    SDL_zerop(meta);
    const NwDocValue *id = nw_doc_get(doc->tables[0], "campaign");
    const NwDocValue *on = nw_doc_get(doc->tables[0], "autosave");
    const NwDocValue *revision = nw_doc_get(doc->tables[0], "revision");
    if (id && id->type == NW_DOC_STR && id->len == 32) {
        bool valid = true;
        for (size_t i = 0; i < id->len; i++) {
            valid = valid && ((id->bytes[i] >= '0' && id->bytes[i] <= '9') ||
                              (id->bytes[i] >= 'a' && id->bytes[i] <= 'f'));
        }
        if (valid) {
            SDL_memcpy(meta->campaign, id->bytes, id->len);
            meta->autosave = on && on->type == NW_DOC_BOOL && on->i;
            meta->revision = revision && revision->type == NW_DOC_INT && revision->i >= 0 ? revision->i : 0;
        }
    }
}

NwSaveResult nw_save_load_meta(const char *dir, int n, int generation, const NwSaveRegistry *reg, uint8_t *game,
                              NwSaveNames *names, NwSaveTime *saved, NwSaveMeta *meta)
{
    NwSaveFiles f;
    NwDoc doc;
    uint8_t tmp[NW_SAVE_GAME_SIZE];
    char why[512];
    if (!nw_save_files(&f, dir, n)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r;
    if (generation < 0 || generation > 2) {
        nw_save_files_free(&f);
        SDL_SetError("invalid save generation");
        return NW_SAVE_FAILED;
    }
    if (generation) {
        int got = nw_save_open(generation == 1 ? f.bak : f.bak2, reg, &doc);
        r = got == 1 ? NW_SAVE_OK : got == 0 ? NW_SAVE_NONE : NW_SAVE_FAILED;
    } else {
        r = nw_save_read(&f, reg, &doc);
    }
    if (r == NW_SAVE_OK || r == NW_SAVE_BACKUP) {
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        if (!nw_save_decode(&doc, reg, tmp, 1)) {
            r = NW_SAVE_FAILED;
        } else if (nw_save_empty(tmp)) { /* never written so: a stray file counts as no save */
            r = NW_SAVE_NONE;
        } else {
            SDL_memcpy(game, tmp, sizeof(tmp));
            if (names) {
                nw_save_names_decode(&doc, reg, 0, names);
            }
            if (saved) {
                nw_save_time_get(&doc, saved);
            }
            if (meta) {
                nw_save_meta_get(&doc, meta);
            }
            SDL_SetError("%s", why);
        }
        nw_doc_free(&doc);
    }
    nw_save_files_free(&f);
    return r;
}

NwSaveResult nw_save_load(const char *dir, int n, const NwSaveRegistry *reg, uint8_t *game, NwSaveNames *names,
                          NwSaveTime *saved)
{
    if (n < 0 || n >= NW_SAVE_GAMES) {
        SDL_SetError("no manual save %d", n + 1);
        return NW_SAVE_FAILED;
    }
    return nw_save_load_meta(dir, n, 0, reg, game, names, saved, NULL);
}

bool nw_save_store_meta(const char *dir, int n, const NwSaveRegistry *reg, const uint8_t *game,
                        const NwSaveNames *names, const NwSaveTime *saved, const NwSaveMeta *meta)
{
    static const NwSaveNames none;
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
        if (got == -1 || (got == 0 && !SDL_GetPathInfo(f.deleted, NULL) && nw_save_recoverable(&f))) {
            int back = nw_save_open(f.bak, reg, &doc);
            if (back != 1 && back != -2) {
                back = nw_save_open(f.bak2, reg, &doc);
            }
            if (back == -2) {
                nw_save_files_free(&f);
                return false;
            }
            got = back == 1 ? 1 : 0;
        }
        if (got == 1 || nw_doc_init(&doc)) {
            ok = nw_save_merge(&doc, reg, game, 1) && nw_save_names_merge(&doc, reg, 0, names ? names : &none) &&
                 nw_save_time_set(&doc, saved);
            if (ok && meta) {
                NwDocValue id, enabled = nw_doc_bool(meta->autosave), revision = nw_doc_int(meta->revision, 0);
                ok = nw_doc_str(&id, meta->campaign, SDL_strlen(meta->campaign)) &&
                     nw_doc_set(&doc, doc.tables[0], "campaign", &id) &&
                     nw_doc_set(&doc, doc.tables[0], "autosave", &enabled) &&
                     nw_doc_set(&doc, doc.tables[0], "revision", &revision);
            }
            ok = ok && nw_save_write(&f, &doc, keep, reg);
            nw_doc_free(&doc);
        }
    }
    nw_save_files_free(&f);
    return ok;
}

bool nw_save_store(const char *dir, int n, const NwSaveRegistry *reg, const uint8_t *game, const NwSaveNames *names,
                   const NwSaveTime *saved)
{
    if (n < 0 || n >= NW_SAVE_GAMES) return SDL_SetError("no manual save %d", n + 1);
    return nw_save_store_meta(dir, n, reg, game, names, saved, NULL);
}

NwSaveResult nw_save_remove(const char *dir, int n)
{
    NwSaveFiles f;
    if (!nw_save_files(&f, dir, n)) {
        return NW_SAVE_FAILED;
    }
    NwSaveResult r = NW_SAVE_NONE;
    if (SDL_GetPathInfo(f.main, NULL)) {
        /* A durable tombstone prevents automatic recovery from resurrecting an intentional deletion. */
        bool ok = nw_saveio_copy(f.main, f.bak) && nw_saveio_write(f.tmp, "deleted\n", 8) &&
                  nw_saveio_replace(f.tmp, f.deleted);
        r = ok && SDL_RemovePath(f.main) ? NW_SAVE_OK : NW_SAVE_FAILED;
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
    bool there = nw_save_files(&f, dir, n) && !SDL_GetPathInfo(f.deleted, NULL) &&
                 (SDL_GetPathInfo(f.main, NULL) || nw_save_recoverable(&f));
    nw_save_files_free(&f);
    return there;
}

bool nw_save_started(const char *dir)
{
    bool started = false;
    for (int n = 0; n < NW_SAVE_TOTAL && !started; n++) {
        NwSaveFiles f;
        started = nw_save_files(&f, dir, n) && (SDL_GetPathInfo(f.main, NULL) || SDL_GetPathInfo(f.bak, NULL));
        nw_save_files_free(&f);
    }
    return started;
}

static int nw_save_scan_range(const char *dir, int *last, int first, int end)
{
    int latest = -1;
    SDL_Time best = 0;
    if (last) {
        *last = -1;
    }
    for (int n = first; n < end; n++) {
        NwSaveFiles f;
        SDL_PathInfo info;
        if (nw_save_files(&f, dir, n) && !SDL_GetPathInfo(f.deleted, NULL) &&
            (SDL_GetPathInfo(f.main, &info) ||
             (nw_save_recoverable(&f) && (SDL_GetPathInfo(f.bak, &info) || SDL_GetPathInfo(f.bak2, &info))))) {
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

int nw_save_scan(const char *dir, int *last)
{
    return nw_save_scan_range(dir, last, 0, NW_SAVE_GAMES);
}

int nw_save_scan_auto(const char *dir, int *last)
{
    return nw_save_scan_range(dir, last, NW_SAVE_GAMES, NW_SAVE_TOTAL);
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
            ok = nw_save_store(dir, i, reg, game, NULL, when.year ? &when : NULL);
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
            made = nw_save_store(dir, free_saves[made], reg, game, NULL, NULL) ? made + 1 : -1;
        }
    }
    SDL_free(raw);
    if (made < 0) {
        nw_save_blame(file);
    }
    return made;
}

/* A card image no PS2 has formatted yet: its superblock page is all erased (PCSX2 makes new cards so). */
static bool nw_save_blank(const uint8_t *image, size_t size)
{
    if (size < 0x10000 || (size % 512 && size % 528)) {
        return false;
    }
    for (size_t i = 1; i < 512; i++) {
        if (image[i] != image[0]) {
            return false;
        }
    }
    return image[0] == 0xFF || image[0] == 0x00;
}

/* The card's save of the game into games (NW_SAVE_PS2_SIZE): 1 read, 0 none (the card formatted), -1 an SDL error;
   *blank when the card is not formatted. */
static int nw_save_card_read(const uint8_t *image, size_t size, const NwSaveRegistry *reg, uint8_t *games,
                             bool *blank)
{
    size_t len = 0;
    bool missing = false;
    *blank = !nw_ps2card_is_card(image, size) && nw_save_blank(image, size);
    if (*blank) {
        SDL_SetError("the memory card is not formatted: format it in PCSX2 first (the PS2 browser, or let the game "
                     "format it when it saves)");
        return -1;
    }
    if (!nw_ps2card_is_card(image, size)) {
        SDL_SetError("not a PS2 memory card image");
        return -1;
    }
    uint8_t *raw = nw_ps2card_file(image, size, NW_SAVE_PS2_NAME, NW_SAVE_PS2_FILE, &len, &missing);
    int r = missing ? 0 : -1;
    if (raw && len != NW_SAVE_PS2_SIZE) {
        SDL_SetError("/%s/%s is %u bytes, a save %u", NW_SAVE_PS2_NAME, NW_SAVE_PS2_FILE, (unsigned)len,
                     (unsigned)NW_SAVE_PS2_SIZE);
    } else if (raw && nw_save_check(reg, raw, NW_SAVE_PS2_GAMES)) {
        SDL_memcpy(games, raw, NW_SAVE_PS2_SIZE);
        r = 1;
    }
    SDL_free(raw);
    return r;
}

NwSaveCard nw_save_card(const char *card, const NwSaveRegistry *reg, uint8_t *games)
{
    size_t size = 0;
    bool blank = false;
    uint8_t *image = SDL_LoadFile(card, &size);
    int r = image ? nw_save_card_read(image, size, reg, games, &blank) : -1;
    SDL_free(image);
    if (r < 0 && !blank) {
        nw_save_blame(card);
    }
    return blank ? NW_SAVE_CARD_BLANK : r > 0 ? NW_SAVE_CARD_SAVE : r == 0 ? NW_SAVE_CARD_NONE : NW_SAVE_CARD_FAILED;
}

static void nw_save_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void nw_save_float(uint8_t *p, float f)
{
    uint32_t v;
    SDL_memcpy(&v, &f, sizeof(v));
    nw_save_le32(p, v);
}

/* The icon.sys the game writes: the PS2 browser's title, colours and light for the icon NW_SAVE_PS2_ICON. */
static void nw_save_icon_sys(uint8_t *out)
{
    static const int32_t background[16] = { 64, 64, 0, 0, 64, 64, 0, 0, 64, 0, 0, 0, 64, 0, 0, 0 };
    static const float light_dir[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    static const float ambient[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
    static const char title[] = "Crash BandicootWrath of Cortex"; /* two lines: the second from letter 15 */
    SDL_memset(out, 0, NW_SAVE_PS2_ICON_SYS);
    SDL_memcpy(out, "PS2D", 4);
    out[0x06] = 15 * 2; /* the line break, in bytes */
    out[0x0C] = 64;     /* the background's transparency */
    for (int i = 0; i < 16; i++) {
        nw_save_le32(out + 0x10 + 4 * i, (uint32_t)background[i]);
    }
    for (int i = 0; i < 12; i++) {
        nw_save_float(out + 0x50 + 4 * i, light_dir[i]); /* the light colours stay black */
    }
    for (int i = 0; i < 4; i++) {
        nw_save_float(out + 0xB0 + 4 * i, ambient[i]);
    }
    uint8_t *t = out + 0xC0; /* full-width Shift-JIS */
    for (const char *c = title; *c; c++, t += 2) {
        t[0] = *c == ' ' ? 0x81 : 0x82;
        t[1] = *c == ' ' ? 0x40 : *c >= 'a' ? (uint8_t)(0x81 + *c - 'a') : (uint8_t)(0x60 + *c - 'A');
    }
    for (int i = 0; i < 3; i++) { /* the icon to view, to copy and to delete */
        SDL_strlcpy((char *)out + 0x104 + 0x40 * i, NW_SAVE_PS2_ICON, 0x40);
    }
}

/* What the PS2 slots get, over games (the card's, had, or all zero). */
static bool nw_save_picked(const char *dir, const NwSaveRegistry *reg, const int *picks, bool had, uint8_t *games)
{
    for (int i = 0; i < NW_SAVE_PS2_GAMES; i++) {
        uint8_t *game = games + i * NW_SAVE_GAME_SIZE;
        if (picks[i] >= 0) {
            NwSaveResult r = nw_save_load(dir, picks[i], reg, game, NULL, NULL);
            if (r == NW_SAVE_NONE) {
                return SDL_SetError("save %d: no save", picks[i] + 1);
            }
            if (r == NW_SAVE_FAILED) {
                char why[512];
                SDL_strlcpy(why, SDL_GetError(), sizeof(why));
                return SDL_SetError("save %d: %s", picks[i] + 1, why);
            }
        } else if (picks[i] != NW_SAVE_PS2_KEEP && picks[i] != NW_SAVE_PS2_EMPTY) {
            return SDL_SetError("PS2 slot %d: no such choice %d", i + 1, picks[i]);
        } else if (picks[i] == NW_SAVE_PS2_EMPTY || !had) { /* as the game empties one: the flag alone */
            game[NW_SAVE_EMPTY] = 1;
        }
    }
    return nw_save_check(reg, games, NW_SAVE_PS2_GAMES);
}

/* image as card: via .tmp, the card before copied to .bak. */
static bool nw_save_card_write(const char *card, const uint8_t *image, size_t size)
{
    char *tmp = NULL, *bak = NULL;
    bool ok = SDL_asprintf(&tmp, "%s.tmp", card) >= 0 && SDL_asprintf(&bak, "%s.bak", card) >= 0;
    if (ok && !SDL_SaveFile(tmp, image, size)) {
        ok = false;
        nw_save_blame(tmp);
    } else if (ok && !SDL_CopyFile(card, bak)) {
        ok = false;
        nw_save_blame(bak);
    } else if (ok && !SDL_RenamePath(tmp, card)) {
        char why[512];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        ok = SDL_SetError("%s: %s (is PCSX2 using the card? close it first)", card, why);
    }
    if (!ok && tmp) {
        SDL_RemovePath(tmp);
    }
    SDL_free(tmp);
    SDL_free(bak);
    return ok;
}

bool nw_save_export(const char *dir, const NwSaveRegistry *reg, const char *card, const int *picks, const void *icon,
                    size_t icon_len)
{
    size_t size = 0, len = 0;
    bool blank = false, missing = false, keeps = false, ok = false;
    uint8_t icon_sys[NW_SAVE_PS2_ICON_SYS], marker[4] = { 0 };
    uint8_t *games = SDL_calloc(1, NW_SAVE_PS2_SIZE);
    uint8_t *image = games ? SDL_LoadFile(card, &size) : NULL;
    int had = image ? nw_save_card_read(image, size, reg, games, &blank) : -1;
    for (int i = 0; i < NW_SAVE_PS2_GAMES; i++) {
        keeps = keeps || picks[i] == NW_SAVE_PS2_KEEP;
    }
    if (had < 0 && image && !blank && !keeps && nw_ps2card_is_card(image, size)) {
        had = 0; /* a save there that does not read goes, when no slot keeps a game of it */
        SDL_memset(games, 0, NW_SAVE_PS2_SIZE);
    }
    if (had >= 0 && !icon) { /* only a card without the save's icon needs one */
        void *old = nw_ps2card_file(image, size, NW_SAVE_PS2_NAME, NW_SAVE_PS2_ICON, &len, &missing);
        if (!old) {
            had = -1;
            if (missing) {
                SDL_SetError("no icon for the card's save (the imported disc's STUFF/CRASH.ICO)");
            }
        }
        SDL_free(old);
    }
    if (had >= 0 && nw_save_picked(dir, reg, picks, had > 0, games)) {
        nw_save_icon_sys(icon_sys);
        const NwPs2CardFile files[] = {
            /* in the order the game makes them; only the games are written over */
            { NW_SAVE_PS2_ICON, icon, icon_len, true },
            { "icon.sys", icon_sys, sizeof(icon_sys), true },
            { NW_SAVE_PS2_NAME, marker, sizeof(marker), true },
            { NW_SAVE_PS2_FILE, games, NW_SAVE_PS2_SIZE, false },
        };
        SDL_Time now = 0;
        SDL_GetCurrentTime(&now);
        ok = nw_ps2card_put(image, size, NW_SAVE_PS2_NAME, files, (int)SDL_arraysize(files), now);
        uint8_t *back = ok ? nw_ps2card_file(image, size, NW_SAVE_PS2_NAME, NW_SAVE_PS2_FILE, &len, &missing) : NULL;
        if (ok && !(back && len == NW_SAVE_PS2_SIZE && SDL_memcmp(back, games, len) == 0)) { /* as the import reads */
            ok = back ? SDL_SetError("the card did not read back what was written") : false;
        }
        SDL_free(back);
        ok = ok && nw_save_card_write(card, image, size);
    }
    if (!ok && !blank) {
        nw_save_blame(card);
    }
    SDL_free(image);
    SDL_free(games);
    return ok;
}
