/* VFS tests: SHA-256 vectors, synthetic disc images built at run time (.iso, raw Mode 2 / Mode 1 .bin, .cue),
   disc verification and import against an injected manifest, data folder resolution, game path mapping and a
   synthetic .DAT archive, the import folder listing and the wizard's background import job.
   Usage: test_vfs <scratch dir>.
   Local only, with your own disc:
   NUWORLD_TEST_DISC=<image or cue> hashes every file of a real PAL v1.03 disc against the committed manifest;
   NUWORLD_TEST_DATA=<extracted disc dir> checks ATS.DAT, CHARS.DAT and SFX.DAT, and NUWORLD_TEST_DAT_LIST=<file>
   writes their listings (archive, index, path, offset, size, unpacked size, flags). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "frontend/import.h"
#include "platform/paths.h"
#include "vfs/dat.h"
#include "vfs/disc.h"
#include "vfs/extract.h"
#include "vfs/iso9660.h"
#include "vfs/manifest.h"
#include "vfs/sha256.h"
#include "vfs/verify.h"
#include "vfs/vfs.h"

#include "dat_builder.h"
#include "disc_builder.h"

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

static bool progress_cb(void *user, uint64_t done, uint64_t total)
{
    uint64_t *last = user;
    CHECK(done > last[0] && done <= total);
    last[0] = done;
    last[1] = total;
    return true;
}

/* Cancels once more than *user bytes are done. */
static bool cancel_cb(void *user, uint64_t done, uint64_t total)
{
    (void)total;
    return done <= *(uint64_t *)user;
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

/* ---- Import and data folder ----------------------------------------------------------------------------- */

static bool exists(const char *path)
{
    return SDL_GetPathInfo(path, NULL);
}

static bool file_is(const char *path, const void *data, size_t size)
{
    size_t got;
    void *bytes = SDL_LoadFile(path, &got);
    bool same = bytes && got == size && memcmp(bytes, data, size) == 0;
    SDL_free(bytes);
    return same;
}

static void test_import(void)
{
    TestManifest t;
    NwDisc disc;
    NwIso iso;
    SDL_PathInfo info;
    Spec spec = { pal_cnf, 0, 0, 0, false };
    uint64_t last[2] = { 0, 0 };
    char root[1024], path[1024];

#define AT(name) (SDL_snprintf(path, sizeof(path), "%s%s", root, name), path)
    SDL_snprintf(root, sizeof(root), "%s/data/", scratch);
    if (!open_spec(&spec, &disc, &iso)) {
        CHECK(!"open_spec");
        return;
    }
    manifest_build(&t);
    CHECK(nw_extract_disc(&disc, &iso, &t.m, root, progress_cb, last));
    CHECK(last[0] == last[1] && last[1] == ELF_SIZE + GSC_SIZE + strlen(pal_cnf));
    CHECK(file_is(AT("disc/LEVELS/B/HUB/HUB.GSC"), gsc_data, GSC_SIZE));
    CHECK(file_is(AT("disc/SLES_503.86"), elf_data, ELF_SIZE));
    CHECK(file_is(AT("disc/SYSTEM.CNF"), pal_cnf, strlen(pal_cnf)));
    CHECK(SDL_GetPathInfo(AT("disc/EMPTY.BIN"), &info) && info.size == 0);
    CHECK(!exists(AT("disc/DUMMY.DAT")));
    CHECK(exists(AT("disc/" NW_DISC_STAMP)));
    CHECK(!exists(AT("disc.partial")));
    CHECK(nw_extract_verify(root, &t.m, NULL, NULL));

    /* --verify notices a changed file. */
    SDL_SaveFile(AT("disc/LEVELS/B/HUB/HUB.GSC"), "changed", 7);
    CHECK_FAILS(nw_extract_verify(root, &t.m, NULL, NULL), "LEVELS/B/HUB/HUB.GSC is 7 bytes, expected 4100");

    /* A hash mismatch aborts the import: no disc.partial/ is left and the old disc/ is untouched. */
    SDL_SaveFile(AT("disc/OLD.TXT"), "old", 3);
    t.hex[2][0] = t.hex[2][0] == '0' ? '1' : '0';
    CHECK_FAILS(nw_extract_disc(&disc, &iso, &t.m, root, NULL, NULL), "LEVELS/B/HUB/HUB.GSC: SHA-256");
    CHECK(!exists(AT("disc.partial")));
    CHECK(exists(AT("disc/OLD.TXT")));

    /* A good import replaces the old disc/. */
    manifest_build(&t);
    CHECK(nw_extract_disc(&disc, &iso, &t.m, root, NULL, NULL));
    CHECK(!exists(AT("disc/OLD.TXT")) && !exists(AT("disc.old")) && !exists(AT("disc.partial")));
    CHECK(nw_extract_verify(root, &t.m, NULL, NULL));

    /* Cancelling from the progress callback stops the import like a failure; verify can be cancelled too. */
    uint64_t limit = 0;
    SDL_SaveFile(AT("disc/OLD.TXT"), "old", 3);
    CHECK_FAILS(nw_extract_disc(&disc, &iso, &t.m, root, cancel_cb, &limit), "cancelled");
    CHECK(!exists(AT("disc.partial")) && exists(AT("disc/OLD.TXT")) && nw_extract_has_disc(root));
    CHECK_FAILS(nw_extract_verify(root, &t.m, cancel_cb, &limit), "cancelled");
    limit = GSC_SIZE;
    CHECK_FAILS(nw_extract_disc(&disc, &iso, &t.m, root, cancel_cb, &limit), "cancelled");
    CHECK(!exists(AT("disc.partial")) && exists(AT("disc/OLD.TXT")));
    SDL_RemovePath(AT("disc/OLD.TXT"));
#ifdef SDL_PLATFORM_WINDOWS
    /* A file held open in disc/ blocks the swap: the import fails cleanly and keeps the old disc/. */
    SDL_IOStream *held = SDL_IOFromFile(AT("disc/SYSTEM.CNF"), "rb");
    CHECK_FAILS(nw_extract_disc(&disc, &iso, &t.m, root, NULL, NULL), "cannot move aside");
    CHECK(!exists(AT("disc.partial")) && !exists(AT("disc.old")) && exists(AT("disc/" NW_DISC_STAMP)));
    SDL_CloseIO(held);
#endif
    nw_iso_free(&iso);
    nw_disc_close(&disc);

    SDL_RemovePath(AT("disc/" NW_DISC_STAMP));
    CHECK(!nw_extract_has_disc(root));
    CHECK_FAILS(nw_extract_verify(root, &t.m, NULL, NULL), "no imported disc");

    /* A refused disc creates nothing. */
    SDL_snprintf(root, sizeof(root), "%s/data2/", scratch);
    spec.cnf = "BOOT2 = cdrom0:\\SLES_503.86;1\r\nVER = 2.01\r\nVMODE = PAL\r\n";
    if (open_spec(&spec, &disc, &iso)) {
        CHECK_FAILS(nw_extract_disc(&disc, &iso, &t.m, root, NULL, NULL), "VER 2.01");
        CHECK(!exists(AT("disc.partial")) && !exists(AT("disc")));
        nw_iso_free(&iso);
        nw_disc_close(&disc);
    }
#undef AT
}

static void test_paths(void)
{
    char base[1024], want[1024];
    char *root, *saves, *dir;

    SDL_snprintf(base, sizeof(base), "%s/exe/", scratch);
    CHECK(SDL_CreateDirectory(base));
    root = nw_paths_data_root(base, "pref/");
    CHECK(root && strcmp(root, "pref/") == 0);
    SDL_free(root);

    /* portable.txt beside the exe: <exe dir>/user/. */
    SDL_snprintf(want, sizeof(want), "%s" NW_PATHS_PORTABLE, base);
    CHECK(SDL_SaveFile(want, "", 0));
    root = nw_paths_data_root(base, "pref/");
    SDL_snprintf(want, sizeof(want), "%suser/", base);
    CHECK(root && strcmp(root, want) == 0);

    saves = root ? nw_paths_dir(root, "saves", true) : NULL;
    SDL_snprintf(want, sizeof(want), "%suser/saves/", base);
    CHECK(saves && strcmp(saves, want) == 0 && exists(saves));
    SDL_free(saves);
    SDL_free(root);

    /* The data folder layout: root, import/ and saves/, also when root does not exist yet. */
    SDL_snprintf(want, sizeof(want), "%sfresh/data", base);
    CHECK(nw_paths_create_layout(want));
    SDL_snprintf(want, sizeof(want), "%sfresh/data/" NW_PATHS_IMPORT, base);
    CHECK(exists(want));
    SDL_snprintf(want, sizeof(want), "%sfresh/data/" NW_PATHS_SAVES, base);
    CHECK(exists(want));
    SDL_snprintf(want, sizeof(want), "%sfresh/data/", base);
    CHECK(nw_paths_create_layout(want));

    dir = nw_paths_dir("a\\b", NULL, false);
    CHECK(dir && strcmp(dir, "a\\b/") == 0);
    SDL_free(dir);
    dir = nw_paths_dir("a/", "disc", false);
    CHECK(dir && strcmp(dir, "a/disc/") == 0);
    SDL_free(dir);

    CHECK(nw_paths_remove_tree(base) && !exists(base));
    CHECK(nw_paths_remove_tree(base));
}

/* ---- Game paths --------------------------------------------------------------------------------------- */

static bool maps_to(const char *game_path, NwVfsDevice device, const char *want)
{
    NwVfsDevice got;
    char out[NW_VFS_PATH_MAX];
    return nw_vfs_map(game_path, &got, out) && got == device && strcmp(out, want) == 0;
}

static void test_map(void)
{
    NwVfsDevice device;
    char out[NW_VFS_PATH_MAX];

    /* NuFileOpen builds "cdrom0:\" + upper(path) + ";1" and "host0:" + upper(path). */
    CHECK(maps_to("cdrom0:\\LEVELS\\B\\HUB\\HUB.GSC;1", NW_VFS_CDROM, "LEVELS/B/HUB/HUB.GSC"));
    CHECK(maps_to("cdrom0:\\levels\\b\\hub\\hub.gsc;1", NW_VFS_CDROM, "LEVELS/B/HUB/HUB.GSC"));
    CHECK(maps_to("CDROM0:\\SYS\\IOPRP23.IMG;1", NW_VFS_CDROM, "SYS/IOPRP23.IMG"));
    CHECK(maps_to("host0:LEVELS\\B\\HUB\\HUB.GSC", NW_VFS_HOST, "LEVELS/B/HUB/HUB.GSC"));
    CHECK(maps_to("host0:saves\\slot;0.bin", NW_VFS_HOST, "SAVES/SLOT;0.BIN"));
    CHECK(maps_to("levels\\b\\hub\\hub.gsc", NW_VFS_CDROM, "LEVELS/B/HUB/HUB.GSC"));
    CHECK(maps_to("cdrom0:\\\\STUFF//FONT.GSC;1", NW_VFS_CDROM, "STUFF/FONT.GSC"));
    CHECK_FAILS(nw_vfs_map("mc0:/BESLES-50386/slot", &device, out), "unknown device");
    CHECK_FAILS(nw_vfs_map("host0:..\\..\\secret", &device, out), "bad path component");
    CHECK_FAILS(nw_vfs_map("cdrom0:\\;1", &device, out), "bad path component");
    CHECK_FAILS(nw_vfs_map("cdrom0:\\", &device, out), "no file name");
}

static void test_vfs(void)
{
    char root[1024], path[1024], buf[16];
    NwVfs vfs;
    NwVfsFile *f;

#define AT(name) (SDL_snprintf(path, sizeof(path), "%s%s", root, name), path)
    SDL_snprintf(root, sizeof(root), "%s/vfs/", scratch);
    CHECK_FAILS(nw_vfs_init(&vfs, root), "no imported disc");
    CHECK(SDL_CreateDirectory(AT("disc/LEVELS/B/HUB")) && SDL_CreateDirectory(AT("disc/Stuff")));
    CHECK(SDL_SaveFile(AT("disc/LEVELS/B/HUB/HUB.GSC"), "disc hub", 8));
    CHECK(SDL_SaveFile(AT("disc/Stuff/Font.Gsc"), "font", 4));
    CHECK(SDL_SaveFile(AT("disc/" NW_DISC_STAMP), "", 0));
    if (!nw_vfs_init(&vfs, root)) {
        CHECK(!"nw_vfs_init");
        return;
    }
    CHECK(vfs.mounts[NW_VFS_DISC].count == 2);
    CHECK(vfs.mounts[NW_VFS_OVERLAY].count == 0 && !vfs.mounts[NW_VFS_OVERLAY].root);

    /* Case-insensitive lookups; open, read, seek, size, close. */
    f = nw_vfs_open(&vfs, "cdrom0:\\STUFF\\FONT.GSC;1", NW_VFS_READ);
    CHECK(f && nw_vfs_size(f) == 4 && nw_vfs_read(f, buf, sizeof(buf)) == 4 && memcmp(buf, "font", 4) == 0);
    CHECK(f && nw_vfs_seek(f, 1, 0) == 1 && nw_vfs_read(f, buf, 2) == 2 && memcmp(buf, "on", 2) == 0);
    CHECK(f && nw_vfs_seek(f, -1, 2) == 3 && nw_vfs_seek(f, -1, 1) == 2);
    nw_vfs_close(f);
    CHECK(!nw_vfs_open(&vfs, "cdrom0:\\STUFF\\NOPE.GSC;1", NW_VFS_READ) && SDL_strstr(SDL_GetError(), "not found"));
    CHECK(!nw_vfs_open(&vfs, "cdrom0:\\STUFF\\FONT.GSC;1", NW_VFS_WRITE) &&
          SDL_strstr(SDL_GetError(), "read-only"));
    CHECK(!nw_vfs_open(&vfs, "cdrom0:\\.NUWORLD-DISC;1", NW_VFS_READ));

    /* The overlay slot comes first. */
    CHECK(SDL_CreateDirectory(AT("mod/levels/b/hub")));
    CHECK(SDL_SaveFile(AT("mod/levels/b/hub/hub.gsc"), "mod hub", 7));
    CHECK(nw_vfs_mount(&vfs, NW_VFS_OVERLAY, AT("mod")));
    f = nw_vfs_open(&vfs, "cdrom0:\\LEVELS\\B\\HUB\\HUB.GSC;1", NW_VFS_READ);
    CHECK(f && nw_vfs_read(f, buf, sizeof(buf)) == 7 && memcmp(buf, "mod hub", 7) == 0);
    nw_vfs_close(f);
    CHECK(nw_vfs_mount(&vfs, NW_VFS_OVERLAY, NULL));
    f = nw_vfs_open(&vfs, "cdrom0:\\LEVELS\\B\\HUB\\HUB.GSC;1", NW_VFS_READ);
    CHECK(f && nw_vfs_read(f, buf, sizeof(buf)) == 8 && memcmp(buf, "disc hub", 8) == 0);
    nw_vfs_close(f);
    CHECK_FAILS(nw_vfs_mount(&vfs, NW_VFS_OVERLAY, AT("missing")), "not a folder");

    /* host0: is writable and uppercased like NuFileOpen. */
    f = nw_vfs_open(&vfs, "host0:SAVES\\SLOT0.BIN", NW_VFS_WRITE);
    CHECK(f && nw_vfs_write(f, "abc", 3) == 3);
    nw_vfs_close(f);
    f = nw_vfs_open(&vfs, "host0:saves\\slot0.bin", NW_VFS_APPEND);
    CHECK(f && nw_vfs_write(f, "de", 2) == 2);
    nw_vfs_close(f);
    CHECK(file_is(AT("host0/SAVES/SLOT0.BIN"), "abcde", 5));
    nw_vfs_shutdown(&vfs);
#undef AT
}

/* ---- .DAT archives -------------------------------------------------------------------------------------- */

typedef struct DatListing {
    const NwDat *dat;
    char paths[8][64];
    int listed;
    int roundtrip_failures;
    uint32_t packed;
    FILE *out;
    const char *archive;
} DatListing;

static void dat_listed(void *user, int index, const char *path)
{
    DatListing *l = user;
    const NwDatEntry *e = &l->dat->entries[index];
    if (index < 8) {
        SDL_strlcpy(l->paths[index], path, sizeof(l->paths[index]));
    }
    l->listed++;
    l->packed += (e->flags & NW_DAT_PACKED) != 0;
    if (nw_dat_find(l->dat, path) != index) {
        l->roundtrip_failures++;
    }
    if (l->out) {
        fprintf(l->out, "%s %d %s %u %u %u %u\n", l->archive, index, path, e->offset, e->size, e->unpacked_size,
                e->flags);
    }
}

static bool dat_parse_patched(const DatBuild *d, size_t at, uint32_t value)
{
    uint8_t dir[1024];
    NwDat dat = { 0 };
    memcpy(dir, d->data + d->dir_offset, d->dir_size);
    put_le32(dir + at, value);
    bool ok = nw_dat_parse(&dat, dir, d->dir_size, d->size);
    nw_dat_close(&dat);
    return ok;
}

static void test_dat(void)
{
    DatBuild d;
    NwDat dat = { 0 };
    DatListing listing = { 0 };

    build_dat(&d);
    if (!nw_dat_parse(&dat, d.data + d.dir_offset, d.dir_size, d.size)) {
        CHECK(!"nw_dat_parse");
        return;
    }
    CHECK(dat.count == 4 && dat.node_count == 8);
    CHECK(nw_dat_find(&dat, "arc\\top.bin") == 0);
    CHECK(nw_dat_find(&dat, "arc\\a\\x.bin") == 1);
    CHECK(nw_dat_find(&dat, "ARC\\A\\X.BIN") == 1);
    CHECK(nw_dat_find(&dat, "arc\\a\\y.bin") == 3);
    CHECK(nw_dat_find(&dat, "arc\\b\\deep.bin") == 2);
    CHECK(nw_dat_find(&dat, "arc\\a") == -1);            /* a directory */
    CHECK(nw_dat_find(&dat, "arc\\missing.bin") == -1);  /* runs into the back link */
    CHECK(nw_dat_find(&dat, "arc\\top.bin\\x") == -1);   /* a file mid-path */
    CHECK(nw_dat_find(&dat, "arc/a/x.bin") == -1);       /* '\' only, like the engine */
    CHECK(nw_dat_find(&dat, "other\\top.bin") == -1);
    CHECK((dat.entries[1].flags & NW_DAT_PACKED) && dat.entries[1].unpacked_size == 100);
    CHECK(!(dat.entries[0].flags & NW_DAT_PACKED) && dat.entries[0].unpacked_size == 4);

    listing.dat = &dat;
    CHECK(nw_dat_list(&dat, dat_listed, &listing));
    CHECK(listing.listed == 4 && listing.roundtrip_failures == 0 && listing.packed == 1);
    CHECK(strcmp(listing.paths[0], "arc\\top.bin") == 0 && strcmp(listing.paths[1], "arc\\a\\x.bin") == 0);
    CHECK(strcmp(listing.paths[2], "arc\\b\\deep.bin") == 0 && strcmp(listing.paths[3], "arc\\a\\Y.BIN") == 0);
    nw_dat_close(&dat);

    /* Bounds checks. */
    size_t nodes_at = 8 + 4 * 16 + 4;
    CHECK(!dat_parse_patched(&d, 0, 0x12345678) && SDL_strstr(SDL_GetError(), "magic"));
    CHECK(!dat_parse_patched(&d, 8 + 16, 2000) && SDL_strstr(SDL_GetError(), "outside the archive"));
    CHECK(!dat_parse_patched(&d, nodes_at + 6 * 8, 0xfff7) && SDL_strstr(SDL_GetError(), "links out of range"));
    CHECK(!dat_parse_patched(&d, nodes_at + 6 * 8 + 4, 999) && SDL_strstr(SDL_GetError(), "name out of range"));
    CHECK(!dat_parse_patched(&d, nodes_at + 8 * 8, 1000) && SDL_strstr(SDL_GetError(), "names are"));
    CHECK(!dat_parse_patched(&d, 4, 1000) && SDL_strstr(SDL_GetError(), "file table"));

    /* Through the VFS. */
    char root[1024], path[1024];
    NwVfs vfs;
    SDL_snprintf(root, sizeof(root), "%s/datvfs/", scratch);
    SDL_snprintf(path, sizeof(path), "%sdisc", root);
    CHECK(SDL_CreateDirectory(path));
    SDL_snprintf(path, sizeof(path), "%sdisc/" NW_DISC_STAMP, root);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_snprintf(path, sizeof(path), "%sdisc/TEST.DAT", root);
    CHECK(SDL_SaveFile(path, d.data, d.size));
    put_le32(d.data, 5000);
    SDL_snprintf(path, sizeof(path), "%sdisc/BAD.DAT", root);
    CHECK(SDL_SaveFile(path, d.data, d.size));
    if (nw_vfs_init(&vfs, root)) {
        CHECK(nw_dat_open(&dat, &vfs, "cdrom0:\\test.dat;1"));
        int ix = nw_dat_find(&dat, "arc\\a\\x.bin");
        void *bytes = nw_dat_load(&dat, ix);
        CHECK(bytes && memcmp(bytes, dat_payload[1], strlen(dat_payload[1])) == 0);
        SDL_free(bytes);
        CHECK(!nw_dat_load(&dat, 4));
        char piece[4];
        CHECK(nw_dat_read(&dat, 2, 5, piece, 4) && memcmp(piece, "data", 4) == 0); /* "deep data" */
        CHECK(nw_dat_read(&dat, 2, 9, piece, 0));
        CHECK_FAILS(nw_dat_read(&dat, 2, 6, piece, 4), "runs past");
        CHECK_FAILS(nw_dat_read(&dat, 2, 10, piece, 0), "runs past");
        CHECK_FAILS(nw_dat_read(&dat, 4, 0, piece, 1), "no .DAT entry");
        nw_dat_close(&dat);
        CHECK_FAILS(nw_dat_open(&dat, &vfs, "cdrom0:\\BAD.DAT;1"), "runs past the file");
        CHECK_FAILS(nw_dat_open(&dat, &vfs, "cdrom0:\\DUMMY.DAT;1"), "not an archive");
        nw_vfs_shutdown(&vfs);
    } else {
        CHECK(!"nw_vfs_init");
    }
}

/* ---- Import folder and job (wizard) ------------------------------------------------------------------- */

static bool listed(const NwImageList *list, const char *const *names, size_t count)
{
    if (list->count != count) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(list->names[i], names[i]) != 0) {
            return false;
        }
    }
    return true;
}

