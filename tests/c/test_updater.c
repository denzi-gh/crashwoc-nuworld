#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "miniz.h"
#include "monocypher-ed25519.h"
#include "platform/paths.h"
#include "platform/update.h"
#include "updater/apply.h"
#include "updater/archive.h"
#include "updater/job.h"
#include "updater/pubkey.h"

static int failures;
static const char *root;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed (%s)\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

typedef struct Blob {
    uint8_t *data;
    size_t size;
} Blob;

typedef struct Release {
    const char *dir;
    int fetched;
} Release;

static void unhex(const char *hex, uint8_t *out)
{
    for (size_t i = 0; hex[i * 2]; i++) {
        unsigned byte = 0;
        sscanf(hex + i * 2, "%2x", &byte);
        out[i] = (uint8_t)byte;
    }
}

static char *path(const char *dir, const char *name)
{
    char *p = NULL;
    SDL_asprintf(&p, "%s%s%s", root, dir, name);
    return p;
}

static void put(const char *dir, const char *name, const void *data, size_t size)
{
    char *p = path(dir, name);
    char *slash = SDL_strrchr(p, '/');
    *slash = '\0';
    SDL_CreateDirectory(p);
    *slash = '/';
    CHECK(SDL_SaveFile(p, data, size));
    SDL_free(p);
}

static bool holds(const char *dir, const char *name, const char *text)
{
    char *p = path(dir, name);
    size_t size = 0;
    char *data = SDL_LoadFile(p, &size);
    bool same = data && size == SDL_strlen(text) && SDL_memcmp(data, text, size) == 0;
    SDL_free(data);
    SDL_free(p);
    return same;
}

static bool there(const char *dir, const char *name)
{
    char *p = path(dir, name);
    bool found = SDL_GetPathInfo(p, NULL);
    SDL_free(p);
    return found;
}

static Blob zip_of(const char *const names[], const char *const texts[], int count)
{
    mz_zip_archive zip;
    Blob out = { 0 };
    void *data = NULL;
    SDL_zero(zip);
    CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    for (int i = 0; i < count; i++) {
        CHECK(mz_zip_writer_add_mem(&zip, names[i], texts[i], SDL_strlen(texts[i]), MZ_DEFAULT_COMPRESSION));
    }
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &out.size));
    out.data = SDL_malloc(out.size);
    SDL_memcpy(out.data, data, out.size);
    mz_free(data);
    mz_zip_writer_end(&zip);
    return out;
}

static void tar_entry(Blob *tar, const char *name, const char *text, char type, unsigned mode)
{
    size_t size = SDL_strlen(text), padded = (size + 511) / 512 * 512;
    tar->data = SDL_realloc(tar->data, tar->size + 512 + padded);
    uint8_t *h = tar->data + tar->size;
    SDL_memset(h, 0, 512 + padded);
    SDL_strlcpy((char *)h, name, 100);
    snprintf((char *)h + 100, 8, "%07o", mode);
    snprintf((char *)h + 124, 12, "%011o", (unsigned)size);
    h[156] = (uint8_t)type;
    SDL_memcpy(h + 257, "ustar", 6);
    SDL_memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) {
        sum += h[i];
    }
    snprintf((char *)h + 148, 7, "%06o", sum);
    SDL_memcpy(h + 512, text, size);
    tar->size += 512 + padded;
}

static Blob gzip_of(Blob tar)
{
    static const uint8_t head[10] = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3 };
    Blob out = { 0 };
    size_t packed = 0;
    tar.data = SDL_realloc(tar.data, tar.size + 1024);
    SDL_memset(tar.data + tar.size, 0, 1024);
    tar.size += 1024;
    void *deflated = tdefl_compress_mem_to_heap(tar.data, tar.size, &packed, TDEFL_DEFAULT_MAX_PROBES);
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, tar.data, tar.size), length = (uint32_t)tar.size;
    out.size = sizeof(head) + packed + 8;
    out.data = SDL_malloc(out.size);
    SDL_memcpy(out.data, head, sizeof(head));
    SDL_memcpy(out.data + sizeof(head), deflated, packed);
    for (int i = 0; i < 4; i++) {
        out.data[sizeof(head) + packed + i] = (uint8_t)(crc >> (8 * i));
        out.data[sizeof(head) + packed + 4 + i] = (uint8_t)(length >> (8 * i));
    }
    mz_free(deflated);
    SDL_free(tar.data);
    return out;
}

