#include "save/savedoc.h"

#include <SDL3/SDL.h>

#define NW_DOC_DEPTH 16 /* header keys, and arrays inside arrays */

typedef struct NwDocBuf {
    uint8_t *data;
    size_t len, cap;
} NwDocBuf;

typedef struct NwDocParse {
    const char *p, *end;
    int line;
    NwDoc *doc;
    NwDocTable *table; /* the one the key = value lines go to */
} NwDocParse;

static bool nw_doc_put(NwDocBuf *b, const void *data, size_t n)
{
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n) {
            cap *= 2;
        }
        uint8_t *grown = SDL_realloc(b->data, cap);
        if (!grown) {
            return false;
        }
        b->data = grown;
        b->cap = cap;
    }
    SDL_memcpy(b->data + b->len, data, n);
    b->len += n;
    return true;
}

static bool nw_doc_puts(NwDocBuf *b, const char *s)
{
    return nw_doc_put(b, s, SDL_strlen(s));
}

NwDocValue nw_doc_int(int64_t i, int hex)
{
    NwDocValue v = { .type = NW_DOC_INT, .hex = hex, .i = i };
    return v;
}

NwDocValue nw_doc_bool(bool b)
{
    NwDocValue v = { .type = NW_DOC_BOOL, .i = b };
    return v;
}

bool nw_doc_str(NwDocValue *v, const void *bytes, size_t len)
{
    *v = (NwDocValue){ .type = NW_DOC_STR };
    if (!(v->bytes = SDL_malloc(len ? len : 1))) {
        return false;
    }
    if (len) {
        SDL_memcpy(v->bytes, bytes, len);
    }
    v->len = len;
    return true;
}

bool nw_doc_array(NwDocValue *v, size_t n)
{
    *v = (NwDocValue){ .type = NW_DOC_ARRAY };
    if (!(v->items = SDL_calloc(n ? n : 1, sizeof(*v->items)))) {
        return false;
    }
    v->len = n;
    return true;
}

void nw_doc_value_free(NwDocValue *v)
{
    if (v->type == NW_DOC_ARRAY) {
        for (size_t i = 0; i < v->len; i++) {
            nw_doc_value_free(&v->items[i]);
        }
    }
    SDL_free(v->items);
    SDL_free(v->bytes);
    *v = nw_doc_int(0, 0);
}

static void nw_doc_table_free(NwDocTable *t)
{
    for (size_t i = 0; i < t->depth; i++) {
        SDL_free(t->path[i]);
    }
    SDL_free(t->path);
    for (size_t i = 0; i < t->count; i++) {
        SDL_free(t->keys[i].name);
        nw_doc_value_free(&t->keys[i].value);
    }
    SDL_free(t->keys);
    SDL_free(t);
}

static NwDocTable *nw_doc_new_table(const char *const *path, size_t depth)
{
    NwDocTable *t = SDL_calloc(1, sizeof(*t));
    if (!t || (depth && !(t->path = SDL_calloc(depth, sizeof(*t->path))))) {
        SDL_free(t);
        return NULL;
    }
    for (; t->depth < depth; t->depth++) {
        if (!(t->path[t->depth] = SDL_strdup(path[t->depth]))) {
            nw_doc_table_free(t);
            return NULL;
        }
    }
    return t;
}

static bool nw_doc_insert(NwDoc *doc, size_t at, NwDocTable *t)
{
    if (doc->count == doc->cap) {
        size_t cap = doc->cap ? doc->cap * 2 : 64;
        NwDocTable **tables = SDL_realloc(doc->tables, cap * sizeof(*tables));
        if (!tables) {
            return false;
        }
        doc->tables = tables;
        doc->cap = cap;
    }
    SDL_memmove(&doc->tables[at + 1], &doc->tables[at], (doc->count - at) * sizeof(*doc->tables));
    doc->tables[at] = t;
    doc->count++;
    return true;
}

