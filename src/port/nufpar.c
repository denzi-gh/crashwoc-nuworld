/* Host overrides for NuFPar; legacy record layout remains unchanged. */
#include "port/nufpar.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#ifndef NW_FPAR_TEXT_BUDGET
#define NW_FPAR_TEXT_BUDGET (16u << 20)
#endif

extern int NuFileOpenSize(int handle);
extern void *NuMemAllocFn(int size, char *file, int line);

typedef struct NwFParState {
    char *line, *word;
    const char *error;
    int line_limit, word_limit, old_line_pos;
} NwFParState;

static NwFParState *state(struct nufpar_s *parser)
{
    return (NwFParState *)(parser + 1);
}

static int fail(struct nufpar_s *parser, const char *error)
{
    NwFParState *s = state(parser);
    s->error = error;
    s->line[0] = s->word[0] = 0;
    parser->lbuff[0] = parser->wbuff[0] = 0;
    return 0;
}

/* Long tokens are available through accessors, never silently truncated into the legacy view. */
static int finish(char *text, char *legacy, size_t capacity, int length)
{
    text[length] = 0;
    if ((size_t)length < capacity) memcpy(legacy, text, (size_t)length + 1);
    else legacy[0] = 0;
    return length;
}

struct nufpar_s *NuFParOpenSized(int handle, int size)
{
    size_t capacity = size >= 0 ? (size_t)size + 1 : 0;
    size_t header = sizeof(struct nufpar_s) + sizeof(NwFParState);
    if (!capacity || (size_t)size > NW_FPAR_TEXT_BUDGET ||
        capacity > ((size_t)INT_MAX - header) / 2) return NULL;
    struct nufpar_s *parser = NuMemAllocFn((int)(header + 2 * capacity), __FILE__, __LINE__);
    if (!parser) return NULL;
    memset(parser, 0, sizeof(*parser));
    NwFParState *s = state(parser);
    *s = (NwFParState){.line_limit = 256, .word_limit = 255};
    s->line = (char *)(s + 1);
    s->word = s->line + capacity;
    s->line[0] = s->word[0] = 0;
    parser->fh = handle;
    parser->buffend = parser->line_num = parser->compos = -1;
    parser->size = size;
    return parser;
}

struct nufpar_s *NuFParOpen(int handle)
{
    return NuFParOpenSized(handle, NuFileOpenSize(handle));
}

int NuFParSetLimits(struct nufpar_s *parser, int line, int word)
{
    NwFParState *s = state(parser);
    if (s->error) return 0;
    if (line < 1 || word < 1) return fail(parser, "invalid parser limits");
    s->line_limit = line < parser->size ? line : parser->size;
    s->word_limit = word < parser->size ? word : parser->size;
    return 1;
}

const char *NuFParGetError(const struct nufpar_s *parser)
{
    return ((const NwFParState *)(parser + 1))->error;
}

const char *NuFParLine(const struct nufpar_s *parser)
{
    return ((const NwFParState *)(parser + 1))->line;
}

const char *NuFParWord(const struct nufpar_s *parser)
{
    return ((const NwFParState *)(parser + 1))->word;
}

int NuFParGetLine(struct nufpar_s *parser)
{
    NwFParState *s = state(parser);
    if (s->error) return 0;
    int length;
    int done;
    char c;

    length = 0;
    parser->line_pos = 0;
    parser->line_num++;
    for (;;) {
        c = NuGetChar(parser);
        switch (c) {
        case '\r':
            NuGetChar(parser);
        case '\n':
            if (length == 0) {
                parser->line_num++;
                break;
            }
        case 0:
            return finish(s->line, parser->lbuff, sizeof(parser->lbuff), length);
        case ';':
            if (length == 0) {
                done = 0;
                do {
                    c = NuGetChar(parser);
                    switch (c) {
                    case '\r':
                        NuGetChar(parser);
                    case '\n':
                    case 0:
                        done = 1;
                        break;
                    }
                } while (!done);
                length = 0;
                parser->line_pos = 0;
                parser->line_num++;
                break;
            }
        default:
            if (length >= s->line_limit || length >= parser->size)
                return fail(parser, "line limit exceeded");
            s->line[length] = c;
            length++;
            break;
        }
    }
}

int NuFParGetWord(struct nufpar_s *parser)
{
    NwFParState *s = state(parser);
    if (s->error) return 0;
    if (parser->line_pos < 0 || parser->line_pos > parser->size)
        return fail(parser, "invalid parser position");
    int length;
    int quoted;
    char c;

    length = 0;
    quoted = 0;
    s->old_line_pos = parser->line_pos;
    while (s->line[parser->line_pos] != 0) {
        c = s->line[parser->line_pos];
        switch (c) {
        case ' ':
        case '\t':
        case ',':
            if (!quoted) {
                if (length != 0) {
                    return finish(s->word, parser->wbuff, sizeof(parser->wbuff), length);
                }
                break;
            }
        default:
            if (c == '"') {
                quoted = 1 - quoted;
            } else {
                if (length >= s->word_limit || length >= parser->size)
                    return fail(parser, "word limit exceeded");
                s->word[length] = c;
                length++;
            }
            break;
        }
        parser->line_pos++;
    }
    return finish(s->word, parser->wbuff, sizeof(parser->wbuff), length);
}

void NuFParUnGetWord(struct nufpar_s *parser)
{
    parser->line_pos = state(parser)->old_line_pos;
}