static bool unpack(Blob archive, const char *dir)
{
    char *d = path(dir, "");
    d[SDL_strlen(d) - 1] = '\0';
    nw_paths_remove_tree(d);
    SDL_CreateDirectory(d);
    d[SDL_strlen(d)] = '/';
    bool ok = nw_archive_unpack(archive.data, archive.size, d);
    SDL_free(d);
    SDL_free(archive.data);
    return ok;
}

static void test_signatures(void)
{
    uint8_t key[2][32], signature[64], message[1] = { 0x72 };
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", key[0]);
    unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", key[1]);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
          "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
          signature);
    CHECK(nw_updater_signed(key, 1, "", 0, signature, 64));
    CHECK(!nw_updater_signed(key + 1, 1, "", 0, signature, 64));
    unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
          "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
          signature);
    CHECK(nw_updater_signed(key, 2, message, 1, signature, 64));
    CHECK(!nw_updater_signed(key, 2, message, 0, signature, 64));
    CHECK(!nw_updater_signed(key, 2, message, 1, signature, 63));

    unhex("ee13c154f2530f87e71143659d8939ef9897c0647aaf8f7a48242fa5e72d6dfb"
          "0aafced0282432f0ea50abc7f09921b0af895bac5a2690437955754d62d12f0f",
          signature);
    CHECK(nw_updater_signed(nw_updater_keys, NW_UPDATER_KEYS, "test message\n", 13, signature, 64));
    CHECK(!nw_updater_signed(nw_updater_keys + 1, 1, "test message\n", 13, signature, 64));
    CHECK(!nw_updater_signed(nw_updater_keys, NW_UPDATER_KEYS, "test massage\n", 13, signature, 64));
}

static void test_sums(void)
{
    static const char sums[] =
        "09555DBF29a62725e6a932051d22773ca4d9562ef4012e15c11202ddf257dba7  a-symbols.zip\r\n"
        "0b5bdc435b5b4b01ad3c5dc0d81dfddc3d1d425c21e38997190882168f1ddf34 *a.zip\n"
        "zz5bdc435b5b4b01ad3c5dc0d81dfddc3d1d425c21e38997190882168f1ddf34  bad.zip\n"
        "short  b.zip\n";
    char hex[NW_SHA256_HEX];
    CHECK(nw_updater_listed(sums, "a.zip", hex) &&
          SDL_strcmp(hex, "0b5bdc435b5b4b01ad3c5dc0d81dfddc3d1d425c21e38997190882168f1ddf34") == 0);
    CHECK(nw_updater_listed(sums, "a-symbols.zip", hex) && SDL_strncmp(hex, "09555dbf", 8) == 0);
    CHECK(!nw_updater_listed(sums, "bad.zip", hex));
    CHECK(!nw_updater_listed(sums, "b.zip", hex));
    CHECK(!nw_updater_listed(sums, "symbols.zip", hex));
    CHECK(!nw_updater_listed("", "a.zip", hex));
}