bool nw_doc_init(NwDoc *doc)
{
    SDL_zerop(doc);
    NwDocTable *root = nw_doc_new_table(NULL, 0);
    if (!root || !nw_doc_insert(doc, 0, root)) {
        SDL_free(root);
        nw_doc_free(doc);
        return false;
    }
    return true;
}

void nw_doc_free(NwDoc *doc)
{
    for (size_t i = 0; i < doc->count; i++) {
        nw_doc_table_free(doc->tables[i]);
    }
    SDL_free(doc->tables);
    SDL_zerop(doc);
}

/* How many leading header keys t shares with path. */
static size_t nw_doc_shared(const NwDocTable *t, const char *const *path, size_t depth)
{
    size_t n = 0;
    while (n < t->depth && n < depth && SDL_strcmp(t->path[n], path[n]) == 0) {
        n++;
    }
    return n;
}

NwDocTable *nw_doc_table(const NwDoc *doc, const char *const *path, size_t depth)
{
    for (size_t i = 0; i < doc->count; i++) {
        NwDocTable *t = doc->tables[i];
        if (t->depth == depth && nw_doc_shared(t, path, depth) == depth) {
            return t;
        }
    }
    return NULL;
}

static NwDocKey *nw_doc_key(const NwDocTable *t, const char *name)
{
    for (size_t i = 0; i < t->count; i++) {
        if (SDL_strcmp(t->keys[i].name, name) == 0) {
            return &t->keys[i];
        }
    }
    return NULL;
}

/* A key of a table above the header path that names one of its tables (a key and a table cannot share a name). */
static const char *nw_doc_header_blocked(const NwDoc *doc, const char *const *path, size_t depth)
{
    for (size_t i = 0; i < doc->count; i++) {
        const NwDocTable *t = doc->tables[i];
        if (t->depth < depth && nw_doc_shared(t, path, depth) == t->depth && nw_doc_key(t, path[t->depth])) {
            return path[t->depth];
        }
    }
    return NULL;
}

/* Whether a table below t is named name. */
static bool nw_doc_key_blocked(const NwDoc *doc, const NwDocTable *t, const char *name)
{
    for (size_t i = 0; i < doc->count; i++) {
        const NwDocTable *u = doc->tables[i];
        if (u->depth > t->depth && nw_doc_shared(u, (const char *const *)t->path, t->depth) == t->depth &&
            SDL_strcmp(u->path[t->depth], name) == 0) {
            return true;
        }
    }
    return false;
}

static NwDocTable *nw_doc_put_table(NwDoc *doc, const char *const *path, size_t depth, size_t at)
{
    const char *key = nw_doc_header_blocked(doc, path, depth);
    if (key) {
        SDL_SetError("key \"%s\" is also a table", key);
        return NULL;
    }
    NwDocTable *t = nw_doc_new_table(path, depth);
    if (!t || !nw_doc_insert(doc, at, t)) {
        if (t) {
            nw_doc_table_free(t);
        }
        return NULL;
    }
    return t;
}

NwDocTable *nw_doc_add_table(NwDoc *doc, const char *const *path, size_t depth)
{
    NwDocTable *t = nw_doc_table(doc, path, depth);
    if (t) {
        return t;
    }
    size_t at = doc->count, best = 1;
    for (size_t i = 1; i < doc->count; i++) {
        size_t n = nw_doc_shared(doc->tables[i], path, depth);
        if (n >= best) {
            best = n;
            at = i + 1;
        }
    }
    return nw_doc_put_table(doc, path, depth, at);
}

void nw_doc_drop_table(NwDoc *doc, NwDocTable *t)
{
    for (size_t i = 1; i < doc->count; i++) {
        if (doc->tables[i] == t) {
            nw_doc_table_free(t);
            SDL_memmove(&doc->tables[i], &doc->tables[i + 1], (doc->count - i - 1) * sizeof(*doc->tables));
            doc->count--;
            return;
        }
    }
}

