/* The line reader of the .TXT files (docs/csp-spec.md, paragraph 5): one entry a line, a word and its values; ';'
   starts a comment; a value with spaces in double quotes; a block opens with '{' at the end of its line and closes
   with '}' on a line of its own. One dispatcher for every vocabulary: unknown words, words given twice, values left
   over and unclosed blocks are refused here, with the file and line, and nowhere else. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "port/gcutpc/internal.h"

bool NuGCutPCTxtFail(NUGCUTPCTXT_s *txt, const char *format, ...)
{
    char reason[NUGCUTPC_ERROR_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(reason, sizeof(reason), format, args);
    va_end(args);
    return NuGCutPCFail(txt->error, "%s:%d: %s", txt->where, txt->line, reason);
}

/* Lines and words. */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* The next line's text into the buffer; false at the end of the text. */
static bool take_line(NUGCUTPCTXT_s *txt, size_t *length)
{
    if (txt->pos >= txt->size) {
        return false;
    }
    size_t start = txt->pos;
    while (txt->pos < txt->size && txt->text[txt->pos] != '\n') {
        txt->pos++;
    }
    *length = txt->pos - start;
    txt->pos++; /* past the '\n' */
    txt->line++;
    if (*length >= sizeof(txt->buffer)) {
        *length = sizeof(txt->buffer); /* refused by the caller */
        return true;
    }
    memcpy(txt->buffer, txt->text + start, *length);
    txt->buffer[*length] = '\0';
    return true;
}

static bool add_word(NUGCUTPCTXT_s *txt, char *word)
{
    if (txt->count == NUGCUTPC_TXT_WORDS) {
        return NuGCutPCTxtFail(txt, "more than %d values on one line", NUGCUTPC_TXT_WORDS);
    }
    txt->words[txt->count++] = word;
    return true;
}

/* The buffer's words, split in place: quoted ones without their quotes, a comment dropped. */
static bool split_words(NUGCUTPCTXT_s *txt)
{
    char *p = txt->buffer;
    txt->count = txt->next = 0;
    for (;;) {
        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0' || *p == ';') {
            return true;
        }
        if (*p == '"') {
            char *end = strchr(p + 1, '"');
            if (!end) {
                return NuGCutPCTxtFail(txt, "a value opened by '\"' is not closed");
            }
            *end = '\0';
            if (!add_word(txt, p + 1)) {
                return false;
            }
            p = end + 1;
            continue;
        }
        char *word = p;
        while (*p && !is_space(*p) && *p != ';') {
            p++;
        }
        bool last = *p == '\0' || *p == ';';
        *p = '\0';
        if (!add_word(txt, word)) {
            return false;
        }
        if (last) {
            return true;
        }
        p++;
    }
}

/* The next line with words; *done at the end of the text. */
static bool next_line(NUGCUTPCTXT_s *txt, bool *done)
{
    size_t length;
    *done = false;
    while (take_line(txt, &length)) {
        if (length >= sizeof(txt->buffer)) {
            return NuGCutPCTxtFail(txt, "a line longer than %d characters", NUGCUTPC_TXT_LINE - 1);
        }
        if (!split_words(txt)) {
            return false;
        }
        if (txt->count > 0) {
            return true;
        }
    }
    *done = true;
    return true;
}

/* The dispatcher. */

static const NUGCUTPCWORD_s *find_word(const NUGCUTPCVOCABULARY_s *vocabulary, const char *name, size_t *index)
{
    for (size_t i = 0; i < vocabulary->count; i++) {
        if (strcmp(vocabulary->words[i].name, name) == 0) {
            *index = i;
            return &vocabulary->words[i];
        }
    }
    return NULL;
}

static bool read_block(NUGCUTPCTXT_s *txt, const NUGCUTPCVOCABULARY_s *vocabulary, void *target, bool closing);

static bool read_entry(NUGCUTPCTXT_s *txt, const NUGCUTPCWORD_s *word, void *target)
{
    txt->next = 1;
    if (!word->block) {
        if (!word->read(txt, target)) {
            return false;
        }
        return !NuGCutPCTxtMore(txt) || NuGCutPCTxtFail(txt, "%s: unexpected '%s'", word->name,
                                                        txt->words[txt->next]);
    }
    if (txt->count != 2 || strcmp(txt->words[1], "{") != 0) {
        return NuGCutPCTxtFail(txt, "%s opens a block: '%s {'", word->name, word->name);
    }
    void *element = word->open(txt, target);
    return element && read_block(txt, word->block, element, true);
}

