/* VFS tests: SHA-256 vectors, synthetic disc images built at run time (.iso, raw Mode 2 / Mode 1 .bin, .cue)
   and disc verification against an injected manifest. Usage: test_vfs <scratch dir>.
   NUWORLD_TEST_DISC=<image or cue> also hashes every file of a real PAL v1.03 disc against the committed manifest. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "vfs/disc.h"
#include "vfs/iso9660.h"
#include "vfs/manifest.h"
#include "vfs/sha256.h"
#include "vfs/verify.h"

static int failures;
static const char *scratch;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

/* call must fail with an error containing text. */
#define CHECK_FAILS(call, text) \
    do { \
        if (call) { \
            fprintf(stderr, "%s:%d: %s succeeded, expected \"%s\"\n", __FILE__, __LINE__, #call, text); \
            failures++; \
        } else if (!SDL_strstr(SDL_GetError(), text)) { \
            fprintf(stderr, "%s:%d: %s: error \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #call, SDL_GetError(), \
                    text); \
            failures++; \
        } \
    } while (0)

/* ---- SHA-256 ---------------------------------------------------------------------------------------------- */

static bool sha_is(const void *data, size_t size, const char *hex)
{
    uint8_t digest[NW_SHA256_SIZE];
    char got[NW_SHA256_HEX];
    nw_sha256(data, size, digest);
    nw_sha256_hex(digest, got);
    return strcmp(got, hex) == 0;
}

static void test_sha256(void)
{
    static const char abc448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const char abc896[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopq"
                                 "klmnopqrlmnopqrsmnopqrstnopqrstu";
    CHECK(sha_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(sha_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(sha_is(abc448, strlen(abc448), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(sha_is(abc896, strlen(abc896), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));

    /* One million 'a', fed in uneven pieces. */
    static uint8_t million[1000000];
    uint8_t digest[NW_SHA256_SIZE];
    char hex[NW_SHA256_HEX];
    NwSha256 s;
    memset(million, 'a', sizeof(million));
    nw_sha256_init(&s);
    for (size_t at = 0, step = 1; at < sizeof(million); at += step, step = step * 3 % 1021 + 1) {
        nw_sha256_update(&s, million + at, step < sizeof(million) - at ? step : sizeof(million) - at);
    }
    nw_sha256_final(&s, digest);
    nw_sha256_hex(digest, hex);
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
}

/* ---- Committed manifest ---------------------------------------------------------------------------------- */

static void test_disc_manifest(void)
{
    const NwManifest *m = &nw_disc_manifest;
    const NwManifestEntry *cnf = nw_manifest_find(m, "system.cnf");
    const NwManifestEntry *elf = nw_manifest_find(m, NW_DISC_BOOT);
    unsigned skipped = 0;

    CHECK(m->count == 451);
    CHECK(cnf && cnf->size == 56 &&
          strcmp(cnf->sha256, "86fe327e4576516f57004800b2663c75c8a65a62872f3fdf3aa0618daf23d9c1") == 0);
    CHECK(elf && elf->size == 6731763 &&
          strcmp(elf->sha256, "7fc6826d08c42a5a92fc018edae3e520783beb5a8330410251192bfe26b1eca1") == 0);
    for (size_t i = 0; i < m->count; i++) {
        if (m->entries[i].flags & NW_MANIFEST_SKIP) {
            skipped++;
            CHECK(strcmp(m->entries[i].path, "DUMMY.DAT") == 0);
        }
        CHECK(i == 0 || strcmp(m->entries[i - 1].path, m->entries[i].path) < 0);
    }
    CHECK(skipped == 1);
}

/* ---- Synthetic ISO9660 writer ---------------------------------------------------------------------------- */

#define IMG_SECTORS 40
#define LSN_ROOT 18 /* 2 sectors */
#define LSN_LEVELS 20
#define LSN_B 21
#define LSN_HUB 22
#define LSN_CNF 23
#define LSN_ELF 24 /* 3 sectors */
#define LSN_DUMMY 27 /* 2 sectors */
#define LSN_GSC 29 /* 3 sectors */
#define LSN_END 32

#define ELF_SIZE 5000
#define DUMMY_SIZE 3000
#define GSC_SIZE 4100

static const char pal_cnf[] = "BOOT2 = cdrom0:\\SLES_503.86;1\r\nVER = 1.03\r\nVMODE = PAL\r\n";

typedef struct Spec {
    const char *cnf;
    uint8_t elf_xor; /* flips bits of the ELF's first byte */
    uint8_t gsc_flags;
    uint8_t gsc_unit;
    bool duplicate;
} Spec;

typedef struct Image {
    uint8_t data[IMG_SECTORS * NW_DISC_SECTOR];
    size_t size;
} Image;

typedef struct Dir {
    uint8_t *base;
    size_t pos;
} Dir;

static uint8_t elf_data[ELF_SIZE], gsc_data[GSC_SIZE], dummy_data[DUMMY_SIZE];

static void fill(uint8_t *p, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
}

static void put_both32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
        p[7 - i] = (uint8_t)(v >> (8 * i));
    }
}

static void dir_record(Dir *d, const char *name, size_t nlen, uint32_t lba, uint32_t size, uint8_t flags, uint8_t unit)
{
    size_t len = 33 + nlen + (nlen % 2 == 0);
    if (d->pos % NW_DISC_SECTOR + len > NW_DISC_SECTOR) {
        d->pos = (d->pos / NW_DISC_SECTOR + 1) * NW_DISC_SECTOR;
    }
    uint8_t *r = d->base + d->pos;
    r[0] = (uint8_t)len;
    put_both32(r + 2, lba);
    put_both32(r + 10, size);
    r[25] = flags;
    r[26] = unit;
    r[28] = 1;
    r[32] = (uint8_t)nlen;
    memcpy(r + 33, name, nlen);
    d->pos += len;
}

static void dir_file(Dir *d, const char *name, uint32_t lba, uint32_t size)
{
    dir_record(d, name, strlen(name), lba, size, 0, 0);
}

static void dir_sub(Dir *d, const char *name, uint32_t lba)
{
    dir_record(d, name, strlen(name), lba, NW_DISC_SECTOR, 2, 0);
}

static Dir dir_begin(Image *img, uint32_t lsn, uint32_t parent, uint32_t sectors)
{
    Dir d = { img->data + (size_t)lsn * NW_DISC_SECTOR, 0 };
    dir_record(&d, "\0", 1, lsn, sectors * NW_DISC_SECTOR, 2, 0);
    dir_record(&d, "\1", 1, parent, NW_DISC_SECTOR, 2, 0);
    return d;
}

/* Root: DUMMY.DAT EMPTY.BIN LEVELS/ | (next sector) SLES_503.86 SYSTEM.CNF; LEVELS/B/HUB/HUB.GSC. */
static void build_image(Image *img, const Spec *spec)
{
    memset(img, 0, sizeof(*img));
    img->size = (size_t)LSN_END * NW_DISC_SECTOR;
    fill(elf_data, ELF_SIZE, 1);
    fill(gsc_data, GSC_SIZE, 2);
    memset(dummy_data, 0, DUMMY_SIZE);

    uint8_t *pvd = img->data + 16 * NW_DISC_SECTOR;
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    memset(pvd + 40, ' ', 32);
    memcpy(pvd + 40, "NWTEST", 6);
    pvd[128] = 0x00, pvd[129] = 0x08, pvd[130] = 0x08, pvd[131] = 0x00;
    Dir pvd_root = { pvd + 156, 0 };
    dir_record(&pvd_root, "\0", 1, LSN_ROOT, 2 * NW_DISC_SECTOR, 2, 0);
    uint8_t *term = img->data + 17 * NW_DISC_SECTOR;
    term[0] = 255;
    memcpy(term + 1, "CD001", 5);
    term[6] = 1;

    Dir root = dir_begin(img, LSN_ROOT, LSN_ROOT, 2);
    dir_file(&root, "DUMMY.DAT;1", LSN_DUMMY, DUMMY_SIZE);
    dir_file(&root, "EMPTY.BIN;1", 0, 0);
    dir_sub(&root, "LEVELS", LSN_LEVELS);
    root.pos = NW_DISC_SECTOR; /* zero padding to the end of the first sector */
    dir_file(&root, "SLES_503.86;1", LSN_ELF, ELF_SIZE);
    dir_file(&root, "SYSTEM.CNF;1", LSN_CNF, (uint32_t)strlen(spec->cnf));
    if (spec->duplicate) {
        dir_file(&root, "SYSTEM.CNF;1", LSN_CNF, (uint32_t)strlen(spec->cnf));
    }
    Dir levels = dir_begin(img, LSN_LEVELS, LSN_ROOT, 1);
    dir_sub(&levels, "B", LSN_B);
    Dir b = dir_begin(img, LSN_B, LSN_LEVELS, 1);
    dir_sub(&b, "HUB", LSN_HUB);
    Dir hub = dir_begin(img, LSN_HUB, LSN_B, 1);
    dir_record(&hub, "HUB.GSC;1", 9, LSN_GSC, GSC_SIZE, spec->gsc_flags, spec->gsc_unit);

    memcpy(img->data + LSN_CNF * NW_DISC_SECTOR, spec->cnf, strlen(spec->cnf));
    memcpy(img->data + LSN_ELF * NW_DISC_SECTOR, elf_data, ELF_SIZE);
    img->data[LSN_ELF * NW_DISC_SECTOR] ^= spec->elf_xor;
    memcpy(img->data + LSN_DUMMY * NW_DISC_SECTOR, dummy_data, DUMMY_SIZE);
    memcpy(img->data + LSN_GSC * NW_DISC_SECTOR, gsc_data, GSC_SIZE);
}

static uint8_t bcd(unsigned v)
{
    return (uint8_t)(v / 10 * 16 + v % 10);
}

/* 2352-byte sectors; form2_lsn marks one sector as Mode 2 Form 2. */
static uint8_t *to_raw(const Image *img, int mode, uint32_t form2_lsn, size_t *size)
{
    static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    uint32_t sectors = (uint32_t)(img->size / NW_DISC_SECTOR);
    uint8_t *out = calloc(sectors, 2352);
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t *r = out + (size_t)s * 2352;
        unsigned lba = s + 150;
        memcpy(r, sync, sizeof(sync));
        r[12] = bcd(lba / 75 / 60);
        r[13] = bcd(lba / 75 % 60);
        r[14] = bcd(lba % 75);
        r[15] = (uint8_t)mode;
        if (mode == 2) {
            r[18] = r[22] = s == form2_lsn ? 0x20 : 0x08;
            memcpy(r + 24, img->data + (size_t)s * NW_DISC_SECTOR, NW_DISC_SECTOR);
        } else {
            memcpy(r + 16, img->data + (size_t)s * NW_DISC_SECTOR, NW_DISC_SECTOR);
        }
    }
    *size = (size_t)sectors * 2352;
    return out;
}

static char *scratch_path(const char *name)
{
    static char path[4][1024];
    static int slot;
    slot = (slot + 1) % 4;
    SDL_snprintf(path[slot], sizeof(path[slot]), "%s/%s", scratch, name);
    return path[slot];
}

static void write_file(const char *name, const void *data, size_t size)
{
    if (!SDL_SaveFile(scratch_path(name), data, size)) {
        fprintf(stderr, "cannot write %s: %s\n", name, SDL_GetError());
        exit(1);
    }
}

static void write_text(const char *name, const char *text)
{
    write_file(name, text, strlen(text));
}

/* ---- Manifest of the synthetic disc ---------------------------------------------------------------------- */

typedef struct TestManifest {
    NwManifestEntry entries[8];
    char hex[8][NW_SHA256_HEX];
    NwManifest m;
} TestManifest;

static void manifest_add(TestManifest *t, const char *path, const void *data, size_t size, unsigned flags)
{
    size_t i = t->m.count++;
    uint8_t digest[NW_SHA256_SIZE];
    nw_sha256(data, size, digest);
    nw_sha256_hex(digest, t->hex[i]);
    t->entries[i] = (NwManifestEntry){ path, size, t->hex[i], flags };
}

static void manifest_build(TestManifest *t)
{
    memset(t, 0, sizeof(*t));
    t->m.entries = t->entries;
    manifest_add(t, "DUMMY.DAT", dummy_data, DUMMY_SIZE, NW_MANIFEST_SKIP);
    manifest_add(t, "EMPTY.BIN", "", 0, 0);
    manifest_add(t, "LEVELS/B/HUB/HUB.GSC", gsc_data, GSC_SIZE, 0);
    manifest_add(t, "SLES_503.86", elf_data, ELF_SIZE, 0);
    manifest_add(t, "SYSTEM.CNF", pal_cnf, strlen(pal_cnf), 0);
}

/* ---- Tests ------------------------------------------------------------------------------------------------ */

static void check_tree(NwDisc *disc)
{
    NwIso iso;
    uint8_t buf[GSC_SIZE];
    if (!nw_iso_index(&iso, disc)) {
        CHECK(!"nw_iso_index");
        return;
    }
    CHECK(iso.count == 5);
    CHECK(strcmp(iso.volume_id, "NWTEST") == 0);
    const NwIsoFile *gsc = nw_iso_find(&iso, "levels\\b\\hub\\hub.gsc;1");
    CHECK(gsc && gsc->size == GSC_SIZE && strcmp(gsc->path, "LEVELS/B/HUB/HUB.GSC") == 0);
    CHECK(nw_iso_find(&iso, "/LEVELS/B/HUB/HUB.GSC") == gsc);
    CHECK(nw_iso_find(&iso, "LEVELS/B/HUB") == NULL);
    CHECK(nw_iso_find(&iso, "LEVELS") == NULL);
    CHECK(nw_iso_find(&iso, "EMPTY.BIN") && nw_iso_find(&iso, "EMPTY.BIN")->size == 0);
    CHECK(nw_iso_find(&iso, "SLES_503.86") != NULL);
    if (gsc) {
        CHECK(nw_iso_read(disc, gsc, 0, buf, GSC_SIZE) && memcmp(buf, gsc_data, GSC_SIZE) == 0);
        CHECK(nw_iso_read(disc, gsc, 2000, buf, 300) && memcmp(buf, gsc_data + 2000, 300) == 0);
        CHECK(nw_iso_read(disc, gsc, GSC_SIZE - 1, buf, 1) && buf[0] == gsc_data[GSC_SIZE - 1]);
        CHECK_FAILS(nw_iso_read(disc, gsc, GSC_SIZE - 1, buf, 2), "past the end");
    }
    nw_iso_free(&iso);
}

static void check_open(const char *name, NwDiscFormat format)
{
    NwDisc disc;
    if (!nw_disc_open(&disc, scratch_path(name))) {
        fprintf(stderr, "%s: %s\n", name, SDL_GetError());
        failures++;
        return;
    }
    CHECK(disc.format == format);
    CHECK(disc.sectors == LSN_END);
    check_tree(&disc);
    nw_disc_close(&disc);
}

static void test_formats(void)
{
    Image *img = malloc(sizeof(*img));
    Spec spec = { pal_cnf, 0, 0, 0, false };
    size_t size;
    uint8_t *raw;

    build_image(img, &spec);
    write_file("disc.iso", img->data, img->size);
    raw = to_raw(img, 2, UINT32_MAX, &size);
    write_file("mode2.bin", raw, size);
    write_file("kopie.bin", raw, size);
    free(raw);
    raw = to_raw(img, 1, UINT32_MAX, &size);
    write_file("mode1.bin", raw, size);
    free(raw);

    check_open("disc.iso", NW_DISC_COOKED);
    check_open("mode2.bin", NW_DISC_RAW_MODE2);
    check_open("mode1.bin", NW_DISC_RAW_MODE1);

    write_text("mode2.cue", "FILE \"mode2.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    check_open("mode2.cue", NW_DISC_RAW_MODE2);
    write_text("mode1.cue", "\xef\xbb\xbf" "file mode1.bin binary\n track 01 mode1/2352\n  index 01 00:00:00\n");
    check_open("mode1.cue", NW_DISC_RAW_MODE1);
    write_text("cooked.cue", "FILE \"disc.iso\" BINARY\n  TRACK 01 MODE1/2048\n");
    check_open("cooked.cue", NW_DISC_COOKED);
    /* FILE names a missing image: fall back to the cue's own name (the "- Kopie.cue" case). */
    write_text("kopie.cue", "FILE \"renamed.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n");
    check_open("kopie.cue", NW_DISC_RAW_MODE2);

    NwDisc disc;
    write_text("audio.cue", "FILE \"mode2.bin\" BINARY\n  TRACK 01 AUDIO\n");
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("audio.cue")), "AUDIO");
    write_text("track2.cue", "FILE \"mode2.bin\" BINARY\n  TRACK 02 MODE2/2352\n");
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("track2.cue")), "first track");
    write_text("lost.cue", "FILE \"lost.bin\" BINARY\n  TRACK 01 MODE2/2352\n");
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("lost.cue")), "lost.bin");
    write_text("empty.cue", "REM nothing\n");
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("empty.cue")), "no FILE");

    /* Not ISO9660. */
    memset(img->data, 0, sizeof(img->data));
    write_file("zero.iso", img->data, img->size);
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("zero.iso")), "ISO9660");
    write_file("tiny.iso", img->data, 100);
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("tiny.iso")), "ISO9660");
    memcpy(img->data, "GCBE7D", 6);
    img->data[0x1c] = 0xc2, img->data[0x1d] = 0x33, img->data[0x1e] = 0x9f, img->data[0x1f] = 0x3d;
    write_file("gamecube.iso", img->data, img->size);
    CHECK_FAILS(nw_disc_open(&disc, scratch_path("gamecube.iso")), "GameCube");
    free(img);
}