/* Polls a job until it ends (10 s at most). */
static NwImportState job_wait(NwImport *job, uint64_t *done, uint64_t *total)
{
    NwImportState state = NW_IMPORT_RUNNING;
    for (int i = 0; i < 10000 && (state = nw_import_poll(job, done, total)) == NW_IMPORT_RUNNING; i++) {
        SDL_Delay(1);
    }
    return state;
}

static void test_import_folder(void)
{
    static const char *const want[] = { "a.ISO", "b.cue", "c.bin", "d.Bin", "e.iso" };
    static const char *const files[] = { "e.iso", "b.cue", "b.BIN", "c.bin", "a.ISO", "d.Bin", "d.txt", "notes.txt",
                                         ".hidden.iso", "iso", ".bin" };
    NwImageList list;
    char dir[1024], path[1024];

    CHECK(nw_import_is_image("X.CUE") && nw_import_is_image("x.iso") && nw_import_is_image("x.Bin"));
    CHECK(!nw_import_is_image("x.img") && !nw_import_is_image("iso") && !nw_import_is_image("x.iso.txt"));

    SDL_snprintf(dir, sizeof(dir), "%s/import/", scratch);
    CHECK(nw_import_scan(dir, &list) && list.count == 0); /* missing folder */
    nw_import_list_free(&list);

    CHECK(SDL_CreateDirectory(dir));
    for (size_t i = 0; i < SDL_arraysize(files); i++) {
        SDL_snprintf(path, sizeof(path), "%s%s", dir, files[i]);
        CHECK(SDL_SaveFile(path, "x", 1));
    }
    SDL_snprintf(path, sizeof(path), "%sfolder.iso", dir);
    CHECK(SDL_CreateDirectory(path));
    /* b.BIN belongs to b.cue; directories and dot files are left out. */
    CHECK(nw_import_scan(dir, &list) && listed(&list, want, SDL_arraysize(want)));
    nw_import_list_free(&list);
    dir[strlen(dir) - 1] = '\0'; /* without the trailing separator */
    CHECK(nw_import_scan(dir, &list) && listed(&list, want, SDL_arraysize(want)));
    nw_import_list_free(&list);
}