static bool read_block(NUGCUTPCTXT_s *txt, const NUGCUTPCVOCABULARY_s *vocabulary, void *target, bool closing)
{
    bool seen[NUGCUTPC_TXT_VOCABULARY] = { false };
    uint32_t capacity[NUGCUTPC_TXT_VOCABULARY] = { 0 };
    if (vocabulary->count > NUGCUTPC_TXT_VOCABULARY) {
        return NuGCutPCTxtFail(txt, "a vocabulary of more than %d words", NUGCUTPC_TXT_VOCABULARY);
    }
    for (;;) {
        bool done;
        if (!next_line(txt, &done)) {
            return false;
        }
        if (done) {
            return !closing || NuGCutPCTxtFail(txt, "a block not closed by '}'");
        }
        if (strcmp(txt->words[0], "}") == 0 && txt->count == 1) {
            return closing || NuGCutPCTxtFail(txt, "'}' without a block");
        }
        size_t index;
        const NUGCUTPCWORD_s *word = find_word(vocabulary, txt->words[0], &index);
        if (!word) {
            return NuGCutPCTxtFail(txt, "unknown word '%s'", txt->words[0]);
        }
        if (seen[index] && !word->repeat) {
            return NuGCutPCTxtFail(txt, "%s twice", word->name);
        }
        seen[index] = true;
        txt->capacity = &capacity[index];
        if (!read_entry(txt, word, target)) {
            return false;
        }
    }
}

bool NuGCutPCTxtRead(NUGCUTPCTXT_s *txt, const NUGCUTPCVOCABULARY_s *vocabulary, void *target)
{
    return read_block(txt, vocabulary, target, false);
}

/* Values. */

bool NuGCutPCTxtMore(const NUGCUTPCTXT_s *txt)
{
    return txt->next < txt->count;
}

static bool next_word(NUGCUTPCTXT_s *txt, const char **word)
{
    if (!NuGCutPCTxtMore(txt)) {
        return NuGCutPCTxtFail(txt, "%s needs more values", txt->words[0]);
    }
    *word = txt->words[txt->next++];
    return true;
}

bool NuGCutPCTxtFloat(NUGCUTPCTXT_s *txt, float *out)
{
    const char *word;
    char *end;
    if (!next_word(txt, &word)) {
        return false;
    }
    *out = strtof(word, &end);
    return (*word && *end == '\0') || NuGCutPCTxtFail(txt, "%s: '%s' is not a number", txt->words[0], word);
}

bool NuGCutPCTxtInt(NUGCUTPCTXT_s *txt, int32_t *out)
{
    const char *word;
    char *end;
    if (!next_word(txt, &word)) {
        return false;
    }
    errno = 0;
    long value = strtol(word, &end, 10);
    if (!*word || *end != '\0' || errno || value < INT32_MIN || value > INT32_MAX) {
        return NuGCutPCTxtFail(txt, "%s: '%s' is not a whole number", txt->words[0], word);
    }
    *out = (int32_t)value;
    return true;
}

bool NuGCutPCTxtByte(NUGCUTPCTXT_s *txt, int16_t *out)
{
    int32_t value;
    if (!NuGCutPCTxtInt(txt, &value)) {
        return false;
    }
    if (value < 0 || value > 255) {
        return NuGCutPCTxtFail(txt, "%s: %d is not 0-255", txt->words[0], value);
    }
    *out = (int16_t)value;
    return true;
}

bool NuGCutPCTxtString(NUGCUTPCTXT_s *txt, const char **out)
{
    const char *word;
    if (!next_word(txt, &word)) {
        return false;
    }
    size_t n = strlen(word) + 1;
    char *copy = NuGCutPCArenaAlloc(txt->arena, n, 1);
    if (!copy) {
        return NuGCutPCTxtFail(txt, "out of memory");
    }
    memcpy(copy, word, n);
    *out = copy;
    return true;
}

bool NuGCutPCTxtChoice(NUGCUTPCTXT_s *txt, const char *const *names, int count, int32_t *out)
{
    const char *word;
    if (!next_word(txt, &word)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(word, names[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return NuGCutPCTxtFail(txt, "%s: '%s' is not one of its choices", txt->words[0], word);
}

bool NuGCutPCTxtOption(NUGCUTPCTXT_s *txt, const char *name)
{
    if (NuGCutPCTxtMore(txt) && strcmp(txt->words[txt->next], name) == 0) {
        txt->next++;
        return true;
    }
    return false;
}

void *NuGCutPCTxtPush(NUGCUTPCTXT_s *txt, void **items, uint32_t *count, size_t size)
{
    if (*count == *txt->capacity) {
        uint32_t capacity = *txt->capacity ? *txt->capacity * 2 : 4;
        void *grown = NuGCutPCArenaAlloc(txt->arena, capacity, size);
        if (!grown) {
            NuGCutPCTxtFail(txt, "out of memory");
            return NULL;
        }
        if (*count) {
            memcpy(grown, *items, (size_t)*count * size);
        }
        *items = grown;
        *txt->capacity = capacity;
    }
    unsigned char *item = (unsigned char *)*items + (size_t)(*count)++ * size;
    memset(item, 0, size);
    return item;
}
