#include "vfs/disc.h"

#include <string.h>

#include <SDL3/SDL.h>

#define NW_DISC_RAW 2352
#define NW_DISC_BATCH 64        /* raw sectors per read */
#define NW_DISC_CUE_MAX 0x10000 /* larger files are not cue sheets */
#define NW_DISC_PVD 16          /* LSN of the primary volume descriptor */
#define NW_DISC_SUBMODE 18      /* Mode 2 subheader submode byte */
#define NW_DISC_FORM2 0x20

static const uint8_t nw_disc_sync[12] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

static bool nw_cue_space(char c)
{
    return c == ' ' || c == '\t';
}

static const char *nw_cue_skip(const char *p, const char *end)
{
    while (p < end && nw_cue_space(*p)) {
        p++;
    }
    return p;
}

static bool nw_cue_keyword(const char *p, const char *end, const char *kw)
{
    size_t n = SDL_strlen(kw);
    return (size_t)(end - p) >= n && SDL_strncasecmp(p, kw, n) == 0 && (p + n == end || nw_cue_space(p[n]));
}

/* Next token of [p, end): a quoted string or a run of non-blanks. */
static const char *nw_cue_token(const char *p, const char *end, char *out, size_t size)
{
    const char *q;
    p = nw_cue_skip(p, end);
    if (p < end && *p == '"') {
        p++;
        q = memchr(p, '"', (size_t)(end - p));
        if (!q) {
            return NULL;
        }
    } else {
        for (q = p; q < end && !nw_cue_space(*q); q++) {
        }
    }
    if (q == p || (size_t)(q - p) >= size) {
        return NULL;
    }
    SDL_memcpy(out, p, (size_t)(q - p));
    out[q - p] = '\0';
    return q < end && *q == '"' ? q + 1 : q;
}

/* First FILE and its TRACK 01. */
static bool nw_cue_parse(const char *text, char *file, size_t file_size, NwDiscFormat *format)
{
    static const struct {
        const char *name;
        NwDiscFormat format;
    } modes[] = {
        { "MODE2/2352", NW_DISC_RAW_MODE2 },
        { "MODE1/2352", NW_DISC_RAW_MODE1 },
        { "MODE1/2048", NW_DISC_COOKED },
    };
    bool have_file = false;
    const char *line = text;

    if (SDL_strncmp(line, "\xef\xbb\xbf", 3) == 0) {
        line += 3;
    }
    while (*line) {
        const char *end = line + strcspn(line, "\r\n");
        const char *p = nw_cue_skip(line, end);

        if (nw_cue_keyword(p, end, "FILE")) {
            if (have_file) {
                break;
            }
            if (!nw_cue_token(p + 4, end, file, file_size)) {
                return SDL_SetError("cue sheet: bad FILE line");
            }
            have_file = true;
        } else if (nw_cue_keyword(p, end, "TRACK")) {
            char number[8], mode[32];
            if (!have_file) {
                return SDL_SetError("cue sheet: TRACK before FILE");
            }
            p = nw_cue_token(p + 5, end, number, sizeof(number));
            if (!p || !nw_cue_token(p, end, mode, sizeof(mode))) {
                return SDL_SetError("cue sheet: bad TRACK line");
            }
            if (SDL_atoi(number) != 1) {
                return SDL_SetError("cue sheet: the first track is %s, expected 01", number);
            }
            for (size_t i = 0; i < SDL_arraysize(modes); i++) {
                if (SDL_strcasecmp(mode, modes[i].name) == 0) {
                    *format = modes[i].format;
                    return true;
                }
            }
            return SDL_SetError("cue sheet: track mode %s is not supported (MODE2/2352, MODE1/2352 or MODE1/2048)",
                                mode);
        }
        line = end + strspn(end, "\r\n");
    }
    return SDL_SetError(have_file ? "cue sheet has no TRACK 01" : "cue sheet has no FILE line");
}

static bool nw_disc_has_ext(const char *path, const char *ext)
{
    size_t n = SDL_strlen(path), e = SDL_strlen(ext);
    return n >= e && SDL_strcasecmp(path + n - e, ext) == 0;
}