static void progress_cb(void *user, uint64_t done, uint64_t total)
{
    uint64_t *last = user;
    CHECK(done > last[0] && done <= total);
    last[0] = done;
    last[1] = total;
}

/* Opens a synthetic image of spec as .iso and indexes it. */
static bool open_spec(const Spec *spec, NwDisc *disc, NwIso *iso)
{
    Image *img = malloc(sizeof(*img));
    build_image(img, spec);
    write_file("spec.iso", img->data, img->size);
    free(img);
    if (!nw_disc_open(disc, scratch_path("spec.iso"))) {
        return false;
    }
    if (!nw_iso_index(iso, disc)) {
        nw_disc_close(disc);
        return false;
    }
    return true;
}

static void test_verify(void)
{
    TestManifest t;
    NwDisc disc;
    NwIso iso;
    Spec spec = { pal_cnf, 0, 0, 0, false };
    uint64_t last[2];

    CHECK(open_spec(&spec, &disc, &iso));
    manifest_build(&t);
    CHECK(nw_verify_identity(&disc, &iso, &t.m));
    last[0] = last[1] = 0;
    CHECK(nw_verify_files(&disc, &iso, &t.m, 0, progress_cb, last));
    CHECK(last[0] == ELF_SIZE + GSC_SIZE + strlen(pal_cnf) && last[1] == last[0]);
    last[0] = last[1] = 0;
    CHECK(nw_verify_files(&disc, &iso, &t.m, NW_VERIFY_SKIPPED, progress_cb, last));
    CHECK(last[0] == ELF_SIZE + GSC_SIZE + DUMMY_SIZE + strlen(pal_cnf));

    /* A changed file is named with both hashes. */
    t.hex[2][0] = t.hex[2][0] == '0' ? '1' : '0';
    CHECK_FAILS(nw_verify_files(&disc, &iso, &t.m, 0, NULL, NULL), "LEVELS/B/HUB/HUB.GSC: SHA-256");
    manifest_build(&t);
    t.entries[0].size++;
    CHECK_FAILS(nw_verify_files(&disc, &iso, &t.m, 0, NULL, NULL), "DUMMY.DAT is 3000 bytes, expected 3001");
    manifest_build(&t);
    t.entries[1].path = "MISSING.BIN";
    CHECK_FAILS(nw_verify_files(&disc, &iso, &t.m, 0, NULL, NULL), "EMPTY.BIN is not part of PAL v1.03");
    manifest_build(&t);
    t.entries[t.m.count++] = (NwManifestEntry){ "ZZZ.BIN", 0, t.hex[1], 0 };
    CHECK_FAILS(nw_verify_files(&disc, &iso, &t.m, 0, NULL, NULL), "ZZZ.BIN is missing");
    nw_iso_free(&iso);
    nw_disc_close(&disc);

    static const struct {
        const char *cnf;
        const char *error;
    } refused[] = {
        { "BOOT2 = cdrom0:\\SLES_503.86;1\r\nVER = 2.01\r\nVMODE = PAL\r\n", "VER 2.01: only PAL v1.03" },
        { "BOOT2 = cdrom0:\\SLUS_202.38;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n", "SLUS_202.38: US disc" },
        { "BOOT2 = cdrom0:\\SLES_503.86;1\r\nVER = 1.03\r\nVMODE = NTSC\r\n", "VMODE NTSC" },
        { "BOOT = cdrom:\\SCES_000.01;1\r\nTCB = 4\r\n", "PS1 disc" },
        { "BOOT2 = cdrom0:\\SLES_503.86;1\r\nVER = 1.03\r\nVMODE = PAL\r\n\r\n", "SYSTEM.CNF differs" },
    };
    for (size_t i = 0; i < SDL_arraysize(refused); i++) {
        spec.cnf = refused[i].cnf;
        if (open_spec(&spec, &disc, &iso)) {
            CHECK_FAILS(nw_verify_identity(&disc, &iso, &t.m), refused[i].error);
            nw_iso_free(&iso);
            nw_disc_close(&disc);
        } else {
            CHECK(!"open_spec");
        }
    }

    spec.cnf = pal_cnf;
    spec.elf_xor = 0x80;
    manifest_build(&t);
    if (open_spec(&spec, &disc, &iso)) {
        CHECK_FAILS(nw_verify_identity(&disc, &iso, &t.m), "SLES_503.86: SHA-256");
        nw_iso_free(&iso);
        nw_disc_close(&disc);
    }
    spec.elf_xor = 0;
    t.entries[3].size = 6731763;
    if (open_spec(&spec, &disc, &iso)) {
        CHECK_FAILS(nw_verify_identity(&disc, &iso, &t.m), "SLES_503.86 is 5000 bytes, expected 6731763");
        nw_iso_free(&iso);
        nw_disc_close(&disc);
    }
}

