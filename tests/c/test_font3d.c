/* game/font3d's Text3D: glyph specials, colour codes, button prompts and the power-up texts' shoulder buttons;
   `retail` pins STUFF\FONT.GSC's glyphs. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "data/image.h"
#include "gfx/packet.h"
#include "gfx/rndr.h"
#include "game/font3d.h"
#include "game/gametypes.h"
#include "nu3d/nucamera.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu3d/nuvport.h"
#include "nucore/nufile.h"
#include "numath/nutrig.h"
#include "port/font3d.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/vfs.h"

#include "gsc_builder.h"

extern int no_default_font, PS2_REZ_W, PS2_REZ_H, PS2_VCNTR_X, PS2_VCNTR_Y;
extern int nugscn_mtlupdate_enabled;
extern struct numtx_s numtx_identity;
extern struct font3dtab_s Font3DTab[];
extern struct font3djchar_s Font3DAccentTab[];
extern signed char Font3DRemap[];
extern struct numtl_s *Font3DMtlTab[5][2];
extern struct nugscn_s *font3d_scene;
extern struct numtl_s **nurndr_forced_mtl_table;
extern float FONT3DSIZE, FONT3DYMUL, D_0062E358, PANEL3DMULX, PANEL3DMULY;
extern float font3d_xleft, font3d_xright;
extern struct powerdata_s PData[6];

/* Declared by no header. */
void NuRndrInitWorld(void);
void NuRndrEndScene(void);
void NuCameraSet(struct nucamera_s *cam);
void InitFont3D(struct nugscn_s *gscn);
void Text3D(char *txt, float x, float y, float z, float scalex, float scaley, float scalez, int align, int colour);
void NewLanguage(int lang);

static int failures;
static char scene_buffer[1 << 20];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static void render_init(void)
{
    PS2_REZ_W = 640;
    PS2_REZ_H = 512;
    PS2_VCNTR_X = 0x800;
    PS2_VCNTR_Y = 0x800;
    no_default_font = 1;
    NuTexInit();
    NuMtlInit();
    NuTrigInit();
    NuRndrInitEx(0x10000);
    nugscn_mtlupdate_enabled = 1;
}

/* A camera at z -20 looking down +z, which sees the panel's glyphs around the origin. */
static void camera_init(void)
{
    memset(&numtx_identity, 0, sizeof(numtx_identity));
    numtx_identity._00 = numtx_identity._11 = numtx_identity._22 = numtx_identity._33 = 1.0f;
    NuVpInit();
    struct nucamera_s *cam = NuCameraCreate();
    cam->mtx = numtx_identity;
    cam->mtx._32 = -20.0f;
    NuCameraSet(cam);
}