const NwDocValue *nw_doc_get(const NwDocTable *t, const char *name)
{
    const NwDocKey *k = nw_doc_key(t, name);
    return k ? &k->value : NULL;
}

static bool nw_doc_grow_keys(NwDocTable *t)
{
    if (t->count == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 16;
        NwDocKey *keys = SDL_realloc(t->keys, cap * sizeof(*keys));
        if (!keys) {
            return false;
        }
        t->keys = keys;
        t->cap = cap;
    }
    return true;
}

bool nw_doc_set(NwDoc *doc, NwDocTable *t, const char *name, NwDocValue *v)
{
    NwDocKey *k = nw_doc_key(t, name);
    if (k) {
        nw_doc_value_free(&k->value);
        k->value = *v;
        return true;
    }
    char *dup = NULL;
    if (nw_doc_key_blocked(doc, t, name)) {
        SDL_SetError("key \"%s\" is also a table", name);
    } else if (nw_doc_grow_keys(t) && (dup = SDL_strdup(name))) {
        t->keys[t->count++] = (NwDocKey){ dup, *v };
        return true;
    }
    nw_doc_value_free(v);
    return false;
}

void nw_doc_unset(NwDocTable *t, const char *name)
{
    NwDocKey *k = nw_doc_key(t, name);
    if (k) {
        size_t i = (size_t)(k - t->keys);
        SDL_free(k->name);
        nw_doc_value_free(&k->value);
        SDL_memmove(&t->keys[i], &t->keys[i + 1], (t->count - i - 1) * sizeof(*t->keys));
        t->count--;
    }
}

/* Parsing. */

static bool nw_doc_fail(NwDocParse *ps, const char *what)
{
    SDL_SetError("line %d: %s", ps->line, what);
    return false;
}

static bool nw_doc_more(const NwDocParse *ps)
{
    return ps->p < ps->end;
}

static void nw_doc_ws(NwDocParse *ps)
{
    while (nw_doc_more(ps) && (*ps->p == ' ' || *ps->p == '\t')) {
        ps->p++;
    }
}

/* A line break at p, consumed; false when there is none. */
static bool nw_doc_newline(NwDocParse *ps)
{
    if (nw_doc_more(ps) && *ps->p == '\n') {
        ps->p++;
    } else if (ps->end - ps->p >= 2 && ps->p[0] == '\r' && ps->p[1] == '\n') {
        ps->p += 2;
    } else {
        return false;
    }
    ps->line++;
    return true;
}

static void nw_doc_comment(NwDocParse *ps)
{
    if (nw_doc_more(ps) && *ps->p == '#') {
        while (nw_doc_more(ps) && *ps->p != '\n' && *ps->p != '\r') {
            ps->p++;
        }
    }
}

/* Spaces, a comment and the line break or the end of the text; false when something else follows. */
static bool nw_doc_eol(NwDocParse *ps)
{
    nw_doc_ws(ps);
    nw_doc_comment(ps);
    return !nw_doc_more(ps) || nw_doc_newline(ps);
}

/* Spaces, comments and line breaks (inside an array). */
static void nw_doc_blank(NwDocParse *ps)
{
    do {
        nw_doc_ws(ps);
        nw_doc_comment(ps);
    } while (nw_doc_newline(ps));
}

/* One character of a string at p as its byte: ASCII, or the UTF-8 of U+0080..U+00FF. */
static bool nw_doc_char(NwDocParse *ps, NwDocBuf *b)
{
    unsigned char c = (unsigned char)*ps->p;
    if ((c < 0x20 && c != '\t') || c == 0x7F) {
        return nw_doc_fail(ps, "a control character in a string");
    }
    if (c >= 0x80) {
        unsigned char next = ps->end - ps->p >= 2 ? (unsigned char)ps->p[1] : 0;
        if ((c != 0xC2 && c != 0xC3) || (next & 0xC0) != 0x80) {
            return nw_doc_fail(ps, "a character above U+00FF or invalid UTF-8");
        }
        c = (unsigned char)(((c & 0x1F) << 6) | (next & 0x3F));
        ps->p++;
    }
    ps->p++;
    return nw_doc_put(b, &c, 1) || nw_doc_fail(ps, "out of memory");
}

