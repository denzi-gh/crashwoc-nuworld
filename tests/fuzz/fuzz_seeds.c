/* Seed corpus for nuworld_fuzz_vfs from the synthetic test builders (no game data): the disc as .iso, raw
   Mode 2 / Mode 1 .bin and refused variants, cue sheets with their images, .DAT archives, .gsc geoms, save documents
   and a PS2 memory card. Each seed is the reader byte of fuzz_vfs.c and its input. With a second folder, the seeds
   of fuzz_loaders.c there: a scene, a character, a cutscene, an anim, a shadow set, a terrain, a .vag, settings.
   Usage: fuzz_seeds <corpus dir> [<loaders corpus dir>]. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "ani_builder.h"
#include "cut_builder.h"
#include "dat_builder.h"
#include "disc_builder.h"
#include "ghg_builder.h"
#include "mc_builder.h"
#include "rnc2_fixture.inc"
#include "save/savecodec.h"
#include "save/savestore.h"
#include "shd_builder.h"
#include "ter_builder.h"
#include "vag_builder.h"
#include "vif_builder.h"

enum { FUZZ_DISC, FUZZ_CUE, FUZZ_DAT, FUZZ_VIF, FUZZ_SAVE, FUZZ_CARD };
/* fuzz_loaders.c's */
enum { LOAD_GSC, LOAD_GHG, LOAD_CUT, LOAD_ANI, LOAD_SHD, LOAD_TER, LOAD_VIEWER, LOAD_VAG = 11, LOAD_SETTINGS };

static const char *corpus;

