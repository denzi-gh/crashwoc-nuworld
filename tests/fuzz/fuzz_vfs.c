/* libFuzzer target for the VFS readers (no game data). The first byte picks the reader, the rest is its input:
     0  a disc image (.iso or raw .bin, detected): nw_disc_open, nw_iso_index, nw_verify_identity, every file
        looked up again and read at both ends;
     1  a cue sheet: u16 LE length, the sheet, then the image (fuzz.bin, also the cue's own name with .bin);
     2  a .DAT archive opened through the VFS, listed, every entry found by its path and loaded.
   The readers open files, so each input is written to a scratch folder under $TMPDIR first.
   Built with -DNUWORLD_FUZZ=ON; nuworld_fuzz_seeds writes the seed corpus from the synthetic test builders. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "platform/paths.h"
#include "vfs/dat.h"
#include "vfs/disc.h"
#include "vfs/extract.h"
#include "vfs/iso9660.h"
#include "vfs/manifest.h"
#include "vfs/verify.h"
#include "vfs/vfs.h"

enum { FUZZ_DISC, FUZZ_CUE, FUZZ_DAT, FUZZ_READERS };

static char *scratch;  /* <tmp>/nuworld-fuzz-<pid>/ */
static char *bin_path; /* the image, of FUZZ_DISC and FUZZ_CUE alike */
static char *cue_path;
static char *dat_path; /* <scratch>disc/FUZZ.DAT, mounted as cdrom0:\FUZZ.DAT */
static NwVfs vfs;

static void fuzz_die(const char *what)
{
    SDL_Log("fuzz_vfs: %s: %s", what, SDL_GetError());
    abort();
}

static char *fuzz_path(const char *name)
{
    char *path = NULL;
    if (SDL_asprintf(&path, "%s%s", scratch, name) < 0) {
        fuzz_die("asprintf");
    }
    return path;
}

static void fuzz_save(const char *path, const void *data, size_t size)
{
    if (!SDL_SaveFile(path, size ? data : "", size)) {
        fuzz_die(path);
    }
}

static void fuzz_cleanup(void)
{
    nw_vfs_shutdown(&vfs);
    nw_paths_remove_tree(scratch);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    const char *tmp = getenv("TMPDIR");
    if (SDL_asprintf(&scratch, "%s/nuworld-fuzz-%d/", tmp && *tmp ? tmp : "/tmp", (int)getpid()) < 0) {
        fuzz_die("asprintf");
    }
    char *disc_dir = fuzz_path(NW_DISC_DIR);
    char *stamp = fuzz_path(NW_DISC_DIR "/" NW_DISC_STAMP);
    bin_path = fuzz_path("fuzz.bin");
    cue_path = fuzz_path("fuzz.cue");
    dat_path = fuzz_path(NW_DISC_DIR "/FUZZ.DAT");
    if (!nw_paths_remove_tree(scratch) || !SDL_CreateDirectory(disc_dir)) {
        fuzz_die(scratch);
    }
    fuzz_save(stamp, NULL, 0);
    fuzz_save(dat_path, NULL, 0);
    if (!nw_vfs_init(&vfs, scratch)) {
        fuzz_die("nw_vfs_init");
    }
    SDL_free(stamp);
    SDL_free(disc_dir);
    atexit(fuzz_cleanup);
    return 0;
}

static void fuzz_disc(const char *path)
{
    NwDisc disc;
    NwIso iso;
    uint8_t buf[2 * NW_DISC_SECTOR + 1]; /* a tail read crosses a sector boundary */

    if (!nw_disc_open(&disc, path)) {
        return;
    }
    if (nw_iso_index(&iso, &disc)) {
        for (uint32_t i = 0; i < iso.count; i++) {
            const NwIsoFile *f = &iso.files[i];
            if (nw_iso_find(&iso, f->path) != f) { /* paths are unique and already in lookup form */
                SDL_Log("fuzz_vfs: nw_iso_find(\"%s\") misses its own entry", f->path);
                abort();
            }
            size_t n = f->size < sizeof(buf) ? f->size : sizeof(buf);
            nw_iso_read(&disc, f, 0, buf, n);
            nw_iso_read(&disc, f, f->size - n, buf, n);
        }
        nw_verify_identity(&disc, &iso, &nw_disc_manifest);
        nw_iso_free(&iso);
    }
    nw_disc_close(&disc);
}

static void fuzz_cue(const uint8_t *data, size_t size)
{
    if (size < 2) {
        return;
    }
    size_t len = data[0] | (size_t)data[1] << 8;
    data += 2;
    size -= 2;
    if (len > size) {
        return;
    }
    /* FILE stays inside the scratch folder: no separators or drive letters, nothing else on the host is opened. */
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '/' || data[i] == '\\' || data[i] == ':') {
            return;
        }
    }
    fuzz_save(cue_path, data, len);
    fuzz_save(bin_path, data + len, size - len);
    fuzz_disc(cue_path);
}

static void dat_listed(void *user, int index, const char *path)
{
    NwDat *dat = user;
    nw_dat_find(dat, path); /* duplicate names may find another entry: no round-trip check */
    SDL_free(nw_dat_load(dat, index));
}

static void fuzz_dat(const uint8_t *data, size_t size)
{
    NwDat dat;
    fuzz_save(dat_path, data, size);
    if (nw_dat_open(&dat, &vfs, "cdrom0:\\FUZZ.DAT;1")) {
        nw_dat_list(&dat, dat_listed, &dat);
        nw_dat_find(&dat, "arc\\a\\x.bin");
        nw_dat_close(&dat);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return 0;
    }
    switch (data[0] % FUZZ_READERS) {
    case FUZZ_DISC:
        fuzz_save(bin_path, data + 1, size - 1);
        fuzz_disc(bin_path);
        break;
    case FUZZ_CUE:
        fuzz_cue(data + 1, size - 1);
        break;
    case FUZZ_DAT:
        fuzz_dat(data + 1, size - 1);
        break;
    }
    return 0;
}