static void test_synthetic(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    GscBuild b;
    GscScene s;
    NwVfs vfs;
    SDL_asprintf(&root, "%sfont3d/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    gsc_build_scene(&b, &s);
    SDL_asprintf(&path, "%sTEST.GSC", disc);
    CHECK(SDL_SaveFile(path, b.buf, b.len));
    SDL_free(path);
    gsc_free(&b);
    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    render_init();
    camera_init();

    char *buf = scene_buffer;
    NuRndrInitWorld();
    struct nugscn_s *sc = NuGScnRead(&buf, scene_buffer + sizeof(scene_buffer), "test.gsc");
    CHECK(sc != NULL);
    if (!sc) {
        return;
    }
    /* Object 0 (the special's) draws material 0, its next material 2, and material 2. */
    sc->mtls[0]->special_id = 1;
    sc->mtls[1]->special_id = 3;
    Font3DTab[0] = (struct font3dtab_s){ 'A', { NULL, NULL }, "special" };
    Font3DTab[1].ascii = 0;
    for (int i = 0; i < 7; i++) {
        Font3DAccentTab[i].name = "none";
    }
    InitFont3D(sc);
    CHECK(Font3DRemap['A'] == 0 && Font3DRemap['B'] == -1 && Font3DTab[0].obj.special == &sc->specials[0]);
    CHECK(Font3DMtlTab[0][1] == sc->mtls[0] && Font3DMtlTab[2][1] == sc->mtls[1] && !Font3DMtlTab[1][1]);

    font3d_scene = sc;
    FONT3DSIZE = 0.07f;
    FONT3DYMUL = 2.0f;
    D_0062E358 = 0.1f;
    PANEL3DMULX = 2.0f;
    PANEL3DMULY = 3.0f;
    /* Two letters 0.1 apart, centred on x 0; #c switches to colour 2 for the second. */
    Text3D("A#cA", 0.0f, 1.0f, 5.0f, 1.0f, 1.0f, 1.0f, 0, 0);
    CHECK(nurndr_forced_mtl_table == NULL);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    unsigned first = 0, second = 0, other = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        bool placed = SDL_fabsf(c->local[13] - 3.0f) < 1e-6f && c->local[14] == 5.0f &&
                      SDL_fabsf(c->local[0] - 0.07f) < 1e-6f && SDL_fabsf(c->local[10] - 0.28f) < 1e-6f;
        if (placed && SDL_fabsf(c->local[12] + 0.1f) < 1e-6f) {
            first++;
            CHECK(c->material == sc->mtls[0] || c->material == sc->mtls[2]);
        } else if (placed && SDL_fabsf(c->local[12] - 0.1f) < 1e-6f) {
            second++;
            CHECK(c->material == sc->mtls[1] || c->material == sc->mtls[2]);
        } else {
            other++;
        }
    }
    CHECK(first == 3 && second == 2 && other == 0);
    NuRndrSwapStreamBuffers();

    /* L1 (q) handed as two letters, cross (x) as an icon in two cells: four from the left, the icon a VERTS record. */
    NwPrompts prompts;
    nw_prompts_retail(&prompts);
    strcpy(prompts.glyph[4].text, "AA");
    strcpy(prompts.glyph[0].icon, "xbox_button_color_a");
    nw_font3d_prompts(&prompts);
    Text3D("qx", 0.0f, 1.0f, 5.0f, 1.0f, 1.0f, 1.0f, 2, 0);
    CHECK(SDL_fabsf(font3d_xleft) < 1e-6f && SDL_fabsf(font3d_xright - 0.4f) < 1e-6f);
    NuRndrEndScene();
    p = nw_rndr_packet();
    unsigned letters[2] = { 0, 0 }, icons = 0;
    other = 0;
    for (uint32_t i = 0; i < p->ncmds; i++) {
        const NwCmd *c = &p->cmds[i];
        if (c->op == NW_CMD_VERTS && SDL_fabsf(c->local[12] - 0.6f) < 1e-6f && c->material && c->material->tid) {
            icons++;
        } else if (c->op != NW_CMD_VERTS && SDL_fabsf(c->local[12] - 0.1f) < 1e-6f) {
            letters[0]++;
        } else if (c->op != NW_CMD_VERTS && SDL_fabsf(c->local[12] - 0.3f) < 1e-6f) {
            letters[1]++;
        } else {
            other++;
        }
    }
    CHECK(letters[0] == 3 && letters[1] == 3 && icons == 1 && other == 0);
    NuRndrSwapStreamBuffers();
    nw_prompts_retail(&prompts);
    nw_font3d_prompts(&prompts);

    /* NewLanguage: the shoulder buttons of the power-up texts are glyph codes in every language. */
    for (int lang = 0; lang < 6; lang++) {
        NewLanguage(lang);
        const struct { int power; char code; } shoulders[3] = { { 0, 'u' }, { 3, 'q' }, { 4, 'v' } };
        for (int k = 0; k < 3; k++) {
            int codes = 0;
            for (int line = 0; line < 7; line++) {
                const char *s = PData[shoulders[k].power].text[line];
                CHECK(!strstr(s, "L1") && !strstr(s, "L2") && !strstr(s, "R2"));
                codes += strchr(s, shoulders[k].code) != NULL;
            }
            CHECK(codes == 1);
        }
    }
    NewLanguage(0);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    SDL_free(disc);
    SDL_free(root);
}

static NwVfs retail_vfs;
static unsigned retail_glyphs, retail_found, retail_accents, retail_colours, retail_draws, retail_places;
static int retail_bad;