/* <corpus>/<name>: reader, head, then body. */
static void seed(const char *name, uint8_t reader, const void *head, size_t head_size, const void *body,
                 size_t body_size)
{
    char path[1024];
    size_t size = 1 + head_size + body_size;
    uint8_t *data = malloc(size);
    data[0] = reader;
    if (head_size) {
        memcpy(data + 1, head, head_size);
    }
    if (body_size) {
        memcpy(data + 1 + head_size, body, body_size);
    }
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

static int loader_seeds(const char *dir)
{
    GscBuild b;
    corpus = dir;
    if (!SDL_CreateDirectory(corpus)) {
        fprintf(stderr, "%s: %s\n", corpus, SDL_GetError());
        return 1;
    }
    GscScene scene;
    gsc_build_scene(&b, &scene);
    seed("scene", LOAD_GSC, NULL, 0, b.buf, b.len);
    seed("viewer", LOAD_VIEWER, NULL, 0, b.buf, b.len);
    gsc_free(&b);
    GhgFile ghg;
    ghg_build(&b, &ghg);
    seed("character", LOAD_GHG, NULL, 0, b.buf, b.len);
    gsc_free(&b);
    CutFile cut;
    cut_build(&b, &cut);
    seed("cutscene", LOAD_CUT, NULL, 0, b.buf, b.len);
    gsc_free(&b);
    AniFile ani;
    ani_build(&b, &ani);
    seed("anim", LOAD_ANI, NULL, 0, b.buf, b.len);
    gsc_free(&b);
    ShdFile shd;
    shd_build(&b, &shd);
    seed("shadows", LOAD_SHD, NULL, 0, b.buf, b.len);
    gsc_free(&b);
    TerFile ter;
    ter_build(&b, &ter);
    seed("terrain", LOAD_TER, NULL, 0, b.buf, b.len);
    gsc_free(&b);
    static uint8_t blocks[4 * NW_ADPCM_BLOCK], vag[48 + sizeof(blocks)];
    for (int i = 0; i < 4; i++) {
        vag_block_fill(blocks + i * NW_ADPCM_BLOCK, i % 5, 3, i == 3 ? NW_ADPCM_END : 0, i - 2);
    }
    seed("vag", LOAD_VAG, NULL, 0, vag, vag_file(vag, 22050, blocks, 4));
    static const char settings[] = "[game]\nhardware_speed = true\n[audio]\nmaster_volume = 80\nvoice_volume = 0\n";
    seed("settings", LOAD_SETTINGS, NULL, 0, settings, sizeof(settings) - 1);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: fuzz_seeds <corpus dir> [<loaders corpus dir>]\n");
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

    VifVtx v[6];
    const bool adc[6] = { 1, 1, 0, 0, 1, 1 };
    const float span[3] = { 4096.0f, 4096.0f, 4096.0f }, mid[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 6; i++) {
        v[i] = (VifVtx){ .x = (float)i, .y = (float)(i & 1), .z = 0.5f, .nx = 1.0f, .u = 0.25f * i, .v = 0.5f,
                         .nz = 1.0f, .rgba = { 0x80, 0x80, 0x80, 0x7F } };
    }
    VifBuild b;
    vif_begin(&b);
    vif_batch(&b, v, adc, 6, 0);
    vif_cnt(&b);
    vif_batch(&b, v, adc, 6, VIF_SPLIT_NEXT | VIF_FAST_UV);
    vif_end(&b, 64);
    seed("vif-chain", FUZZ_VIF, NULL, 0, b.buf, b.len);
    vif_free(&b);
    vif_begin(&b);
    vif_packed(&b, span, mid);
    vif_batch(&b, v, adc, 6, VIF_UV_ROW);
    vif_end(&b, 0);
    seed("vif-packed", FUZZ_VIF, NULL, 0, b.buf, b.len);
    vif_free(&b);
    vif_begin(&b);
    vif_faceon(&b, 3);
    vif_end(&b, 0);
    seed("vif-faceon", FUZZ_VIF, NULL, 0, b.buf, b.len);
    vif_free(&b);

    /* a save as the codec writes it (level keys "#0".., as fuzz_vfs.c's registry has them), one written by hand, and
       a card that holds the first */
    static uint8_t games[NW_SAVE_RECORDS * NW_SAVE_GAME_SIZE];
    for (size_t i = 0; i < sizeof(games); i++) {
        games[i] = (uint8_t)(i * 7 + (i >> 5));
    }
    NwSaveRegistry reg;
    NwDoc doc;
    size_t size = 0;
    char *text = NULL;
    if (!nw_save_registry_init(&reg, NULL, NW_SAVE_LEVELS, NW_SAVE_HUBS) || !nw_doc_init(&doc) ||
        !nw_save_merge(&doc, &reg, games, NW_SAVE_RECORDS) || !(text = nw_doc_write(&doc, "seed", &size))) {
        fprintf(stderr, "save seed: %s\n", SDL_GetError());
        return 1;
    }
    seed("save", FUZZ_SAVE, NULL, 0, text, size);
    SDL_free(text);
    nw_doc_free(&doc);
    nw_save_registry_free(&reg);
    static const char by_hand[] = "\xEF\xBB\xBF# by hand\r\nformat = 1\r\nnote = 'kept'\r\n"
                                  "[mod.\"some.mod\"]\r\ndata = [1, -2, 0x10, [\"x\", true], [], ]\r\n"
                                  "[slot.0]\r\nname = \"CRASH\\u0000\xC3\xA9\"\r\nempty = false\r\n"
                                  "[slot.0.level.\"#3\"]\r\nflags = 0x001f\r\ntime = [[\"1ST\", 25500]]\r\n";
    seed("save-by-hand", FUZZ_SAVE, NULL, 0, by_hand, sizeof(by_hand) - 1);

    static Mcb card;
    const char *names[1] = { NW_SAVE_CARD_NAME "slot00" };
    const void *files[1] = { games };
    size_t lens[1] = { sizeof(games) };
    mcb_build(&card, 1, NW_SAVE_CARD_NAME, 1, names, files, lens);
    uint8_t *image = mcb_image(&card, false, &size);
    seed("card", FUZZ_CARD, NULL, 0, image, size);
    free(image);
    return argc == 3 ? loader_seeds(argv[2]) : 0;
}