static void test_import_job(void)
{
    TestManifest t;
    NwDisc disc;
    NwIso iso;
    Spec spec = { pal_cnf, 0, 0, 0, false };
    uint64_t done, total;
    char root[1024], path[1024];

    SDL_snprintf(root, sizeof(root), "%s/jobdata/", scratch);
    if (!open_spec(&spec, &disc, &iso)) {
        CHECK(!"open_spec");
        return;
    }
    nw_iso_free(&iso);
    nw_disc_close(&disc);
    manifest_build(&t);

    NwImport *job = nw_import_start(scratch_path("spec.iso"), root, &t.m);
    CHECK(job);
    if (job) {
        CHECK(job_wait(job, &done, &total) == NW_IMPORT_DONE);
        CHECK(done == total && total == ELF_SIZE + GSC_SIZE + strlen(pal_cnf));
        CHECK(nw_extract_has_disc(root) && nw_extract_verify(root, &t.m, NULL, NULL));
        SDL_snprintf(path, sizeof(path), "%sdisc/LEVELS/B/HUB/HUB.GSC", root);
        CHECK(file_is(path, gsc_data, GSC_SIZE));
        nw_import_free(job);
    }

    /* Errors come from the job's thread. */
    job = nw_import_start(scratch_path("missing.iso"), root, &t.m);
    CHECK(job);
    if (job) {
        CHECK(job_wait(job, &done, &total) == NW_IMPORT_FAILED);
        CHECK(SDL_strstr(nw_import_error(job), "missing.iso"));
        nw_import_free(job);
    }
    t.hex[2][0] = t.hex[2][0] == '0' ? '1' : '0';
    job = nw_import_start(scratch_path("spec.iso"), root, &t.m);
    CHECK(job);
    if (job) {
        CHECK(job_wait(job, &done, &total) == NW_IMPORT_FAILED);
        CHECK(SDL_strstr(nw_import_error(job), "LEVELS/B/HUB/HUB.GSC: SHA-256"));
        nw_import_free(job);
    }
    CHECK(nw_extract_has_disc(root)); /* the earlier import is kept */

    /* Freeing a running job cancels it and waits; whatever it got to, no disc.partial/ is left. */
    manifest_build(&t);
    job = nw_import_start(scratch_path("spec.iso"), root, &t.m);
    CHECK(job);
    nw_import_free(job);
    SDL_snprintf(path, sizeof(path), "%sdisc.partial", root);
    CHECK(!exists(path) && nw_extract_has_disc(root));
}