static void test_archives(void)
{
    const char *names[] = { NW_ARCHIVE_ROOT "NuWorld.exe", NW_ARCHIVE_ROOT "sub/notes.txt" };
    const char *texts[] = { "program", "notes" };
    CHECK(unpack(zip_of(names, texts, 2), "zip/"));
    CHECK(holds("zip/", "NuWorld.exe", "program") && holds("zip/", "sub/notes.txt", "notes"));

    const char *outside[] = { NW_ARCHIVE_ROOT "../escaped.txt" };
    CHECK(!unpack(zip_of(outside, texts, 1), "zip-outside/") && !there("", "escaped.txt"));
    const char *elsewhere[] = { "Other/NuWorld.exe" };
    CHECK(!unpack(zip_of(elsewhere, texts, 1), "zip-elsewhere/") && !there("zip-elsewhere/", "NuWorld.exe"));
    Blob cut = zip_of(names, texts, 2);
    cut.size -= 30;
    CHECK(!unpack(cut, "zip-cut/"));

    Blob tar = { 0 };
    tar_entry(&tar, NW_ARCHIVE_ROOT, "", '5', 0755);
    tar_entry(&tar, "PaxHeaders/NuWorld", "30 mtime=1794991401.6854554\n", 'x', 0644);
    tar_entry(&tar, NW_ARCHIVE_ROOT "NuWorld", "program", '0', 0755);
    tar_entry(&tar, NW_ARCHIVE_ROOT "sub/notes.txt", "notes", '0', 0644);
    CHECK(unpack(gzip_of(tar), "tar/"));
    CHECK(holds("tar/", "NuWorld", "program") && holds("tar/", "sub/notes.txt", "notes"));

    static const char *const hostile[] = {
        NW_ARCHIVE_ROOT "../escaped.txt", NW_ARCHIVE_ROOT "a/../../escaped.txt", "/escaped.txt",
        NW_ARCHIVE_ROOT "c:escaped.txt",  NW_ARCHIVE_ROOT "a\\escaped.txt",      NW_ARCHIVE_ROOT "./escaped.txt",
    };
    for (size_t i = 0; i < SDL_arraysize(hostile); i++) {
        Blob bad = { 0 };
        tar_entry(&bad, hostile[i], "x", '0', 0644);
        CHECK(!unpack(gzip_of(bad), "tar-hostile/") && !there("", "escaped.txt"));
    }
    Blob link = { 0 }, renamed = { 0 }, shortened = { 0 };
    tar_entry(&link, NW_ARCHIVE_ROOT "link", "", '2', 0777);
    CHECK(!unpack(gzip_of(link), "tar-link/"));
    tar_entry(&renamed, "PaxHeaders/x", "22 path=../escaped.txt\n", 'x', 0644);
    tar_entry(&renamed, NW_ARCHIVE_ROOT "x", "x", '0', 0644);
    CHECK(!unpack(gzip_of(renamed), "tar-renamed/"));
    tar_entry(&shortened, NW_ARCHIVE_ROOT "NuWorld", "program", '0', 0755);
    shortened = gzip_of(shortened);
    shortened.size -= 12;
    CHECK(!unpack(shortened, "tar-short/"));
    CHECK(!unpack((Blob){ (uint8_t *)SDL_strdup("plain text"), 10 }, "plain/"));
}

static void test_swap(void)
{
    char *from = path("swap-new/", ""), *install = path("swap-install/", ""), *replaced = NULL;
    put("swap-install/", "NuWorld.exe", "old program", 11);
    put("swap-install/", "portable.txt", "", 0);
    put("swap-install/", "SDL3.dll", "library", 7);
    put("swap-new/", "NuWorld.exe", "new program", 11);
    put("swap-new/", "SDL3.dll", "library", 7);
    put("swap-new/", "sub/notes.txt", "notes", 5);
    CHECK(nw_updater_swap(from, install, &replaced));
    CHECK(replaced && SDL_strcmp(replaced, "NuWorld.exe\n") == 0);
    CHECK(holds("swap-install/", "SDL3.dll", "library") && !there("swap-install/", "SDL3.dll.old"));
    CHECK(holds("swap-install/", "NuWorld.exe", "new program"));
    CHECK(holds("swap-install/", "NuWorld.exe.old", "old program"));
    CHECK(holds("swap-install/", "sub/notes.txt", "notes") && there("swap-install/", "portable.txt"));
    CHECK(!there("swap-new/", "NuWorld.exe"));
    SDL_free(replaced);
    SDL_free(from);
    SDL_free(install);

    from = path("back-new/", "");
    install = path("back-install/", "");
    put("back-install/", "NuWorld.exe", "old program", 11);
    put("back-install/", "blocked", "a file where the update wants a folder", 38);
    put("back-install/", "SDL3.dll", "library", 7);
    put("back-new/", "NuWorld.exe", "new program", 11);
    put("back-new/", "SDL3.dll", "library", 7);
    put("back-new/", "blocked/inside.txt", "x", 1);
    CHECK(!nw_updater_swap(from, install, NULL));
    CHECK(holds("back-install/", "NuWorld.exe", "old program") && !there("back-install/", "NuWorld.exe.old"));
    CHECK(holds("back-install/", "SDL3.dll", "library"));
    CHECK(holds("back-new/", "NuWorld.exe", "new program"));
    SDL_free(from);
    SDL_free(install);
}

static bool release_fetch(void *user, const char *name, const char *to, uint64_t max, NwHttpProgress progress,
                          void *progress_user)
{
    Release *release = user;
    char *from = path(release->dir, name);
    size_t size = 0;
    void *data = SDL_LoadFile(from, &size);
    bool ok = data && size <= max && SDL_SaveFile(to, data, size) && (!progress || progress(progress_user, size, size));
    release->fetched++;
    SDL_free(data);
    SDL_free(from);
    return ok || SDL_SetError("no %s in the test release", name);
}