static bool nw_doc_escape(NwDocParse *ps, NwDocBuf *b)
{
    uint32_t cp = 0;
    int digits = 0;
    ps->p++; /* the backslash */
    if (!nw_doc_more(ps)) {
        return nw_doc_fail(ps, "an unterminated string");
    }
    switch (*ps->p++) {
    case 'b': cp = '\b'; break;
    case 't': cp = '\t'; break;
    case 'n': cp = '\n'; break;
    case 'f': cp = '\f'; break;
    case 'r': cp = '\r'; break;
    case '"': cp = '"'; break;
    case '\\': cp = '\\'; break;
    case 'u': digits = 4; break;
    case 'U': digits = 8; break;
    default: return nw_doc_fail(ps, "an unknown escape");
    }
    for (int i = 0; i < digits; i++, ps->p++) {
        char c = nw_doc_more(ps) ? *ps->p : '\0';
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) {
            return nw_doc_fail(ps, "a bad \\u escape");
        }
        cp = cp << 4 | (uint32_t)d;
    }
    if (cp > 0xFF) {
        return nw_doc_fail(ps, "a character above U+00FF");
    }
    uint8_t byte = (uint8_t)cp;
    return nw_doc_put(b, &byte, 1) || nw_doc_fail(ps, "out of memory");
}

/* A basic ("...") or literal ('...') string at p into b. */
static bool nw_doc_string(NwDocParse *ps, NwDocBuf *b)
{
    char quote = *ps->p++;
    if (ps->end - ps->p >= 2 && ps->p[0] == quote && ps->p[1] == quote) {
        return nw_doc_fail(ps, "multi-line strings are not supported");
    }
    for (;;) {
        if (!nw_doc_more(ps) || *ps->p == '\n' || *ps->p == '\r') {
            return nw_doc_fail(ps, "an unterminated string");
        }
        if (*ps->p == quote) {
            ps->p++;
            return true;
        }
        if (!(quote == '"' && *ps->p == '\\' ? nw_doc_escape(ps, b) : nw_doc_char(ps, b))) {
            return false;
        }
    }
}

