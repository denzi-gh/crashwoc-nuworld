/* nu3d/nutexanm under the port: a script read through the kept nufpar layer steps a material's texture, and the frame
   packet draws the new one. Usage: test_texanim <scratch dir> | retail. Local only, with your own disc: `retail`
   reads ATS.DAT's title.ats and LEVELS\B\LOGOS\TITLE.GHG of NUWORLD_TEST_DATA as the title cut does and pins the
   logo's flipbook; exits 77 without it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/packet.h"
#include "gfx/rndr.h"
#include "nu3d/nuhgobj.h"
#include "nu3d/numtl.h"
#include "nu3d/nurndr.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu3d/nutexanm.h"
#include "nucore/nufile.h"
#include "numath/nutrig.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/vfs.h"

extern int no_default_font, PS2_REZ_W, PS2_REZ_H, PS2_VCNTR_X, PS2_VCNTR_Y;

/* Declared by no header. */
void NuRndrEndScene(void);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* Rate 1: each frame shows two ticks. Signal 1 restarts the flipbook after a wait. */
static const char script[] = "Scriptname test\n"
                             "Start:\n"
                             "\tRate\t1,0\n"
                             "Main:\n"
                             "\tTex\t0\n"
                             "\tRepeat\n"
                             "\tTexAdj\t1,0,2\n"
                             "\tUntilTex = 2\n"
                             "\tEnd\n"
                             "On 1\n"
                             "\tWait 3,0\n"
                             "\tGoto Main\n";

/* The texture the frame packet gives the material's draw. */
static const NwGpuTex *drawn_texture(struct numtl_s *mtl)
{
    nw_rndr_cmd(&mtl->rs, NW_CMD_DRAW)->mesh = (const NwGpuMesh *)(uintptr_t)1;
    NuRndrEndScene();
    NuRndrVu1Draw();
    const NwPacket *p = nw_packet_take();
    const NwGpuTex *tex = p && p->ncmds == 1 && p->cmds[0].mtl < p->nmtls ? p->mtls[p->cmds[0].mtl].tex : NULL;
    NuRndrSwapStreamBuffers();
    return tex;
}

static void test_synthetic(const char *scratch)
{
    char *root = NULL, *disc = NULL, *path = NULL;
    NwVfs vfs;
    SDL_asprintf(&root, "%stexanim/", scratch);
    SDL_asprintf(&disc, "%s" NW_DISC_DIR "/", root);
    SDL_RemovePath(root);
    CHECK(SDL_CreateDirectory(disc));
    SDL_asprintf(&path, "%s" NW_DISC_STAMP, disc);
    CHECK(SDL_SaveFile(path, "", 0));
    SDL_free(path);
    SDL_asprintf(&path, "%sTEST.ATS", disc);
    CHECK(SDL_SaveFile(path, script, sizeof(script) - 1));
    SDL_free(path);
    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);

    static uint8_t bits[8 * 8 * 4];
    short tids[3];
    for (int i = 0; i < 3; i++) {
        memset(bits, 0x40 * (i + 1), sizeof(bits));
        struct nutex_s spec = { 3, 8, 8, 0, bits, NULL };
        tids[i] = (short)NuTexCreate(&spec);
        CHECK(tids[i] > 0 && nw_rndr_texture(tids[i]) != NULL);
    }
    struct numtl_s *mtl = NuMtlCreate(1);
    mtl->tid = tids[0];
    NuMtlUpdate(mtl);

    NuTexAnimProgSysInit();
    struct nutexanimprog_s *prog = NuTexAnimProgReadScript(NULL, "test.ats");
    CHECK(prog != NULL && NuTexAnimProgFind("TEST") == prog);
    struct nutexanim_s *anim = NuTexAnimCreate(NULL, prog, mtl, tids, 3);
    NuTexAnimAddList(anim);

    /* Ticks 1-7 run Start to End, signal 1 from tick 8 waits 3 and starts Main again. */
    static const int want[] = { 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1 };
    for (size_t t = 0; t < SDL_arraysize(want); t++) {
        NuTexAnimSetSignals(t >= 7 ? 2 : 0);
        NuTexAnimProcess();
        CHECK(anim->env->tex_ix == want[t] && mtl->tid == tids[want[t]]);
        CHECK(drawn_texture(mtl) == nw_rndr_texture(tids[want[t]]));
    }
    NuTexAnimRemoveList(anim);
    NuTexAnimDestroy(anim);

    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    SDL_RemovePath(root);
    SDL_free(disc);
    SDL_free(root);
}