static void release_make(const char *dir, const char *version, const char *program, const uint8_t secret[64])
{
    char name[96], sums[200], hex[NW_SHA256_HEX];
    uint8_t digest[NW_SHA256_SIZE], signature[64];
    Blob archive;
#ifdef _WIN32
    const char *names[] = { NW_ARCHIVE_ROOT NW_UPDATER_PROGRAM, NW_ARCHIVE_ROOT "README.txt" };
    const char *texts[] = { program, "new readme" };
    archive = zip_of(names, texts, 2);
#else
    Blob tar = { 0 };
    tar_entry(&tar, NW_ARCHIVE_ROOT NW_UPDATER_PROGRAM, program, '0', 0755);
    tar_entry(&tar, NW_ARCHIVE_ROOT "README.txt", "new readme", '0', 0644);
    archive = gzip_of(tar);
#endif
    SDL_snprintf(name, sizeof(name), "CrashWOC-NuWorld-v%s-" NW_UPDATER_ARCHIVE, version);
    nw_sha256(archive.data, archive.size, digest);
    nw_sha256_hex(digest, hex);
    SDL_snprintf(sums, sizeof(sums), "%s  %s\n", hex, name);
    crypto_ed25519_sign(signature, secret, (const uint8_t *)sums, SDL_strlen(sums));
    put(dir, name, archive.data, archive.size);
    put(dir, "SHA256SUMS", sums, SDL_strlen(sums));
    put(dir, "SHA256SUMS.sig", signature, sizeof(signature));
    SDL_free(archive.data);
}

static void test_update(void)
{
    uint8_t seed[32] = { 7 }, secret[64], key[1][32], other[1][32] = { { 1 } };
    char *install = path("install/", "");
    Release release = { "release/", 0 };
    NwUpdaterConfig config = { .install = install, .from = "9.9.8", .to = "v9.9.9", .fetch = release_fetch,
                               .fetch_user = &release, .keys = key, .key_count = 1 };
    crypto_ed25519_key_pair(secret, key[0], seed);
    release_make("release/", "9.9.9", "new program", secret);
    put("install/", NW_UPDATER_PROGRAM, "old program", 11);
    put("install/", "README.txt", "old readme", 10);
    put("install/", "portable.txt", "", 0);

    config.to = "9.9.7";
    CHECK(!nw_updater_apply(&config) && release.fetched == 0);
    config.to = "v9.9.9";
    config.keys = other;
    CHECK(!nw_updater_apply(&config) && SDL_strstr(SDL_GetError(), "not signed"));
    CHECK(holds("install/", NW_UPDATER_PROGRAM, "old program") && !there("install/", NW_UPDATE_STAGE));
    config.keys = key;

    char *archive = NULL;
    SDL_asprintf(&archive, "CrashWOC-NuWorld-v9.9.9-" NW_UPDATER_ARCHIVE);
    put("release/", archive, "tampered", 8);
    CHECK(!nw_updater_apply(&config) && SDL_strstr(SDL_GetError(), "does not match"));
    CHECK(holds("install/", NW_UPDATER_PROGRAM, "old program") && !there("install/", NW_UPDATE_STAGE));
    SDL_free(archive);

    release_make("release/", "9.9.9", "new program", secret);
    CHECK(nw_updater_apply(&config));
    CHECK(holds("install/", NW_UPDATER_PROGRAM, "new program") && holds("install/", "README.txt", "new readme"));
    CHECK(holds("install/", NW_UPDATER_PROGRAM NW_UPDATE_OLD, "old program") && there("install/", "portable.txt"));
    CHECK(there("install/", NW_UPDATE_STAGE "/" NW_UPDATE_JOURNAL) && !there("install/", NW_UPDATE_STAGE "/new"));

    nw_update_cleanup(install);
    CHECK(!there("install/", NW_UPDATER_PROGRAM NW_UPDATE_OLD) && !there("install/", "README.txt" NW_UPDATE_OLD));
    CHECK(!there("install/", NW_UPDATE_STAGE) && holds("install/", NW_UPDATER_PROGRAM, "new program"));
    SDL_free(install);
}

int main(int argc, char **argv)
{
    char *base = NULL;
    if (argc != 2 || SDL_asprintf(&base, "%s/", argv[1]) < 0) {
        fprintf(stderr, "usage: test_updater <empty folder>\n");
        return 2;
    }
    nw_paths_remove_tree(argv[1]);
    SDL_CreateDirectory(argv[1]);
    root = base;
    test_signatures();
    test_sums();
    test_archives();
    test_swap();
    test_update();
    SDL_free(base);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("updater: ok\n");
    return 0;
}