static void test_refused(void)
{
    Image *img = malloc(sizeof(*img));
    NwDisc disc;
    NwIso iso;
    Spec spec = { pal_cnf, 0, 0x80, 0, false };
    size_t size;

    CHECK(!open_spec(&spec, &disc, &iso) && SDL_strstr(SDL_GetError(), "multi-extent"));
    spec.gsc_flags = 0;
    spec.gsc_unit = 1;
    CHECK(!open_spec(&spec, &disc, &iso) && SDL_strstr(SDL_GetError(), "interleaved"));
    spec.gsc_unit = 0;
    spec.duplicate = true;
    CHECK(!open_spec(&spec, &disc, &iso) && SDL_strstr(SDL_GetError(), "duplicate name SYSTEM.CNF"));
    spec.duplicate = false;

    /* A truncated dump: HUB.GSC runs past the end. */
    build_image(img, &spec);
    write_file("short.iso", img->data, (LSN_GSC + 1) * NW_DISC_SECTOR);
    CHECK(nw_disc_open(&disc, scratch_path("short.iso")));
    CHECK_FAILS(nw_iso_index(&iso, &disc), "LEVELS/B/HUB/HUB.GSC lies past the end");
    nw_disc_close(&disc);

    /* Form 2 sectors are not file data. */
    uint8_t *raw = to_raw(img, 2, LSN_GSC + 1, &size);
    write_file("form2.bin", raw, size);
    free(raw);
    CHECK(nw_disc_open(&disc, scratch_path("form2.bin")));
    CHECK(nw_iso_index(&iso, &disc));
    TestManifest t;
    manifest_build(&t);
    CHECK_FAILS(nw_verify_files(&disc, &iso, &t.m, 0, NULL, NULL), "sector 30 is Mode 2 Form 2");
    nw_iso_free(&iso);
    nw_disc_close(&disc);
    free(img);
}

