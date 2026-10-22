/* The NuDat* override (src/port/nudat.c) and the sce file shim under the kept nufile layer, on a synthetic
   archive with a real RNC2 entry. Usage: test_nudat <scratch dir>.
   Local only, with your own disc: `test_nudat retail` loads every entry of ATS.DAT and CHARS.DAT from the
   extracted disc dir NUWORLD_TEST_DATA through NuFileLoadBuffer (packed ones unpacked by the decomp's NuPPUnpack,
   checked against the CRC in their RNC header); exits 77 without it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "nucore/nufile.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/vfs.h"

#include "dat_builder.h"
#include "rnc2_fixture.inc"

/* The kept nufile layer (no header declares it). */
int NuFileOpen(char *file, enum nufilemode_e mode);
int NuFileRead(int fh, void *data, int size);
int NuFileSeek(int fh, int offset, int origin);
int NuFilePos(int fh);
int NuFileOpenSize(int fh);
int NuFileSize(char *fname);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
void NuFileClose(int fh);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static void rnc2_payload(unsigned char out[NW_RNC2_UNPACKED])
{
    static const char text[] = "CrashWOC NuWorld RNC2 fixture. ";
    for (unsigned i = 0; i < 1200; i++) {
        out[i] = (unsigned char)text[i % (sizeof(text) - 1)];
    }
    for (unsigned i = 0; i < NW_RNC2_UNPACKED - 1200; i++) {
        out[1200 + i] = (unsigned char)(i * 7 + (i >> 3));
    }
}

/* The CRC of RNC headers (CRC-16, polynomial 0xA001). */
static unsigned rnc_crc16(const unsigned char *p, size_t n)
{
    unsigned crc = 0;
    while (n--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xa001u : crc >> 1;
        }
    }
    return crc & 0xffffu;
}

static void test_synthetic(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    unsigned char payload[NW_RNC2_UNPACKED], buf[2048];
    char text[8];
    DatBuild d;
    NwVfs vfs;

    SDL_asprintf(&root, "%snudat/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    build_dat_with(&d, nw_rnc2_fixture, sizeof(nw_rnc2_fixture), NW_RNC2_UNPACKED);
    SDL_asprintf(&path, "%sTEST.DAT", disc);
    CHECK(SDL_SaveFile(path, d.data, d.size));
    SDL_free(path);
    SDL_asprintf(&path, "%sLOOSE.BIN", disc);
    CHECK(SDL_SaveFile(path, nw_rnc2_fixture, sizeof(nw_rnc2_fixture)));
    SDL_free(path);
    rnc2_payload(payload);

    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);

    struct nudathdr_s *dat = NuDatOpen("test.dat", NULL, NULL);
    CHECK(dat && dat->nfiles == 4 && dat->finfo[-1].uplen == -1 && dat->intalloc == 1);
    if (!dat) {
        nw_shim_file_init(NULL);
        nw_vfs_shutdown(&vfs);
        return;
    }
    NuDatSet(dat);
    CHECK(NuFileSize("arc\\a\\x.bin") == NW_RNC2_UNPACKED);
    CHECK(NuFileSize("ARC\\A\\y.bin") == 3);
    CHECK(NuFileSize("arc\\top.bin") == 4);      /* entry 0: through an open, as retail */
    CHECK(NuFileSize("arc\\nope") == -1);        /* finfo[-1].uplen: the directory magic */
    CHECK(NuFileSize("arc\\arc\\top.bin") == -1); /* the root's back link is a miss on the host */

    memset(buf, 0xaa, sizeof(buf));
    CHECK(NuFileLoadBuffer("arc\\a\\x.bin", buf, sizeof(buf)) == (int)NW_RNC2_UNPACKED);
    CHECK(memcmp(buf, payload, NW_RNC2_UNPACKED) == 0);
    CHECK(NuFileLoadBuffer("arc\\b\\deep.bin", buf, sizeof(buf)) == 9 && memcmp(buf, "deep data", 9) == 0);
    /* No room for NuDatFileLoadBuffer's 0x100 margin: retail falls back to NuPPLoadBuffer on the entry. */
    memset(buf, 0, sizeof(buf));
    CHECK(NuFileLoadBuffer("arc\\a\\x.bin", buf, NW_RNC2_UNPACKED) == (int)NW_RNC2_UNPACKED);
    CHECK(memcmp(buf, payload, NW_RNC2_UNPACKED) == 0);

    int fh = NuFileOpen("arc\\a\\Y.BIN", NUFILE_READ);
    CHECK(fh >= 0x800 && NuFileOpenSize(fh) == 3);
    CHECK(NuFileRead(fh, text, 3) == 3 && memcmp(text, "why", 3) == 0);
    CHECK(NuFileSeek(fh, 1, 0) == 1 && NuFilePos(fh) == 1);
    CHECK(NuFileRead(fh, text, 2) == 2 && memcmp(text, "hy", 2) == 0);
    CHECK(NuFileSeek(fh, 1, 2) == 2); /* from the entry's end */
    CHECK(NuFileOpen("arc\\top.bin", NUFILE_READ) == 0); /* one open entry per archive, no loose file either */
    NuFileClose(fh);
    fh = NuFileOpen("arc\\top.bin", NUFILE_READ);
    CHECK(fh >= 0x800 && NuFileRead(fh, text, 4) == 4 && memcmp(text, "top!", 4) == 0);
    NuFileClose(fh);
    NuDatSet(NULL);
    NuDatClose(dat);

    /* A loose RNC2 file: NuPPLoadBuffer through the disc path. */
    memset(buf, 0, sizeof(buf));
    CHECK(NuFileLoadBuffer("loose.bin", buf, sizeof(buf)) == (int)NW_RNC2_UNPACKED);
    CHECK(memcmp(buf, payload, NW_RNC2_UNPACKED) == 0);
    CHECK(NuDatOpen("missing.dat", NULL, NULL) == NULL);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    SDL_free(disc);
    SDL_free(root);
}