/* On the game thread, as LoadFont3D: the scene, InitFont3D, then "NEW GAME" as the title menu draws it. */
static int retail_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    NuFileInitEx(1, 1);
    render_init();
    camera_init();
    char *buf = scene_buffer;
    NuRndrInitWorld();
    struct nugscn_s *sc = NuGScnRead(&buf, scene_buffer + sizeof(scene_buffer), "stuff\\font.gsc");
    if (!sc) {
        fprintf(stderr, "retail: stuff\\font.gsc does not load\n");
        retail_bad++;
        return 0;
    }
    InitFont3D(sc);
    for (int i = 0; Font3DTab[i].ascii != 0; i++) {
        retail_glyphs++;
        retail_found += Font3DTab[i].obj.special != NULL;
        /* font.gsc has no "exclamation": '!' draws nothing on retail either. */
        if (!Font3DTab[i].obj.special && strcmp(Font3DTab[i].name, "exclamation") != 0) {
            fprintf(stderr, "retail: glyph %s is not in font.gsc\n", Font3DTab[i].name);
            retail_bad++;
        }
        if (Font3DTab[i].obj.special && Font3DRemap[Font3DTab[i].ascii] != i) {
            fprintf(stderr, "retail: glyph %s does not remap to its entry\n", Font3DTab[i].name);
            retail_bad++;
        }
    }
    for (int i = 0; i < 7; i++) {
        retail_accents += Font3DAccentTab[i].obj.special != NULL;
    }
    for (int i = 0; i < 5; i++) {
        retail_colours += Font3DMtlTab[i][1] != NULL;
    }
    /* Retail's '.'-style entries below the table: 0xFE and 0xF8 as signed chars. */
    if (Font3DRemap[-2] != 42 || Font3DRemap[-8] != 43 || strcmp(Font3DTab[42].name, "fullstop") != 0 ||
        strcmp(Font3DTab[43].name, "third") != 0) {
        fprintf(stderr, "retail: the 0xFE / 0xF8 glyphs do not read back\n");
        retail_bad++;
    }

    font3d_scene = sc;
    PANEL3DMULX = 1.0f;
    PANEL3DMULY = 1.0f;
    Text3D("NEW GAME", 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0, 2);
    NuRndrEndScene();
    const NwPacket *p = nw_rndr_packet();
    float x[16];
    for (uint32_t i = 0; i < p->ncmds; i++) {
        unsigned k = 0;
        while (k < retail_places && x[k] != p->cmds[i].local[12]) {
            k++;
        }
        if (k == retail_places && retail_places < SDL_arraysize(x)) {
            x[retail_places++] = p->cmds[i].local[12];
        }
    }
    retail_draws = p->ncmds;
    NuRndrSwapStreamBuffers();
    return 0;
}

static int test_retail(void)
{
    const char *dir = getenv("NUWORLD_TEST_DATA");
    if (!dir) {
        printf("NUWORLD_TEST_DATA is not set: skipped\n");
        return 77;
    }
    char path[1024], err[512];
    size_t size = 0;
    NwImageStats stats;
    SDL_snprintf(path, sizeof(path), "%s/SLES_503.86", dir);
    void *elf = SDL_LoadFile(path, &size);
    if (!elf || !nw_vfs_mount(&retail_vfs, NW_VFS_DISC, dir)) {
        fprintf(stderr, "NUWORLD_TEST_DATA: %s\n", SDL_GetError());
        return 1;
    }
    nw_shim_file_init(&retail_vfs);
    nw_newlib_init();
    bool loaded = nw_image_load(&nw_image_tables, elf, size, &stats, err, sizeof(err));
    SDL_free(elf);
    if (!loaded) {
        fprintf(stderr, "data image: %s\n", err);
        return 1;
    }
    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(retail_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, "retail: %s\n", nw_game_error());
    }
    printf("font.gsc: %u of %u glyphs, %u accents, %u colours; NEW GAME: %u draws at %u places\n", retail_found,
           retail_glyphs, retail_accents, retail_colours, retail_draws, retail_places);
    CHECK(state == NW_GAME_RETURNED && retail_bad == 0);
    CHECK(retail_glyphs == 59 && retail_found == 58 && retail_accents == 7 && retail_colours == 5);
    CHECK(retail_draws == 14 && retail_places == 7);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_font3d <scratch dir> | retail\n");
        return 2;
    }
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_synthetic(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "font3d: all checks passed");
    return failures ? 1 : 0;
}