/* ---- Real disc (local only) ------------------------------------------------------------------------------ */

static void real_progress(void *user, uint64_t done, uint64_t total)
{
    int *percent = user;
    int now = (int)(done * 100 / total);
    if (now != *percent) {
        *percent = now;
        printf("\r  %3d%%", now);
        fflush(stdout);
    }
}

static void test_real_disc(const char *path)
{
    NwDisc disc;
    NwIso iso;
    int percent = -1;
    Uint64 start = SDL_GetTicks();

    if (!nw_disc_open(&disc, path)) {
        fprintf(stderr, "NUWORLD_TEST_DISC: %s\n", SDL_GetError());
        failures++;
        return;
    }
    printf("%s: %s, %u sectors\n", path, nw_disc_format_name(disc.format), disc.sectors);
    bool ok = nw_iso_index(&iso, &disc);
    if (ok) {
        ok = nw_verify_identity(&disc, &iso, &nw_disc_manifest) &&
             nw_verify_files(&disc, &iso, &nw_disc_manifest, NW_VERIFY_SKIPPED, real_progress, &percent);
        printf("\n");
        if (ok) {
            printf("  %u files match the manifest (%.1f s)\n", iso.count, (SDL_GetTicks() - start) / 1000.0);
        }
        nw_iso_free(&iso);
    }
    if (!ok) {
        fprintf(stderr, "NUWORLD_TEST_DISC: %s\n", SDL_GetError());
        failures++;
    }
    nw_disc_close(&disc);
}

/* ---- Main ----------------------------------------------------------------------------------------------- */

static SDL_EnumerationResult SDLCALL remove_entry(void *user, const char *dir, const char *name)
{
    char path[1024];
    SDL_PathInfo info;
    (void)user;
    SDL_snprintf(path, sizeof(path), "%s%s", dir, name);
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(path, remove_entry, NULL);
    }
    return SDL_RemovePath(path) ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_vfs <scratch dir>\n");
        return 2;
    }
    scratch = argv[1];
    SDL_EnumerateDirectory(scratch, remove_entry, NULL);
    if (!SDL_CreateDirectory(scratch)) {
        fprintf(stderr, "%s: %s\n", scratch, SDL_GetError());
        return 1;
    }

    test_sha256();
    test_disc_manifest();
    test_formats();
    test_verify();
    test_refused();

    const char *disc = getenv("NUWORLD_TEST_DISC");
    if (disc && *disc) {
        test_real_disc(disc);
    }

    SDL_EnumerateDirectory(scratch, remove_entry, NULL);
    SDL_RemovePath(scratch);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("vfs: all checks passed\n");
    return 0;
}