/* ---- Real disc (local only) ------------------------------------------------------------------------------ */

static bool real_progress(void *user, uint64_t done, uint64_t total)
{
    int *percent = user;
    int now = (int)(done * 100 / total);
    if (now != *percent) {
        *percent = now;
        printf("\r  %3d%%", now);
        fflush(stdout);
    }
    return true;
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

static void test_real_dats(const char *dir)
{
    static const struct {
        const char *name;
        const char *path;
        int count;
        uint32_t packed;
    } dats[] = {
        { "ATS.DAT", "cdrom0:\\ATS.DAT;1", 34, 0 },
        { "CHARS.DAT", "cdrom0:\\CHARS.DAT;1", 1076, 1066 },
        { "SFX.DAT", "cdrom0:\\SFX.DAT;1", 782, 0 },
    };
    const char *list = getenv("NUWORLD_TEST_DAT_LIST");
    FILE *out = list && *list ? fopen(list, "w") : NULL;
    NwVfs vfs;

    SDL_zero(vfs);
    if (!nw_vfs_mount(&vfs, NW_VFS_DISC, dir)) {
        fprintf(stderr, "NUWORLD_TEST_DATA: %s\n", SDL_GetError());
        failures++;
        return;
    }
    for (size_t i = 0; i < SDL_arraysize(dats); i++) {
        NwDat dat;
        DatListing listing = { 0 };
        if (!nw_dat_open(&dat, &vfs, dats[i].path)) {
            fprintf(stderr, "NUWORLD_TEST_DATA: %s\n", SDL_GetError());
            failures++;
            continue;
        }
        listing.dat = &dat;
        listing.out = out;
        listing.archive = dats[i].name;
        CHECK(nw_dat_list(&dat, dat_listed, &listing));
        CHECK(listing.listed == dats[i].count && (int)dat.count == dats[i].count);
        CHECK(listing.packed == dats[i].packed && listing.roundtrip_failures == 0);
        printf("%s: %d entries, %u packed\n", dats[i].name, listing.listed, listing.packed);
        nw_dat_close(&dat);
    }
    if (out) {
        fclose(out);
    }
    nw_vfs_shutdown(&vfs);
}

/* ---- Main ----------------------------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_vfs <scratch dir>\n");
        return 2;
    }
    scratch = argv[1];
    if (!nw_paths_remove_tree(scratch) || !SDL_CreateDirectory(scratch)) {
        fprintf(stderr, "%s: %s\n", scratch, SDL_GetError());
        return 1;
    }

    test_sha256();
    test_disc_manifest();
    test_formats();
    test_verify();
    test_refused();
    test_import();
    test_paths();
    test_map();
    test_vfs();
    test_dat();
    test_import_folder();
    test_import_job();

    const char *disc = getenv("NUWORLD_TEST_DISC");
    if (disc && *disc) {
        test_real_disc(disc);
    }
    const char *data = getenv("NUWORLD_TEST_DATA");
    if (data && *data) {
        test_real_dats(data);
    }

    nw_paths_remove_tree(scratch);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("vfs: all checks passed\n");
    return 0;
}
