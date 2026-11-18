#include "updater/archive.h"

#include <SDL3/SDL.h>

#include "miniz.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#define NW_ARCHIVE_ENTRIES 1024
#define NW_ARCHIVE_FILE ((uint64_t)256 << 20)
#define NW_ARCHIVE_TOTAL ((uint64_t)512 << 20)
#define NW_ARCHIVE_NAME 200
#define NW_ARCHIVE_PAX 4096

typedef struct NwArchive {
    const char *dir;
    unsigned entries;
    uint64_t total;
    SDL_IOStream *out;
    char *path;
} NwArchive;

typedef struct NwTar {
    uint8_t header[512];
    size_t have;
    uint64_t left, pad;
    bool end, pax, program;
    char note[NW_ARCHIVE_PAX + 1];
    size_t noted;
} NwTar;

static bool nw_archive_name(const char *name, const char **rel)
{
    size_t root = SDL_strlen(NW_ARCHIVE_ROOT);
    if (SDL_strlen(name) >= NW_ARCHIVE_NAME || SDL_strncmp(name, NW_ARCHIVE_ROOT, root) != 0) {
        return SDL_SetError("an entry is outside " NW_ARCHIVE_ROOT);
    }
    *rel = name + root;
    const char *part = *rel;
    for (const char *c = *rel;; c++) {
        if ((unsigned char)*c < 0x20 ? *c != '\0' : SDL_strchr("\\:<>\"|?*", *c) != NULL) {
            return SDL_SetError("an entry has a name no update uses");
        }
        if (*c == '/' || *c == '\0') {
            size_t len = (size_t)(c - part);
            if ((len == 0 && *c == '/') || (len <= 2 && part[0] == '.' && (len == 1 || part[1] == '.'))) {
                return SDL_SetError("an entry leaves " NW_ARCHIVE_ROOT);
            }
            if (*c == '\0') {
                return true;
            }
            part = c + 1;
        }
    }
}

static bool nw_archive_dir(NwArchive *a, const char *rel)
{
    char *path = NULL;
    bool ok = SDL_asprintf(&path, "%s%s", a->dir, rel) >= 0 && SDL_CreateDirectory(path);
    SDL_free(path);
    return ok;
}

static bool nw_archive_open(NwArchive *a, const char *rel, uint64_t size)
{
    a->total += size;
    if (++a->entries > NW_ARCHIVE_ENTRIES || size > NW_ARCHIVE_FILE || a->total > NW_ARCHIVE_TOTAL) {
        return SDL_SetError("the archive holds more than an update may");
    }
    if (SDL_asprintf(&a->path, "%s%s", a->dir, rel) < 0) {
        a->path = NULL;
        return false;
    }
    char *slash = SDL_strrchr(a->path + SDL_strlen(a->dir), '/');
    if (slash) {
        *slash = '\0';
        bool made = SDL_CreateDirectory(a->path);
        *slash = '/';
        if (!made) {
            return false;
        }
    }
    a->out = SDL_IOFromFile(a->path, "wb");
    return a->out != NULL;
}

static bool nw_archive_close(NwArchive *a, bool ok, bool program)
{
    if (a->out) {
        ok = SDL_CloseIO(a->out) && ok;
        a->out = NULL;
    }
#ifndef _WIN32
    if (ok && a->path && chmod(a->path, program ? 0755 : 0644) != 0) {
        ok = SDL_SetError("cannot set the mode of %s", a->path);
    }
#else
    (void)program;
#endif
    SDL_free(a->path);
    a->path = NULL;
    return ok;
}

static size_t nw_archive_write(void *user, mz_uint64 at, const void *p, size_t n)
{
    (void)at;
    return SDL_WriteIO(((NwArchive *)user)->out, p, n);
}