static size_t nw_disc_dir_len(const char *path)
{
    size_t dir = 0;
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            dir = i + 1;
        }
    }
    return dir;
}

/* Opens the cue's FILE (relative to the cue), else a .bin with the cue's own name. */
static bool nw_disc_open_cue(NwDisc *disc, const char *cue, NwDiscFormat *format)
{
    char file[1024];
    char *target = NULL, *fallback = NULL;
    size_t len;
    char *text = SDL_LoadFile(cue, &len);
    if (!text) {
        return false;
    }
    bool ok = len <= NW_DISC_CUE_MAX ? nw_cue_parse(text, file, sizeof(file), format)
                                     : SDL_SetError("%s is too large for a cue sheet", cue);
    SDL_free(text);
    if (!ok) {
        return false;
    }

    bool absolute = file[0] == '/' || file[0] == '\\' || (SDL_isalpha(file[0]) && file[1] == ':');
    size_t dir = absolute ? 0 : nw_disc_dir_len(cue);
    if (SDL_asprintf(&target, "%.*s%s", (int)dir, cue, file) < 0) {
        return false;
    }
    disc->io = SDL_IOFromFile(target, "rb");
    if (!disc->io) {
        size_t stem = SDL_strlen(cue) - 4; /* ".cue" */
        if (SDL_asprintf(&fallback, "%.*s.bin", (int)stem, cue) >= 0) {
            disc->io = SDL_IOFromFile(fallback, "rb");
            if (!disc->io) {
                SDL_SetError("cannot open %s (named in the cue sheet) or %s", target, fallback);
            }
        }
    }
    SDL_free(fallback);
    SDL_free(target);
    return disc->io != NULL;
}