static NwVfs retail_vfs;
static char retail_buffer[4 << 20];
static int retail_steps[13]; /* the tick on which the logo shows each frame */
static int retail_bad;

/* On the game thread, as LoadCutMovie(0) and PlayCutMovie: title.ats from ATS.DAT, the logo, then signal 1 (the
   first boot's texanimbits) every tick. */
static int retail_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    nw_shim_file_init(&retail_vfs);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    NuTexAnimProgSysInit();
    union variptr_u buf;
    buf.charptr = retail_buffer;
    struct nudathdr_s *dat = NuDatOpen("ats.dat", NULL, NULL);
    NuDatSet(dat);
    struct nutexanimprog_s *title = NuTexAnimProgReadScript(&buf, "ats\\levels\\pal\\title.ats");
    NuDatSet(NULL);
    if (dat) {
        NuDatClose(dat);
    }
    struct NUHGOBJ_s *h = NuGHGRead(&buf, "levels\\b\\logos\\title.ghg");
    if (!title || !h || h->numtexanims != 7) {
        fprintf(stderr, "retail: no title.ats or TITLE.GHG with 7 texanims\n");
        retail_bad++;
        return 0;
    }
    struct nutexanim_s *logo = &h->texanims[0];
    if (logo->env->prog != title || logo->numtids != 13 || SDL_strcasecmp(logo->scriptname, "title") != 0) {
        fprintf(stderr, "retail: texanim 0 is not the title flipbook\n");
        retail_bad++;
    }
    for (int i = 1; i < 7; i++) {
        if (h->texanims[i].env->prog || SDL_strcasecmp(h->texanims[i].scriptname, "TitleLgt") != 0) {
            fprintf(stderr, "retail: texanim %d runs a program (titlelgt.ats is not loaded on the title)\n", i);
            retail_bad++;
        }
    }
    int shown = -1;
    for (int tick = 1; tick <= 1600; tick++) {
        NuTexAnimSetSignals(2);
        NuTexAnimProcess();
        int ix = logo->env->tex_ix;
        if (logo->mtl->tid != logo->tids[ix]) {
            retail_bad++;
        }
        if (ix != shown) {
            shown = ix;
            retail_steps[ix] = tick;
        }
    }
    return 0;
}

static int test_retail(void)
{
    const char *dir = getenv("NUWORLD_TEST_DATA");
    if (!dir) {
        printf("NUWORLD_TEST_DATA is not set: skipped\n");
        return 77;
    }
    CHECK(nw_vfs_mount(&retail_vfs, NW_VFS_DISC, dir));
    CHECK(nw_game_init(NW_CLOCK_REALTIME, 50));
    CHECK(nw_game_start(retail_main, 0, NULL));
    NwGameState state;
    while ((state = nw_game_state()) == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    if (state == NW_GAME_FATAL) {
        fprintf(stderr, "retail: %s\n", nw_game_error());
    }
    printf("title flipbook: frame 0 on tick %d, frame 1 on tick %d, frame 12 on tick %d\n", retail_steps[0],
           retail_steps[1], retail_steps[12]);
    CHECK(state == NW_GAME_RETURNED && retail_bad == 0);
    for (int i = 1; i < 13; i++) {
        CHECK(retail_steps[i] == 1457 + i);
    }
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "retail") == 0) {
        return test_retail();
    }
    if (argc != 2) {
        fprintf(stderr, "usage: test_texanim <scratch dir> | retail\n");
        return 2;
    }
    PS2_REZ_W = 640;
    PS2_REZ_H = 512;
    PS2_VCNTR_X = 0x800;
    PS2_VCNTR_Y = 0x800;
    no_default_font = 1;
    NuTexInit();
    NuMtlInit();
    NuTrigInit();
    NuRndrInitEx(0x10000);
    char *scratch = NULL;
    SDL_asprintf(&scratch, "%s/", argv[1]);
    SDL_CreateDirectory(scratch);
    test_synthetic(scratch);
    SDL_free(scratch);
    printf("%s\n", failures ? "FAILED" : "texanim: all checks passed");
    return failures ? 1 : 0;
}
