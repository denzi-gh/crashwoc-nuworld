/* Seed corpus for nuworld_fuzz_vfs from the synthetic test builders (no game data): the disc as .iso, raw
   Mode 2 / Mode 1 .bin and refused variants, cue sheets with their images, and .DAT archives. Each seed is the
   reader byte of fuzz_vfs.c and its input.
   Usage: fuzz_seeds <corpus dir>. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "dat_builder.h"
#include "disc_builder.h"
#include "rnc2_fixture.inc"

enum { FUZZ_DISC, FUZZ_CUE, FUZZ_DAT };

static const char *corpus;

/* <corpus>/<name>: reader, head, then body. */
static void seed(const char *name, uint8_t reader, const void *head, size_t head_size, const void *body,
                 size_t body_size)
{
    char path[1024];
    size_t size = 1 + head_size + body_size;
    uint8_t *data = malloc(size);
    data[0] = reader;
    memcpy(data + 1, head, head_size);
    memcpy(data + 1 + head_size, body, body_size);
    SDL_snprintf(path, sizeof(path), "%s/%s", corpus, name);
    if (!SDL_SaveFile(path, data, size)) {
        fprintf(stderr, "cannot write %s: %s\n", path, SDL_GetError());
        exit(1);
    }
    free(data);
}

static void seed_cue(const char *name, const char *cue, const void *image, size_t image_size)
{
    uint8_t head[2 + 256];
    size_t len = strlen(cue);
    head[0] = (uint8_t)len;
    head[1] = (uint8_t)(len >> 8);
    memcpy(head + 2, cue, len);
    seed(name, FUZZ_CUE, head, 2 + len, image, image_size);
}

static void seed_spec(const char *name, Image *img, const Spec *spec)
{
    build_image(img, spec);
    seed(name, FUZZ_DISC, NULL, 0, img->data, img->size);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: fuzz_seeds <corpus dir>\n");
        return 2;
    }
    corpus = argv[1];
    if (!SDL_CreateDirectory(corpus)) {
        fprintf(stderr, "%s: %s\n", corpus, SDL_GetError());
        return 1;
    }

    Image *img = malloc(sizeof(*img));
    Spec spec = { pal_cnf, 0, 0, 0, false };
    size_t mode2_size, mode1_size, form2_size;

    seed_spec("disc-multi-extent", img, &(Spec){ pal_cnf, 0, 0x80, 0, false });
    seed_spec("disc-interleaved", img, &(Spec){ pal_cnf, 0, 0, 1, false });
    seed_spec("disc-duplicate", img, &(Spec){ pal_cnf, 0, 0, 0, true });
    seed_spec("disc-ps1", img, &(Spec){ "BOOT = cdrom:\\SCES_000.01;1\r\nTCB = 4\r\n", 0, 0, 0, false });
    seed_spec("disc-iso", img, &spec);
    seed("disc-truncated", FUZZ_DISC, NULL, 0, img->data, (LSN_GSC + 1) * NW_DISC_SECTOR);
    uint8_t *mode2 = to_raw(img, 2, UINT32_MAX, &mode2_size);
    uint8_t *mode1 = to_raw(img, 1, UINT32_MAX, &mode1_size);
    uint8_t *form2 = to_raw(img, 2, LSN_GSC + 1, &form2_size);
    seed("disc-mode2", FUZZ_DISC, NULL, 0, mode2, mode2_size);
    seed("disc-mode1", FUZZ_DISC, NULL, 0, mode1, mode1_size);
    seed("disc-form2", FUZZ_DISC, NULL, 0, form2, form2_size);

    seed_cue("cue-mode2", "FILE \"fuzz.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n", mode2,
             mode2_size);
    seed_cue("cue-mode1", "\xef\xbb\xbf" "file fuzz.bin binary\n track 01 mode1/2352\n  index 01 00:00:00\n", mode1,
             mode1_size);
    seed_cue("cue-cooked", "FILE \"fuzz.bin\" BINARY\n  TRACK 01 MODE1/2048\n", img->data, img->size);
    seed_cue("cue-fallback", "FILE \"renamed.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n", mode2, mode2_size);
    seed_cue("cue-audio", "FILE \"fuzz.bin\" BINARY\n  TRACK 01 AUDIO\n", NULL, 0);
    seed_cue("cue-track2", "FILE \"fuzz.bin\" BINARY\n  TRACK 02 MODE2/2352\n", NULL, 0);
    free(form2);
    free(mode1);
    free(mode2);
    free(img);

    DatBuild d;
    build_dat(&d);
    seed("dat", FUZZ_DAT, NULL, 0, d.data, d.size);
    build_dat_with(&d, nw_rnc2_fixture, sizeof(nw_rnc2_fixture), NW_RNC2_UNPACKED);
    seed("dat-packed", FUZZ_DAT, NULL, 0, d.data, d.size);
    return 0;
}