/* Raw if sector 16 starts with the sync pattern; the header's mode byte picks Mode 1 or 2. */
static bool nw_disc_detect(NwDisc *disc, Sint64 size, NwDiscFormat *format)
{
    uint8_t head[16];
    *format = NW_DISC_COOKED;
    if (size < (NW_DISC_PVD + 1) * NW_DISC_RAW) {
        return true;
    }
    if (SDL_SeekIO(disc->io, (Sint64)NW_DISC_PVD * NW_DISC_RAW, SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(disc->io, head, sizeof(head)) != sizeof(head)) {
        return false;
    }
    if (SDL_memcmp(head, nw_disc_sync, sizeof(nw_disc_sync)) != 0) {
        return true;
    }
    if (head[15] == 1) {
        *format = NW_DISC_RAW_MODE1;
    } else if (head[15] == 2) {
        *format = NW_DISC_RAW_MODE2;
    } else {
        return SDL_SetError("raw sector 16 has mode %u, expected 1 or 2", head[15]);
    }
    return true;
}

/* A clearer message for images that are not ISO9660 at all. */
static bool nw_disc_not_iso(NwDisc *disc)
{
    uint8_t head[0x20];
    if (SDL_SeekIO(disc->io, 0, SDL_IO_SEEK_SET) == 0 && SDL_ReadIO(disc->io, head, sizeof(head)) == sizeof(head)) {
        if (head[0x1c] == 0xc2 && head[0x1d] == 0x33 && head[0x1e] == 0x9f && head[0x1f] == 0x3d) {
            return SDL_SetError("GameCube disc image (%.6s), not a PS2 disc", (const char *)head);
        }
        if (head[0x18] == 0x5d && head[0x19] == 0x1c && head[0x1a] == 0x9e && head[0x1b] == 0xa3) {
            return SDL_SetError("Wii disc image (%.6s), not a PS2 disc", (const char *)head);
        }
    }
    return SDL_SetError("not an ISO9660 image");
}

bool nw_disc_open(NwDisc *disc, const char *path)
{
    NwDiscFormat format = NW_DISC_COOKED;
    uint8_t pvd[NW_DISC_SECTOR];
    bool cue = nw_disc_has_ext(path, ".cue");

    SDL_zerop(disc);
    if (cue) {
        if (!nw_disc_open_cue(disc, path, &format)) {
            return false;
        }
    } else {
        disc->io = SDL_IOFromFile(path, "rb");
        if (!disc->io) {
            return false;
        }
    }

    Sint64 size = SDL_GetIOSize(disc->io);
    if (size < 0 || (!cue && !nw_disc_detect(disc, size, &format))) {
        nw_disc_close(disc);
        return false;
    }
    disc->format = format;
    disc->sector_size = format == NW_DISC_COOKED ? NW_DISC_SECTOR : NW_DISC_RAW;
    disc->data_offset = format == NW_DISC_COOKED ? 0 : format == NW_DISC_RAW_MODE1 ? 16 : 24;
    if (size / disc->sector_size > UINT32_MAX) {
        nw_disc_close(disc);
        return SDL_SetError("image is too large");
    }
    disc->sectors = (uint32_t)(size / disc->sector_size);
    if (format != NW_DISC_COOKED) {
        disc->raw = SDL_malloc(NW_DISC_BATCH * NW_DISC_RAW);
        if (!disc->raw) {
            nw_disc_close(disc);
            return false;
        }
    }

    if (disc->sectors <= NW_DISC_PVD) {
        nw_disc_not_iso(disc);
        nw_disc_close(disc);
        return false;
    }
    if (!nw_disc_read(disc, NW_DISC_PVD, 1, pvd)) {
        nw_disc_close(disc);
        return false;
    }
    if (pvd[0] != 1 || SDL_memcmp(pvd + 1, "CD001", 5) != 0 || pvd[6] != 1) {
        nw_disc_not_iso(disc);
        nw_disc_close(disc);
        return false;
    }
    return true;
}

void nw_disc_close(NwDisc *disc)
{
    if (disc->io) {
        SDL_CloseIO(disc->io);
    }
    SDL_free(disc->raw);
    SDL_zerop(disc);
}

bool nw_disc_read(NwDisc *disc, uint32_t lsn, uint32_t count, void *dst)
{
    uint8_t *out = dst;

    if (lsn > disc->sectors || count > disc->sectors - lsn) {
        return SDL_SetError("sector %u is past the end of the image (%u sectors): truncated dump?",
                            lsn > disc->sectors ? lsn : disc->sectors, disc->sectors);
    }
    if (disc->format == NW_DISC_COOKED) {
        size_t size = (size_t)count * NW_DISC_SECTOR;
        if (SDL_SeekIO(disc->io, (Sint64)lsn * NW_DISC_SECTOR, SDL_IO_SEEK_SET) < 0 ||
            SDL_ReadIO(disc->io, out, size) != size) {
            return SDL_SetError("read error at sector %u", lsn);
        }
        return true;
    }

    while (count > 0) {
        uint32_t n = count < NW_DISC_BATCH ? count : NW_DISC_BATCH;
        size_t size = (size_t)n * NW_DISC_RAW;
        if (SDL_SeekIO(disc->io, (Sint64)lsn * NW_DISC_RAW, SDL_IO_SEEK_SET) < 0 ||
            SDL_ReadIO(disc->io, disc->raw, size) != size) {
            return SDL_SetError("read error at sector %u", lsn);
        }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *s = disc->raw + (size_t)i * NW_DISC_RAW;
            if (disc->format == NW_DISC_RAW_MODE2 && (s[NW_DISC_SUBMODE] & NW_DISC_FORM2)) {
                return SDL_SetError("sector %u is Mode 2 Form 2, not file data", lsn + i);
            }
            SDL_memcpy(out, s + disc->data_offset, NW_DISC_SECTOR);
            out += NW_DISC_SECTOR;
        }
        lsn += n;
        count -= n;
    }
    return true;
}

const char *nw_disc_format_name(NwDiscFormat format)
{
    switch (format) {
    case NW_DISC_COOKED:
        return "2048-byte sectors";
    case NW_DISC_RAW_MODE1:
        return "raw 2352 Mode 1";
    case NW_DISC_RAW_MODE2:
        return "raw 2352 Mode 2 Form 1";
    }
    return "?";
}