static bool nw_archive_zip(NwArchive *a, const void *data, size_t size)
{
    mz_zip_archive zip;
    SDL_zero(zip);
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        return SDL_SetError("the zip archive does not open: %s", mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    bool ok = true;
    for (mz_uint i = 0; ok && i < mz_zip_reader_get_num_files(&zip); i++) {
        mz_zip_archive_file_stat st;
        const char *rel = NULL;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            ok = SDL_SetError("the zip archive is damaged");
        } else if (!nw_archive_name(st.m_filename, &rel)) {
            ok = false;
        } else if (st.m_is_directory || !*rel) {
            ok = nw_archive_dir(a, rel);
        } else if (st.m_is_encrypted || !st.m_is_supported) {
            ok = SDL_SetError("the zip archive holds more than plain files");
        } else {
            ok = nw_archive_open(a, rel, st.m_uncomp_size) &&
                 (mz_zip_reader_extract_to_callback(&zip, i, nw_archive_write, a, 0) ||
                  SDL_SetError("%s does not unpack: %s", rel, mz_zip_get_error_string(mz_zip_get_last_error(&zip))));
            ok = nw_archive_close(a, ok, ((st.m_external_attr >> 16) & 0111) != 0);
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

static uint64_t nw_archive_octal(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    size_t i = 0;
    if (p[0] & 0x80) {
        return UINT64_MAX;
    }
    while (i < n && p[i] == ' ') {
        i++;
    }
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
        v = v * 8 + (uint64_t)(p[i] - '0');
    }
    return v;
}

static bool nw_archive_tar_entry(NwArchive *a, NwTar *t)
{
    const uint8_t *h = t->header;
    unsigned sum = 0, any = 0;
    for (int i = 0; i < 512; i++) {
        sum += i >= 148 && i < 156 ? ' ' : h[i];
        any |= h[i];
    }
    if (!any) {
        t->end = true;
        return true;
    }
    if (SDL_memcmp(h + 257, "ustar", 5) != 0 || sum != nw_archive_octal(h + 148, 8)) {
        return SDL_SetError("the tar archive is damaged");
    }
    char name[155 + 1 + 100 + 1];
    size_t n = SDL_strnlen((const char *)h + 345, 155), m = SDL_strnlen((const char *)h, 100);
    SDL_memcpy(name, h + 345, n);
    if (n) {
        name[n++] = '/';
    }
    SDL_memcpy(name + n, h, m);
    name[n + m] = '\0';
    uint64_t size = nw_archive_octal(h + 124, 12);
    t->left = size;
    t->pad = (512 - size % 512) % 512;
    t->pax = h[156] == 'x' || h[156] == 'g';
    if (t->pax) {
        t->noted = 0;
        return size <= NW_ARCHIVE_PAX || SDL_SetError("the tar archive has an oversized header");
    }
    const char *rel = NULL;
    if (!nw_archive_name(name, &rel)) {
        return false;
    }
    if (h[156] == '5' || !*rel) {
        return nw_archive_dir(a, rel);
    }
    if (h[156] != '0' && h[156] != '\0') {
        return SDL_SetError("the tar archive holds more than plain files");
    }
    t->program = (nw_archive_octal(h + 100, 8) & 0111) != 0;
    return nw_archive_open(a, rel, size) && (size != 0 || nw_archive_close(a, true, t->program));
}

static bool nw_archive_tar(NwArchive *a, NwTar *t, const uint8_t *p, size_t n)
{
    while (n && !t->end) {
        if (t->left) {
            size_t take = (size_t)SDL_min((uint64_t)n, t->left);
            if (a->out && SDL_WriteIO(a->out, p, take) != take) {
                return false;
            }
            if (t->pax) {
                SDL_memcpy(t->note + t->noted, p, take);
                t->noted += take;
            }
            p += take;
            n -= take;
            t->left -= take;
            if (!t->left && a->out && !nw_archive_close(a, true, t->program)) {
                return false;
            }
            if (!t->left && t->pax) {
                t->note[t->noted] = '\0';
                if (SDL_strstr(t->note, "path=")) {
                    return SDL_SetError("the tar archive renames its entries");
                }
            }
        } else if (t->pad) {
            size_t take = (size_t)SDL_min((uint64_t)n, t->pad);
            p += take;
            n -= take;
            t->pad -= take;
        } else {
            size_t take = SDL_min(n, sizeof(t->header) - t->have);
            SDL_memcpy(t->header + t->have, p, take);
            t->have += take;
            p += take;
            n -= take;
            if (t->have == sizeof(t->header)) {
                t->have = 0;
                if (!nw_archive_tar_entry(a, t)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool nw_archive_targz(NwArchive *a, const uint8_t *data, size_t size)
{
    size_t at = 10;
    if (size < 18 || data[2] != 8 || (data[3] & 0xE0)) {
        return SDL_SetError("the gzip archive does not open");
    }
    if (data[3] & 4) {
        at += 2 + (size_t)(data[10] | data[11] << 8);
    }
    for (int field = 8; field <= 16; field *= 2) {
        if (data[3] & field) {
            while (at < size && data[at++]) {
            }
        }
    }
    if (data[3] & 2) {
        at += 2;
    }
    if (at + 8 > size) {
        return SDL_SetError("the gzip archive does not open");
    }
    size_t end = size - 8, fill = 0;
    tinfl_decompressor *inflate = SDL_malloc(sizeof(*inflate));
    uint8_t *window = SDL_malloc(TINFL_LZ_DICT_SIZE);
    NwTar *tar = SDL_calloc(1, sizeof(*tar));
    bool ok = inflate && window && tar;
    mz_ulong crc = MZ_CRC32_INIT;
    tinfl_status status = TINFL_STATUS_HAS_MORE_OUTPUT;
    if (ok) {
        tinfl_init(inflate);
    }
    while (ok && status == TINFL_STATUS_HAS_MORE_OUTPUT) {
        size_t in = end - at, out = TINFL_LZ_DICT_SIZE - fill;
        status = tinfl_decompress(inflate, data + at, &in, window, window + fill, &out, 0);
        at += in;
        crc = mz_crc32(crc, window + fill, out);
        ok = status >= TINFL_STATUS_DONE ? nw_archive_tar(a, tar, window + fill, out)
                                         : SDL_SetError("the gzip archive is damaged");
        fill = (fill + out) & (TINFL_LZ_DICT_SIZE - 1);
    }
    uint32_t want = (uint32_t)data[end] | (uint32_t)data[end + 1] << 8 | (uint32_t)data[end + 2] << 16 |
                    (uint32_t)data[end + 3] << 24;
    if (ok && (status != TINFL_STATUS_DONE || (uint32_t)crc != want || !tar->end || a->out)) {
        ok = SDL_SetError("the tar.gz archive is cut short");
    }
    SDL_free(tar);
    SDL_free(window);
    SDL_free(inflate);
    return ok;
}

bool nw_archive_unpack(const void *data, size_t size, const char *dir)
{
    const uint8_t *p = data;
    NwArchive a = { .dir = dir };
    bool ok = size >= 4 && p[0] == 'P' && p[1] == 'K'    ? nw_archive_zip(&a, data, size)
              : size >= 4 && p[0] == 0x1F && p[1] == 0x8B ? nw_archive_targz(&a, p, size)
                                                          : SDL_SetError("the update is neither zip nor tar.gz");
    if (a.out) {
        SDL_CloseIO(a.out);
    }
    SDL_free(a.path);
    return ok;
}