typedef struct RetailCheck {
    NwDat *raw;
    unsigned entries, packed, bad;
} RetailCheck;

static void retail_entry(void *user, int index, const char *path)
{
    RetailCheck *c = user;
    const NwDatEntry *e = &c->raw->entries[index];
    bool packed = e->flags & NW_DAT_PACKED;
    int size = (int)e->unpacked_size;
    unsigned char *buf = malloc((size_t)size + 0x100);
    unsigned char *stored = nw_dat_load(c->raw, index);
    bool ok = buf && stored && NuFileSize((char *)path) == (index == 0 ? (int)e->size : size) &&
              NuFileLoadBuffer((char *)path, buf, size + 0x100) == size;
    if (ok && packed) {
        ok = e->size >= 18 && rnc_crc16(buf, (size_t)size) == ((unsigned)stored[12] << 8 | stored[13]);
    } else if (ok) {
        ok = memcmp(buf, stored, e->size) == 0;
    }
    if (!ok && c->bad++ < 5) {
        fprintf(stderr, "retail: %s does not load as stored\n", path);
    }
    c->entries++;
    c->packed += packed;
    SDL_free(stored);
    free(buf);
}

static int test_retail(void)
{
    const char *dir = getenv("NUWORLD_TEST_DATA");
    static const char *const archives[] = { "ATS.DAT", "CHARS.DAT" };
    NwVfs vfs = { 0 };
    if (!dir) {
        printf("NUWORLD_TEST_DATA is not set: skipped\n");
        return 77;
    }
    CHECK(nw_vfs_mount(&vfs, NW_VFS_DISC, dir));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    for (size_t i = 0; i < SDL_arraysize(archives); i++) {
        char *game_path = NULL;
        NwDat raw;
        RetailCheck c = { &raw, 0, 0, 0 };
        SDL_asprintf(&game_path, "cdrom0:\\%s;1", archives[i]);
        CHECK(nw_dat_open(&raw, &vfs, game_path));
        struct nudathdr_s *dat = NuDatOpen((char *)archives[i], NULL, NULL);
        CHECK(dat != NULL);
        if (dat) {
            NuDatSet(dat);
            CHECK(nw_dat_list(&raw, retail_entry, &c));
            NuDatSet(NULL);
            NuDatClose(dat);
        }
        printf("%s: %u entries, %u packed, %u wrong\n", archives[i], c.entries, c.packed, c.bad);
        CHECK(c.entries == raw.count && c.bad == 0);
        nw_dat_close(&raw);
        SDL_free(game_path);
    }
    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc < 2) {
        fprintf(stderr, "usage: test_nudat <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_synthetic(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