static bool nw_doc_bare(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/* A bare or quoted key at p, SDL_malloc'd; NULL with an SDL error. */
static char *nw_doc_parse_key(NwDocParse *ps)
{
    NwDocBuf b = { 0 };
    bool ok;
    if (nw_doc_more(ps) && (*ps->p == '"' || *ps->p == '\'')) {
        ok = nw_doc_string(ps, &b);
    } else {
        const char *start = ps->p;
        while (nw_doc_more(ps) && nw_doc_bare(*ps->p)) {
            ps->p++;
        }
        ok = ps->p > start ? nw_doc_put(&b, start, (size_t)(ps->p - start)) : nw_doc_fail(ps, "expected a key");
    }
    if (ok && b.len && SDL_strnlen((const char *)b.data, b.len) != b.len) {
        ok = nw_doc_fail(ps, "a NUL in a key");
    }
    if (!ok || !nw_doc_put(&b, "", 1)) {
        SDL_free(b.data);
        return NULL;
    }
    return (char *)b.data;
}

/* Whether the character at p is one of set (never a NUL). */
static bool nw_doc_at(const NwDocParse *ps, const char *set)
{
    return nw_doc_more(ps) && *ps->p && SDL_strchr(set, *ps->p);
}

/* After a scalar: what may follow it. */
static bool nw_doc_delimited(const NwDocParse *ps)
{
    return !nw_doc_more(ps) || nw_doc_at(ps, " \t,]#\r\n");
}

static bool nw_doc_digit(char c, bool hex)
{
    return (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
}

static bool nw_doc_integer(NwDocParse *ps, NwDocValue *v)
{
    bool neg = false, hex = false;
    if (*ps->p == '+' || *ps->p == '-') {
        neg = *ps->p++ == '-';
    } else if (ps->end - ps->p >= 2 && ps->p[0] == '0' && ps->p[1] == 'x') {
        hex = true;
        ps->p += 2;
    }
    const char *start = ps->p;
    uint64_t n = 0, limit = neg ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    int digits = 0;
    while (nw_doc_more(ps) && (nw_doc_digit(*ps->p, hex) || *ps->p == '_')) {
        char c = *ps->p++;
        if (c == '_') {
            if (!digits || !nw_doc_more(ps) || !nw_doc_digit(*ps->p, hex)) {
                return nw_doc_fail(ps, "a misplaced _ in an integer");
            }
            continue;
        }
        unsigned d = c <= '9' ? (unsigned)(c - '0') : (unsigned)((c | 0x20) - 'a' + 10);
        unsigned base = hex ? 16 : 10;
        if (n > (limit - d) / base) {
            return nw_doc_fail(ps, "an integer out of range");
        }
        n = n * base + d;
        digits++;
    }
    if (!digits || (!hex && digits > 1 && *start == '0')) {
        return nw_doc_fail(ps, "a malformed integer");
    }
    if (nw_doc_at(ps, ".eE:-T")) {
        return nw_doc_fail(ps, "floats and dates are not supported");
    }
    if (!nw_doc_delimited(ps)) {
        return nw_doc_fail(ps, "an unsupported value");
    }
    *v = nw_doc_int(neg ? (int64_t)(0 - n) : (int64_t)n, hex ? SDL_min(digits, 16) : 0);
    return true;
}

static bool nw_doc_value(NwDocParse *ps, NwDocValue *v, int nesting);

static bool nw_doc_array_value(NwDocParse *ps, NwDocValue *v, int nesting)
{
    NwDocValue items = { .type = NW_DOC_ARRAY };
    size_t cap = 0;
    if (nesting >= NW_DOC_DEPTH) {
        return nw_doc_fail(ps, "arrays nested too deep");
    }
    ps->p++; /* [ */
    for (;;) {
        nw_doc_blank(ps);
        if (nw_doc_more(ps) && *ps->p == ']') {
            break;
        }
        if (items.len == cap) {
            cap = cap ? cap * 2 : 4;
            NwDocValue *grown = SDL_realloc(items.items, cap * sizeof(*grown));
            if (!grown) {
                nw_doc_value_free(&items);
                return nw_doc_fail(ps, "out of memory");
            }
            items.items = grown;
        }
        if (!nw_doc_value(ps, &items.items[items.len], nesting + 1)) {
            nw_doc_value_free(&items);
            return false;
        }
        items.len++;
        nw_doc_blank(ps);
        if (nw_doc_more(ps) && *ps->p == ',') {
            ps->p++;
        } else if (!nw_doc_more(ps) || *ps->p != ']') {
            nw_doc_value_free(&items);
            return nw_doc_fail(ps, "expected , or ] in an array");
        }
    }
    ps->p++; /* ] */
    *v = items;
    return true;
}

static bool nw_doc_word(NwDocParse *ps, const char *word)
{
    size_t n = SDL_strlen(word);
    if ((size_t)(ps->end - ps->p) < n || SDL_memcmp(ps->p, word, n) != 0) {
        return false;
    }
    ps->p += n;
    if (!nw_doc_delimited(ps)) {
        ps->p -= n;
        return false;
    }
    return true;
}

static bool nw_doc_value(NwDocParse *ps, NwDocValue *v, int nesting)
{
    char c = nw_doc_more(ps) ? *ps->p : '\0';
    if (c == '"' || c == '\'') {
        NwDocBuf b = { 0 };
        if (!nw_doc_string(ps, &b) || (!b.data && !(b.data = SDL_malloc(1)))) { /* bytes never NULL, as nw_doc_str */
            SDL_free(b.data);
            return false;
        }
        *v = (NwDocValue){ .type = NW_DOC_STR, .bytes = b.data, .len = b.len };
        return true;
    }
    if (c == '[') {
        return nw_doc_array_value(ps, v, nesting);
    }
    if (c == '{') {
        return nw_doc_fail(ps, "inline tables are not supported");
    }
    if (nw_doc_word(ps, "true")) {
        *v = nw_doc_bool(true);
        return true;
    }
    if (nw_doc_word(ps, "false")) {
        *v = nw_doc_bool(false);
        return true;
    }
    if (c == '+' || c == '-' || (c >= '0' && c <= '9')) {
        return nw_doc_integer(ps, v);
    }
    return nw_doc_fail(ps, c ? "an unsupported value" : "expected a value");
}

static bool nw_doc_header(NwDocParse *ps)
{
    char *path[NW_DOC_DEPTH];
    size_t depth = 0;
    bool ok = false;
    ps->p++; /* [ */
    if (nw_doc_more(ps) && *ps->p == '[') {
        return nw_doc_fail(ps, "arrays of tables are not supported");
    }
    for (;;) {
        nw_doc_ws(ps);
        if (depth == NW_DOC_DEPTH) {
            nw_doc_fail(ps, "a header nested too deep");
            goto out;
        }
        if (!(path[depth] = nw_doc_parse_key(ps))) {
            goto out;
        }
        depth++;
        nw_doc_ws(ps);
        if (nw_doc_more(ps) && *ps->p == '.') {
            ps->p++;
        } else if (nw_doc_more(ps) && *ps->p == ']') {
            ps->p++;
            break;
        } else {
            nw_doc_fail(ps, "expected . or ] in a header");
            goto out;
        }
    }
    if (nw_doc_table(ps->doc, (const char *const *)path, depth)) {
        nw_doc_fail(ps, "a table defined twice");
    } else if (!(ps->table = nw_doc_put_table(ps->doc, (const char *const *)path, depth, ps->doc->count))) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        nw_doc_fail(ps, why);
    } else if (!nw_doc_eol(ps)) {
        nw_doc_fail(ps, "text after a header");
    } else {
        ok = true;
    }
out:
    for (size_t i = 0; i < depth; i++) {
        SDL_free(path[i]);
    }
    return ok;
}

static bool nw_doc_assign(NwDocParse *ps)
{
    NwDocValue v;
    char *name = nw_doc_parse_key(ps);
    if (!name) {
        return false;
    }
    nw_doc_ws(ps);
    bool ok = false;
    if (nw_doc_more(ps) && *ps->p == '.') {
        nw_doc_fail(ps, "dotted keys are not supported");
    } else if (!nw_doc_more(ps) || *ps->p != '=') {
        nw_doc_fail(ps, "expected = after a key");
    } else {
        ps->p++;
        nw_doc_ws(ps);
        if (nw_doc_value(ps, &v, 0)) {
            if (nw_doc_get(ps->table, name)) {
                nw_doc_fail(ps, "a key defined twice");
                nw_doc_value_free(&v);
            } else if (!nw_doc_set(ps->doc, ps->table, name, &v)) {
                char why[256];
                SDL_strlcpy(why, SDL_GetError(), sizeof(why));
                nw_doc_fail(ps, why);
            } else {
                ok = nw_doc_eol(ps) || nw_doc_fail(ps, "text after a value");
            }
        }
    }
    SDL_free(name);
    return ok;
}

bool nw_doc_parse(NwDoc *doc, const char *text, size_t size)
{
    NwDocParse ps = { text, text + size, 1, doc, doc->tables[0] };
    if (size >= 3 && SDL_memcmp(text, "\xEF\xBB\xBF", 3) == 0) {
        ps.p += 3;
    }
    while (nw_doc_more(&ps)) {
        if (nw_doc_eol(&ps)) {
            continue;
        }
        if (!(*ps.p == '[' ? nw_doc_header(&ps) : nw_doc_assign(&ps))) {
            return false;
        }
    }
    return true;
}

/* Writing. */

static bool nw_doc_write_str(NwDocBuf *b, const uint8_t *s, size_t len)
{
    bool ok = nw_doc_puts(b, "\"");
    for (size_t i = 0; ok && i < len; i++) {
        char esc[8];
        if (s[i] == '"' || s[i] == '\\') {
            esc[0] = '\\';
            esc[1] = (char)s[i];
            esc[2] = '\0';
        } else if (s[i] >= 0x20 && s[i] < 0x7F) {
            esc[0] = (char)s[i];
            esc[1] = '\0';
        } else {
            SDL_snprintf(esc, sizeof(esc), "\\u%04X", s[i]);
        }
        ok = nw_doc_puts(b, esc);
    }
    return ok && nw_doc_puts(b, "\"");
}

static bool nw_doc_write_key(NwDocBuf *b, const char *name)
{
    bool bare = *name != '\0';
    for (const char *c = name; *c; c++) {
        bare = bare && nw_doc_bare(*c);
    }
    return bare ? nw_doc_puts(b, name) : nw_doc_write_str(b, (const uint8_t *)name, SDL_strlen(name));
}

static bool nw_doc_write_value(NwDocBuf *b, const NwDocValue *v)
{
    char num[32];
    switch (v->type) {
    case NW_DOC_INT:
        if (v->hex && v->i >= 0) {
            SDL_snprintf(num, sizeof(num), "0x%0*" SDL_PRIx64, v->hex, (uint64_t)v->i);
        } else {
            SDL_snprintf(num, sizeof(num), "%" SDL_PRIs64, v->i);
        }
        return nw_doc_puts(b, num);
    case NW_DOC_BOOL:
        return nw_doc_puts(b, v->i ? "true" : "false");
    case NW_DOC_STR:
        return nw_doc_write_str(b, v->bytes, v->len);
    case NW_DOC_ARRAY:
        if (!nw_doc_puts(b, "[")) {
            return false;
        }
        for (size_t i = 0; i < v->len; i++) {
            if ((i && !nw_doc_puts(b, ", ")) || !nw_doc_write_value(b, &v->items[i])) {
                return false;
            }
        }
        return nw_doc_puts(b, "]");
    }
    return false;
}

char *nw_doc_write(const NwDoc *doc, const char *comment, size_t *size)
{
    NwDocBuf b = { 0 };
    bool ok = true;
    for (const char *line = comment; ok && line && *line;) {
        const char *next = SDL_strchr(line, '\n');
        size_t n = next ? (size_t)(next - line) : SDL_strlen(line);
        ok = nw_doc_puts(&b, n ? "# " : "#") && nw_doc_put(&b, line, n) && nw_doc_puts(&b, "\n");
        line = next ? next + 1 : NULL;
    }
    for (size_t i = 0; ok && i < doc->count; i++) {
        const NwDocTable *t = doc->tables[i];
        if (i > 0 || comment) {
            ok = b.len == 0 || nw_doc_puts(&b, "\n");
        }
        if (ok && i > 0) {
            ok = nw_doc_puts(&b, "[");
            for (size_t k = 0; ok && k < t->depth; k++) {
                ok = (k == 0 || nw_doc_puts(&b, ".")) && nw_doc_write_key(&b, t->path[k]);
            }
            ok = ok && nw_doc_puts(&b, "]\n");
        }
        for (size_t k = 0; ok && k < t->count; k++) {
            ok = nw_doc_write_key(&b, t->keys[k].name) && nw_doc_puts(&b, " = ") &&
                 nw_doc_write_value(&b, &t->keys[k].value) && nw_doc_puts(&b, "\n");
        }
    }
    if (!ok || !nw_doc_put(&b, "", 1)) {
        SDL_free(b.data);
        SDL_OutOfMemory();
        return NULL;
    }
    if (size) {
        *size = b.len - 1;
    }
    return (char *)b.data;
}
