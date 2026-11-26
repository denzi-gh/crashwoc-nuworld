/* nu3d/nutexanm: a script read through nufpar steps a material's texture and the frame packet draws the new one;
   `retail` pins the title logo's flipbook. */
#include <stdint.h>
#include <math.h>
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
#include "nu3d/nutexanm.retail.h"
#include "nucore/nufile.h"
#include "port/nufpar.h"
#include "port/nutexanm.h"
#include "script/bursts.h"
#include "port/signals.h"
#include "numath/nutrig.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/vfs.h"
#include "gamelib/gcutscn.h"

/* The EE's float add, subtract and divide (hle/eefpu.c), as the game thread's code computes them. */
float nw_fpu_add(float a, float b);
float nw_fpu_sub(float a, float b);
float nw_fpu_div(float a, float b);

extern int no_default_font, PS2_REZ_W, PS2_REZ_H, PS2_VCNTR_X, PS2_VCNTR_Y;

/* Declared by no header. */
void NuRndrEndScene(void);
int NuFileOpen(char *file, enum nufilemode_e mode);
int NuFileOpenSize(int handle);
void NuFileClose(int handle);

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

static const char edge_script[] = "Scriptname edges\n"
                                  "Rate 1,0\n"
                                  "Tex 0\n"
                                  "End\n"
                                  "On 1\n"
                                  "Tex 1\n"
                                  "Wait 1,0\n"
                                  "Tex 0\n"
                                  "End\n"
                                  "Off 1\n"
                                  "Tex 2\n"
                                  "End\n"
                                  "On 2\n"
                                  "Tex 0\n"
                                  "End\n";

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

/* Two users of one program keep separate playback positions and reset independently. */
static void test_independent_envs(struct nutexanimprog_s *prog, short tids[3])
{
    struct numtl_s materials[2] = {0};
    struct nutexanimenv_s *a = NuTexAnimEnvCreate(NULL, &materials[0], tids, prog);
    struct nutexanimenv_s *b = NuTexAnimEnvCreate(NULL, &materials[1], tids, prog);
    CHECK(a != NULL && b != NULL);
    NuTexAnimSetSignals(0);
    if (a && b) {
        for (int tick = 0; tick < 3; tick++) NuTexAnimEnvProc(a);
        NuTexAnimEnvProc(b);
        CHECK(a->tex_ix == 1 && materials[0].tid == tids[1]);
        CHECK(b->tex_ix == 0 && materials[1].tid == tids[0]);
        NuTexAnimEnvReset(a);
        NuTexAnimEnvProc(a);
        CHECK(a->tex_ix == 0 && materials[0].tid == tids[0]);
        CHECK(b->tex_ix == 0 && materials[1].tid == tids[0]);
        NuTexAnimEnvProc(b);
        NuTexAnimEnvProc(b);
        CHECK(b->tex_ix == 1 && materials[1].tid == tids[1]);
        CHECK(a->tex_ix == 0 && materials[0].tid == tids[0]);
    }
    if (a) NuTexAnimEnvDestroy(a);
    if (b) NuTexAnimEnvDestroy(b);
}

/* A held signal must not restart its handler; a new on edge wins over an off edge. */
static void test_signal_edges(struct numtl_s *mtl, short tids[3])
{
    struct nutexanimprog_s *prog = NuTexAnimProgReadScript(NULL, "edges.ats");
    CHECK(prog != NULL);
    if (!prog) return;
    struct nutexanimenv_s *env = NuTexAnimEnvCreate(NULL, mtl, tids, prog);
    CHECK(env != NULL);
    if (env) {
        static const unsigned signals[] = {0, 2, 2, 2, 2, 2, 2, 0, 0, 2, 4, 4, 4};
        static const int want[] = {0, 1, 1, 1, 1, 0, 0, 2, 2, 1, 0, 0, 0};
        for (size_t tick = 0; tick < SDL_arraysize(signals); tick++) {
            NuTexAnimSetSignals(signals[tick]);
            NuTexAnimEnvProc(env);
            CHECK(env->tex_ix == want[tick] && mtl->tid == tids[want[tick]]);
            CHECK(drawn_texture(mtl) == nw_rndr_texture(tids[want[tick]]));
        }
        NuTexAnimEnvDestroy(env);
    }
    NuTexAnimProgDestroy(prog);
}

static void write_parser_fixture(const char *disc)
{
    char text[9000], *path = NULL;
    const char first[] = "first \"two words\" 123,4\r\n";
    size_t at = sizeof(first) - 1;
    memcpy(text, first, at);
    memset(text + at, 'a', 255);
    at += 255;
    text[at++] = ' ';
    text[at++] = '\n';
    memset(text + at, 'b', 257);
    at += 257;
    text[at++] = '\n';
    memset(text + at, 'c', 8193);
    at += 8193;
    text[at++] = '\n';
    memcpy(text + at, "last", 4);
    at += 4;
    SDL_asprintf(&path, "%sPARSER.TXT", disc);
    CHECK(SDL_SaveFile(path, text, at));
    SDL_free(path);
}

static void test_parser_buffers(void)
{
    struct nufpar_s *parser = NuFParCreate("parser.txt");
    CHECK(parser != NULL);
    if (!parser) return;
    CHECK(NuFParGetLine(parser) > 0);
    CHECK(NuFParGetWord(parser) == 5 && !strcmp(parser->wbuff, "first"));
    struct nufpar_s *other = NuFParCreate("parser.txt");
    CHECK(other != NULL);
    if (other) {
        CHECK(NuFParGetLine(other) > 0);
        CHECK(NuFParGetWord(other) == 5 && NuFParGetWord(other) == 9);
        NuFParUnGetWord(parser);
        CHECK(NuFParGetWord(parser) == 5 && !strcmp(parser->wbuff, "first"));
        NuFParDestroy(other);
    }
    CHECK(NuFParGetWord(parser) == 9 && !strcmp(parser->wbuff, "two words"));
    CHECK(NuFParGetInt(parser) == 123 && NuFParGetInt(parser) == 4);
    CHECK(NuFParGetLine(parser) == 256);
    CHECK(NuFParGetWord(parser) == 255 && strspn(parser->wbuff, "a") == 255);
    CHECK(NuFParGetWord(parser) == 0);
    CHECK(NuFParGetLine(parser) == 0 && NuFParGetError(parser) != NULL);
    CHECK(parser->lbuff[0] == 0 && parser->wbuff[0] == 0);
    CHECK(NuFParGetLine(parser) == 0);
    NuFParDestroy(parser);

    parser = NuFParCreate("parser.txt");
    CHECK(parser != NULL);
    if (!parser) return;
    CHECK(NuFParSetLimits(parser, 9000, 9000));
    CHECK(NuFParGetLine(parser) > 0);
    const int lines[] = {256, 257, 8193, 4};
    const int words[] = {255, 257, 8193, 4};
    const char *alphabet[] = {"a", "b", "c", "last"};
    for (size_t i = 0; i < SDL_arraysize(lines); i++) {
        CHECK(NuFParGetLine(parser) == lines[i]);
        CHECK(NuFParGetWord(parser) == words[i]);
        CHECK(strspn(NuFParWord(parser), alphabet[i]) == (size_t)words[i]);
        CHECK(NuFParLine(parser)[lines[i]] == 0 && NuFParWord(parser)[words[i]] == 0);
        if (words[i] > 255) CHECK(parser->wbuff[0] == 0);
        CHECK(NuFParGetWord(parser) == 0);
    }
    CHECK(NuFParGetLine(parser) == 0 && NuFParGetError(parser) == NULL);
    CHECK(!NuFParSetLimits(parser, 0, 1) && NuFParGetError(parser) != NULL);
    NuFParDestroy(parser);

    parser = NuFParCreate("parser.txt");
    CHECK(parser != NULL);
    if (!parser) return;
    CHECK(NuFParSetLimits(parser, 9000, 255));
    CHECK(NuFParGetLine(parser) > 0 && NuFParGetLine(parser) == 256);
    CHECK(NuFParGetWord(parser) == 255);
    CHECK(NuFParGetLine(parser) == 257);
    CHECK(NuFParGetWord(parser) == 0 && NuFParGetError(parser) != NULL);
    CHECK(parser->wbuff[0] == 0);
    NuFParDestroy(parser);
}

static const char v2_script[] = "ats 2 title\ntime frames 30\nsequence fill {\nframes 0..12\nstep 0.6\n}\n";

static bool read_v2(const char *disc, const char *text, NwTexAnimSequence *sequence, NwTexAnimError *error)
{
    char *path = NULL;
    SDL_asprintf(&path, "%sV2.ATS", disc);
    bool saved = SDL_SaveFile(path, text, strlen(text));
    SDL_free(path);
    CHECK(saved);
    return saved && nw_texanim_read_sequence("v2.ats", sequence, error);
}

static void test_v2_sequences(const char *disc)
{
    NwTexAnimSequence sequence = {0}, seconds = {0};
    NwTexAnimError error;
    CHECK(read_v2(disc, v2_script, &sequence, &error));
    CHECK(!error.line && !error.message[0]);
    CHECK(!strcmp(sequence.effect, "title") && !strcmp(sequence.track, "fill"));
    CHECK(sequence.first == 0 && sequence.last == 12);
    CHECK(sequence.sharing == NW_TEXANIM_SHARED);
    CHECK(sequence.ticks_per_second == 150 && sequence.ticks_per_unit == 5 && sequence.step_ticks == 3);
    for (int frame = 0; frame <= 12; frame++) {
        CHECK(nw_texanim_sample_sequence(&sequence, frame * 3) == frame);
        CHECK(nw_texanim_sample_sequence(&sequence, frame * 3 + 1) == frame);
        CHECK(nw_texanim_sample_sequence(&sequence, frame * 3 - 1) == (frame ? frame - 1 : 0));
    }
    const int64_t seek[] = {36, 1, 9, 0, 3, INT64_MAX, INT64_MIN, 9};
    const int want[] = {12, 0, 3, 0, 1, 12, 0, 3};
    for (size_t i = 0; i < SDL_arraysize(seek); i++) {
        CHECK(nw_texanim_sample_sequence(&sequence, seek[i]) == want[i]);
        CHECK(nw_texanim_sample_sequence(&sequence, seek[i]) == want[i]);
    }
    CHECK(read_v2(disc, "ats 2 title\ntime seconds\nsequence fill {\nframes 0..12\nstep 1/50\n}", &seconds, NULL));
    CHECK(seconds.ticks_per_second == 50 && seconds.step_ticks == 1);
    for (int tick = 0; tick < 20; tick++)
        CHECK(nw_texanim_sample_sequence(&sequence, 3 * tick) == nw_texanim_sample_sequence(&seconds, tick));
    CHECK(read_v2(disc, "; comment\n\nats 2 \"ti;tle\" ; comment\ntime frames 30000/1001\n"
                        "sequence \"fill layer\" {\n step 3/5; inline\n frames 4..16\n}\n", &sequence, &error));
    CHECK(!strcmp(sequence.effect, "ti;tle") && !strcmp(sequence.track, "fill layer"));
    CHECK(sequence.ticks_per_second == 150000 && sequence.ticks_per_unit == 5005 && sequence.step_ticks == 3003);
    CHECK(nw_texanim_sample_sequence(&sequence, 3002) == 4 && nw_texanim_sample_sequence(&sequence, 3003) == 5);
    CHECK(read_v2(disc, "ats 2 x\ntime frames 10/3\nsequence x {\nframes 0..1\nstep 1/3\n}", &sequence, NULL));
    CHECK(sequence.ticks_per_second == 10 && sequence.ticks_per_unit == 3 && sequence.step_ticks == 1);
    CHECK(read_v2(disc, "ats 2 x\ntime frames 29.97\nsequence x {\nframes 2147483647..2147483647\nstep 1\n}",
                  &sequence, &error));
    CHECK(sequence.ticks_per_second == 2997 && sequence.ticks_per_unit == 100);
    CHECK(nw_texanim_sample_sequence(&sequence, INT64_MAX) == INT32_MAX);
    CHECK(nw_texanim_sample_sequence(NULL, 0) == -1);
    CHECK(nw_texanim_sample_sequence(&(NwTexAnimSequence){0}, 0) == -1);

    static const char *invalid[] = {
        "", "Scriptname title", "ats 3 title", "ats 2", "ats 2 x extra", "ats 2 \"\"", "ats 2 \"broken",
        "ats 2 12345678901234567890123456789012", "ats 2 x\ntime frames", "ats 2 x\ntime seconds extra",
        "ats 2 x\ntime frames 0", "ats 2 x\ntime frames 1/0", "ats 2 x\ntime frames nan",
        "ats 2 x\ntime frames inf", "ats 2 x\ntime frames 9223372036854775808", "ats 2 x\ntime frames 30,0",
        "ats 2 x\ntime seconds\nsequence x{", "ats 2 x\ntime seconds\nsequence x {\nframes 0..1\n}",
        "ats 2 x\ntime seconds\nsequence x {\nframes 0..1\nstep 1",
        "ats 2 x\ntime seconds\nsequence x {\nframes 0..1\nstep 1\n} extra",
        "ats 2 x\ntime seconds\nsequence x {\nframes 0..1\nstep 1\n}\nsequence y {",
    };
    static const char *bad_properties[] = {
        "frames 1..0\nstep 1", "frames -1..1\nstep 1", "frames 0..2147483648\nstep 1",
        "frames 0..1 trailing\nstep 1", "frames 0..1\nframes 0..2\nstep 1", "frames 0..1\nstep 1\nstep 2",
        "frames 0..1\nstep 0", "frames 0..1\nstep -1", "frames 0..1\nstep 1/0", "frames 0..1\nstep 1/2/3",
        "frames 0..1\nstep 1e-3", "frames 0..1\nstep 0.00000000000000000001", "frames 0..1\nrate 1",
        "frames 0..1\nstep 9223372036854775808", "frames 0..1\nstep 1\nplayback random",
    };
    unsigned char before[sizeof(sequence)];
    memcpy(before, &sequence, sizeof(sequence));
    for (size_t i = 0; i < SDL_arraysize(invalid); i++) {
        CHECK(!read_v2(disc, invalid[i], &sequence, &error));
        CHECK(!memcmp(before, &sequence, sizeof(sequence)));
        CHECK(error.line > 0 && strstr(error.message, "v2.ats:") != NULL);
    }
    for (size_t i = 0; i < SDL_arraysize(bad_properties); i++) {
        char text[512];
        SDL_snprintf(text, sizeof(text), "ats 2 title\ntime seconds\nsequence fill {\n%s\n}\n", bad_properties[i]);
        CHECK(!read_v2(disc, text, &sequence, &error));
        CHECK(!memcmp(before, &sequence, sizeof(sequence)) && error.line >= 4 && error.message[0]);
    }
    CHECK(!read_v2(disc, "ats 2 x\ntime frames 9223372036854775807\nsequence x {\nframes 0..1\nstep 1/2\n}",
                   &sequence, &error));
    CHECK(strstr(error.message, "64-bit") != NULL);
    CHECK(!read_v2(disc, "ats 2 x\ntime frames 1/9223372036854775807\nsequence x {\nframes 0..1\nstep 2\n}",
                   &sequence, &error));
    CHECK(strstr(error.message, "64-bit") != NULL);
    char long_line[5000];
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = 0;
    CHECK(!read_v2(disc, long_line, &sequence, &error));
    CHECK(strstr(error.message, "line limit") != NULL && !memcmp(before, &sequence, sizeof(sequence)));
    CHECK(!read_v2(disc, "\n; comment\nats 2 x\ntime seconds\nsequence x {\nstep bad\n}", &sequence, &error));
    CHECK(error.line == 6);
    CHECK(!nw_texanim_read_sequence("missing-v2.ats", &sequence, &error) && error.line == 0);
    CHECK(!memcmp(before, &sequence, sizeof(sequence)));
}

static void test_v2_runtime(const char *disc, struct numtl_s *mtl, short tids[3])
{
    const char *text = "ats 2 title\ntime frames 30\nsequence fill {\nframes 0..2\nstep 0.6\n}\n";
    NwTexAnimSequence data;
    CHECK(read_v2(disc, text, &data, NULL));
    NuTexAnimProgSysInit();
    struct nutexanimprog_s *legacy = NuTexAnimProgReadScript(NULL, "test.ats");
    struct nutexanimprog_s *p = NuTexAnimProgReadScript(NULL, "v2.ats");
    CHECK(legacy && p && NuTexAnimProgFind("TITLE") == p && NuTexAnimProgFind("test") == legacy);
    if (!legacy || !p) return;
    CHECK(nw_texanim_sequence(p)->ticks_per_second == 150 && !nw_texanim_sequence(legacy));
    unsigned char arena[4096] = {0};
    union variptr_u cursor = {.voidptr = arena};
    CHECK(!NuTexAnimProgReadScript(&cursor, "v2.ats") && cursor.voidptr == arena);
    CHECK(strstr(nw_texanim_error()->message, "already loaded") != NULL);
    CHECK(!read_v2(disc, "ats 3 broken", &data, NULL) && !NuTexAnimProgReadScript(&cursor, "v2.ats"));
    CHECK(!read_v2(disc, "Scriptname TITLE\nTex 0\nEnd\n", &data, NULL));
    /* A retail V1 named like a loaded V2 yields the V2, without using the caller's buffer. */
    CHECK(NuTexAnimProgReadScript(&cursor, "v2.ats") == p && cursor.voidptr == arena && NuTexAnimProgFind("title") == p);
    CHECK(read_v2(disc, "ats 2 TEST\ntime seconds\nsequence x {\nframes 0..2\nstep 1\n}", &data, NULL));
    CHECK(!NuTexAnimProgReadScript(NULL, "v2.ats") && NuTexAnimProgFind("test") == legacy);
    CHECK(!read_v2(disc, "ats 2 broken\ntime seconds\nsequence x {\nstep bad\n}", &data, NULL));
    CHECK(!NuTexAnimProgReadScript(NULL, "v2.ats") && !NuTexAnimProgFind("broken") && nw_texanim_error()->line == 4);
    CHECK(read_v2(disc, text, &data, NULL));

    struct numtl_s *other = NuMtlCreate(1);
    other->tid = tids[0];
    NuMtlUpdate(other);
    struct nutexanim_s *a = NuTexAnimCreate(NULL, p, mtl, tids, 3);
    struct nutexanim_s *b = NuTexAnimCreate(NULL, p, other, tids, 3);
    CHECK(a && b);
    if (!a || !b) return;
    NuTexAnimAddList(a);
    CHECK(!NuTexAnimCreate(NULL, p, other, tids, 2)); /* Reject the whole interval before accessing tids. */
    CHECK(!NuTexAnimCreate(NULL, p, mtl, tids, 3)); /* Concurrent writers of one material are unsupported. */
    const int64_t times[] = {0, 3, 6, 2, INT64_MIN, INT64_MAX, 3};
    const int frames[] = {0, 1, 2, 0, 0, 2, 1};
    for (size_t i = 0; i < SDL_arraysize(times); i++) {
        CHECK(nw_texanim_time(a, times[i]));
        NuTexAnimProcess();
        NuTexAnimProcess();
        CHECK(a->env->tex_ix == frames[i] && drawn_texture(mtl) == nw_rndr_texture(tids[frames[i]]));
        CHECK(b->env->tex_ix == 0 && other->tid == tids[0]);
    }
    CHECK(nw_texanim_bind(b, legacy) && !nw_texanim_time(b, 3));
    CHECK(nw_texanim_bind(b, p) && nw_texanim_time(b, 6));
    NuTexAnimEnvReset(a->env);
    NuTexAnimProcessList(a);
    CHECK(a->env->tex_ix == 0 && mtl->tid == tids[0] && b->env->tex_ix == 2);

    NuFileInitEx(0, 0); /* Native dump tests use the writable host0: device. */
    NuTexAnimProgWrite("blocked.bin", p);
    CHECK(strstr(nw_texanim_error()->message, "no native binary") != NULL);
    NuTexAnimProgWrite__retail("raw.bin", p); /* Deliberately bypass the guard to test the reader. */
    union variptr_u size = {.voidptr = arena};
    NuTexAnimProgCreate(&size, 0, "layout");
    int fh = NuFileOpen("raw.bin", NUFILE_READ);
    CHECK(fh && NuFileOpenSize(fh) == (int)(size.u8 - arena + 2 * sizeof(short)));
    if (fh) NuFileClose(fh);
    CHECK(!NuTexAnimProgRead(&cursor, "raw.bin") && cursor.voidptr == arena);
    CHECK(!NuTexAnimProgRead(NULL, "raw.bin") && NuTexAnimProgFind("title") == p);
    NuTexAnimProgWrite("legacy.bin", legacy);
    struct nutexanimprog_s *native = NuTexAnimProgRead(NULL, "legacy.bin");
    CHECK(native && !nw_texanim_sequence(native));
    if (native) NuTexAnimProgDestroy(native);
    NuFileInitEx(1, 0);
    NuTexAnimProgDestroy(legacy);

    NuTexAnimProgSysInit(); /* Retire the registry while both model environments still own p. */
    CHECK(!NuTexAnimProgFind("title") && nw_texanim_time(a, 6));
    NuTexAnimProgDestroy(p); /* Already retired, still alive. */
    NuTexAnimDestroy(a);
    CHECK(nw_texanim_time(b, 3) && b->env->tex_ix == 1);
    struct nutexanimprog_s *fresh = NuTexAnimProgReadScript(NULL, "v2.ats");
    CHECK(fresh && fresh != p && NuTexAnimProgFind("title") == fresh);
    if (fresh) NuTexAnimProgDestroy(fresh);
    NuTexAnimDestroy(b); /* Last reference frees the retired program. */
    p = NuTexAnimProgReadScript(NULL, "v2.ats");
    a = p ? NuTexAnimCreate(NULL, p, mtl, tids, 3) : NULL;
    NuTexAnimProgRelease();
    CHECK(!NuTexAnimProgFind("title") && a && nw_texanim_time(a, 3));
    if (a) NuTexAnimDestroy(a);
    NuMtlDestroy(other);
}

static void test_instances(struct numtl_s *mtl, short tids[3])
{
    const char text[] = "ats 2 players\ntime frames 30\nsharing independent\nsequence x {\nframes 0..2\nstep 0.6\n}\n";
    struct nutexanimprog_s *p = nw_texanim_from_memory("players",text,sizeof(text)-1);
    CHECK(p && nw_texanim_sequence(p)->sharing == NW_TEXANIM_INDEPENDENT);
    if (!p) return;
    struct nutexanim_s source = {.mtl=mtl,.tids=tids,.numtids=3};
    NwTexAnimConfig config = {.clock=7};
    NwTexAnimInstance *a = nw_texanim_instance(p,&source,config,NULL);
    NwTexAnimInstance *b = nw_texanim_instance(p,&source,config,NULL);
    CHECK(a && b && a != b && !nw_texanim_instance(p,&source,config,a));
    int original = mtl->tid;
    CHECK(nw_texanim_instance_time(a,3) && nw_texanim_instance_frame(a) == 1);
    CHECK(nw_texanim_instance_frame(b) == 0 && mtl->tid == original);
    config.sharing = NW_TEXANIM_SHARED;
    NwTexAnimInstance *group = nw_texanim_instance(p,&source,config,NULL);
    NwTexAnimInstance *joined = nw_texanim_instance(p,&source,config,group);
    CHECK(group && joined == group);
    CHECK(!nw_texanim_instance(p,&source,(NwTexAnimConfig){.sharing=NW_TEXANIM_SHARED,.clock=8},group));
    CHECK(!nw_texanim_instance(p,&source,(NwTexAnimConfig){.sharing=NW_TEXANIM_SHARED,.clock=7,.origin=1},group));
    short other[3] = {tids[2],tids[1],tids[0]};
    struct nutexanim_s wrong = {.tids=other,.numtids=3};
    CHECK(!nw_texanim_instance(p,&wrong,config,group));
    CHECK(!nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=0},NULL));
    CHECK(!nw_texanim_instance(p,&source,(NwTexAnimConfig){.sharing=99,.clock=7},NULL));
    wrong.numtids = 2;
    CHECK(!nw_texanim_instance(p,&wrong,config,NULL));
    config.sharing = NW_TEXANIM_INDEPENDENT; config.origin = 3;
    NwTexAnimInstance *delayed = nw_texanim_instance(p,&source,config,NULL);
    CHECK(delayed && nw_texanim_instance_time(delayed,3) && nw_texanim_instance_frame(delayed) == 0);
    CHECK(nw_texanim_instance_time(delayed,6) && nw_texanim_instance_frame(delayed) == 1);
    CHECK(nw_texanim_instance_time(group,6) && nw_texanim_instance_frame(joined) == 2);
    config.origin = INT64_MIN;
    NwTexAnimInstance *early = nw_texanim_instance(p,&source,config,NULL);
    CHECK(early && nw_texanim_instance_time(early,INT64_MAX) && nw_texanim_instance_frame(early) == 2);
    nw_texanim_instance_drop(early);
    config.origin = INT64_MAX;
    early = nw_texanim_instance(p,&source,config,NULL);
    CHECK(early && nw_texanim_instance_time(early,INT64_MIN) && nw_texanim_instance_frame(early) == 0);
    nw_texanim_instance_drop(early);
    NuTexAnimProgSysInit(); /* All players retain the retired program. */
    CHECK(!NuTexAnimProgFind("players") && nw_texanim_instance_clock(a,0.6,30));
    CHECK(nw_texanim_instance_frame(a) == 1 && nw_texanim_instance_tid(a) == tids[1]);
    CHECK(!nw_texanim_instance_clock(a,NAN,30) && nw_texanim_instance_frame(a) == 1);
    CHECK(!nw_texanim_instance_clock(a,1,0) && nw_texanim_instance_frame(a) == 1);
    const int64_t seek[] = {INT64_MAX,INT64_MIN,3,0,6,3};
    const int want[] = {2,0,1,0,2,1};
    for (size_t n = 0; n < SDL_arraysize(seek); n++) {
        CHECK(nw_texanim_instance_time(a,seek[n]) && nw_texanim_instance_frame(a) == want[n]);
        CHECK(nw_texanim_instance_frame(b) == 0 && mtl->tid == original);
    }
    NwMtlBinding old = nw_rndr_mtl_scope((NwMtlBinding){.material=mtl,.tid=nw_texanim_instance_tid(a)});
    nw_rndr_push(&mtl->rs,nw_rndr_record(NW_CMD_DRAW,mtl,NULL,0));
    nw_rndr_mtl_scope(old);
    old = nw_rndr_mtl_scope((NwMtlBinding){.material=mtl,.tid=nw_texanim_instance_tid(b)});
    nw_rndr_push(&mtl->rs,nw_rndr_record(NW_CMD_DRAW,mtl,NULL,0));
    nw_rndr_mtl_scope(old);
    CHECK(nw_texanim_instance_time(a,6)); /* Already recorded draws keep their earlier values. */
    nw_texanim_instance_drop(a); nw_texanim_instance_drop(b);
    nw_texanim_instance_drop(group); nw_texanim_instance_drop(joined); nw_texanim_instance_drop(delayed);
    NuRndrEndScene(); NuRndrVu1Draw();
    const NwPacket *packet = nw_packet_take();
    CHECK(packet->ncmds == 2 && packet->nmtls == 2);
    CHECK(packet->mtls[packet->cmds[0].mtl].tex == nw_rndr_texture(tids[1]));
    CHECK(packet->mtls[packet->cmds[1].mtl].tex == nw_rndr_texture(tids[0]));
    NuRndrSwapStreamBuffers();
    CHECK(!nw_texanim_instance_time(NULL,0) && nw_texanim_instance_frame(NULL) == -1);
    CHECK(nw_texanim_instance_tid(NULL) == 0);
    const char *bad[] = {"sharing other", "sharing", "sharing shared extra", "sharing shared\nsharing independent"};
    for (size_t n = 0; n < SDL_arraysize(bad); n++) {
        char invalid[200]; SDL_snprintf(invalid,sizeof(invalid),"ats 2 bad\ntime seconds\n%s\nsequence x {\nframes 0..1\nstep 1\n}\n",bad[n]);
        CHECK(!nw_texanim_from_memory("bad",invalid,strlen(invalid)));
    }
}

static NwTexture *image_resolver(void *context, const char *id)
{
    return !strcmp(id,"image") ? context : NULL;
}
static void test_array_players(struct numtl_s *mtl, short tids[3])
{
    const char text[] = "ats 2 arrays\ntime frames 30\nsharing independent\nsequence x {\nframes 0..2\nstep 0.6\n}\n";
    struct nutexanimprog_s *p = nw_texanim_from_memory("arrays",text,sizeof(text)-1);
    struct nutexanim_s source = {.mtl=mtl,.tids=tids,.numtids=3};
    NwTexAnimInstance *a = nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=1},NULL);
    NwTexAnimInstance *b = nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=1},NULL);
    CHECK(a && b && !nw_texanim_instance_array(NULL,NULL));
    nw_tex_set_max_side(2048); nw_tex_set_max_layers(64);
    CHECK(nw_texanim_instance_array(a,NULL) && nw_texanim_instance_array(b,a));
    const NwGpuTex *image = nw_texanim_instance_texture(a);
    CHECK(image && image == nw_texanim_instance_texture(b) && image->tex.layers == 3);
    NuTexAnimProgSysInit();
    CHECK(nw_texanim_instance_time(a,3) && nw_texanim_instance_frame(a) == 1);
    CHECK(nw_texanim_instance_frame(b) == 0);
    for (int n = 0; n < 2; n++) {
        NwTexAnimInstance *i = n ? b : a;
        NwMtlBinding old = nw_rndr_mtl_scope((NwMtlBinding){.material=mtl,.texture=image,
            .layer=nw_texanim_instance_frame(i)});
        nw_rndr_push(&mtl->rs,nw_rndr_record(NW_CMD_DRAW,mtl,NULL,0)); nw_rndr_mtl_scope(old);
    }
    CHECK(nw_texanim_instance_time(a,INT64_MAX));
    nw_texanim_instance_drop(a); CHECK(nw_texanim_instance_texture(b) == image);
    CHECK(nw_texanim_instance_time(b,0)); nw_texanim_instance_drop(b);
    NuRndrEndScene(); NuRndrVu1Draw(); const NwPacket *packet = nw_packet_take();
    CHECK(packet->ncmds == 2 && packet->nmtls == 2);
    CHECK(packet->mtls[packet->cmds[0].mtl].tex == image && packet->mtls[packet->cmds[0].mtl].layer == 1);
    CHECK(packet->mtls[packet->cmds[1].mtl].tex == image && packet->mtls[packet->cmds[1].mtl].layer == 0);
    NuRndrSwapStreamBuffers();
    NwGpuJob *jobs = NULL; uint32_t cap = 0; bool freed = false;
    uint32_t count = nw_gpu_take(0,&jobs,&cap);
    for (uint32_t n = 0; n < count; n++) CHECK(jobs[n].op != NW_GPU_FREE_TEX || jobs[n].obj != image);
    count = nw_gpu_take(UINT64_MAX,&jobs,&cap);
    for (uint32_t n = 0; n < count; n++) if (jobs[n].op == NW_GPU_FREE_TEX && jobs[n].obj == image) freed = true;
    CHECK(freed); SDL_free(jobs);
    for (int reset = 0; reset < 8; reset++) {
        p = nw_texanim_from_memory("arrays",text,sizeof(text)-1);
        a = nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=1},NULL);
        CHECK(a && nw_texanim_instance_array(a,NULL) && nw_texanim_instance_frame(a) == 0);
        CHECK(nw_texanim_instance_time(a,INT64_MAX) && nw_texanim_instance_frame(a) == 2);
        CHECK(nw_texanim_instance_time(a,0) && nw_texanim_instance_frame(a) == 0);
        NuTexAnimProgSysInit(); nw_texanim_instance_drop(a);
    }
}
static void test_uv_program(const char *disc, struct numtl_s *mtl, short tids[3])
{
    const char *prefix="ats 2 uv3\ntime frames 30\nmipmaps off\nsharing independent\nsequence fill {\nframes 0..2\nstep 0.6\n}\n";
    const char *body="uv flow {\noffset linear {\nkey 0 -1 0.25\nkey 1/7 1 0.75\nkey 1/3 3 -0.25\n}\n"
        "scale step {\nkey 0 1 1\nkey 1/7 -1 0\n}\nrotation linear {\nkey 0 0\nkey 2 360\n}\npivot 0.25 0.75\naddress mirror\n}\n";
    char text[2048]; SDL_snprintf(text,sizeof(text),"%s%s",prefix,body);
    NwTexAnimSequence sequence; CHECK(read_v2(disc,text,&sequence,NULL));
    CHECK(sequence.ticks_per_unit==105 && sequence.ticks_per_second==3150 && sequence.step_ticks==63);
    struct nutexanimprog_s *p=NuTexAnimProgReadScript(NULL,"v2.ats"); CHECK(p);
    struct nutexanim_s *legacy=NuTexAnimCreate(NULL,p,mtl,tids,3);
    CHECK(legacy && !nw_texanim_time(legacy,0)); NuTexAnimDestroy(legacy);
    struct nutexanim_s source={.tids=tids,.numtids=3,.mtl=mtl};
    NwTexAnimInstance *a=nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=3},NULL);
    NwTexAnimInstance *b=nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=3},NULL); NwUV u,v;
    CHECK(a && b && nw_texanim_instance_uv(b,&v));
    for (int n=0; n<=210; n++) {
        CHECK(nw_texanim_instance_time(a,n) && nw_texanim_instance_uv(a,&u));
        double r=n<=15 ? n/15.0 : n<35 ? (n-15)/20.0 : 1;
        double x=n<=15 ? -1+2*r : 1+2*r, y=n<=15 ? .25+.5*r : .75-r;
        CHECK(fabs(u.value[0]-x)<1e-6 && fabs(u.value[1]-y)<1e-6 && fabs(u.value[4]-360*n/210.0)<2e-5);
        CHECK(u.value[2]==(n<15 ? 1 : -1) && u.value[3]==(n<15 ? 1 : 0));
        CHECK(u.value[5]==.25f && u.value[6]==.75f && u.address==2 && u.binding!=v.binding);
    }
    CHECK(nw_texanim_instance_time(a,INT64_MIN) && nw_texanim_instance_uv(a,&u) && u.value[0]==-1);
    CHECK(nw_texanim_instance_time(a,INT64_MAX) && nw_texanim_instance_uv(a,&u) && u.value[4]==360);
    CHECK(nw_texanim_instance_clock(a,0,30) && nw_texanim_instance_uv(a,&u) && u.value[4]==0);
    CHECK(!nw_texanim_instance_uv(NULL,&u) && !nw_texanim_instance_uv(a,NULL));
    for (int n=0; n<2; n++) {
        CHECK(nw_texanim_instance_time(a,n*15) && nw_texanim_instance_uv(a,&u));
        NwMtlBinding old=nw_rndr_mtl_scope((NwMtlBinding){.material=mtl,.tid=tids[0],.uv=u});
        nw_rndr_push(&mtl->rs,nw_rndr_record(NW_CMD_DRAW,mtl,NULL,0)); nw_rndr_mtl_scope(old);
    }
    NuTexAnimProgSysInit(); CHECK(nw_texanim_instance_time(b,15) && nw_texanim_instance_uv(b,&v) && v.value[2]==-1);
    nw_texanim_instance_drop(a); nw_texanim_instance_drop(b);
    NuRndrEndScene(); NuRndrVu1Draw(); const NwPacket *packet=nw_packet_take();
    CHECK(packet->ncmds==2 && packet->nmtls==2);
    CHECK(packet->mtls[packet->cmds[0].mtl].uv.value[0]==-1 && packet->mtls[packet->cmds[1].mtl].uv.value[0]==1);
    NuRndrSwapStreamBuffers();
    const char *bad[]={"offset nan", "offset 0", "pivot -1 0", "address unknown", "rotation 0\nrotation 1",
        "offset bezier {\nkey 0 0 0\n}", "offset linear {\n}", "scale step {\nkey 0 1 1\nkey 0 2 2\n}",
        "rotation linear {\nkey 1 0\nkey 0 1\n}", "rotation linear {\nkey -1 0\n}",
        "rotation linear {\nkey 1/9223372036854775807 0\n}", "pivot 0 2", "scale 1/0 1", "rotation 99999999999999999999999"};
    for (size_t j=0; j<SDL_arraysize(bad); j++) {
        SDL_snprintf(text,sizeof(text),"%suv flow {\n%s\n}\n",prefix,bad[j]);
        CHECK(!read_v2(disc,text,&sequence,NULL) && !nw_texanim_from_memory("bad UV",text,strlen(text)));
        CHECK(nw_texanim_error()->line>0 && !NuTexAnimProgFind("uv3"));
    }
    char *large=SDL_malloc(10000); size_t length=0;
    for (int n=0; n<1000; n++) { memcpy(large+length,"; UV\n",5); length+=5; }
    length+=(size_t)SDL_snprintf(large+length,10000-length,"%s%s",prefix,body);
    p=nw_texanim_from_memory("large UV",large,length); CHECK(p); SDL_free(large);
    if (p) NuTexAnimProgDestroy(p);
    SDL_snprintf(text,sizeof(text),"%s",prefix); p=nw_texanim_from_memory("explicit off",text,strlen(text));
    a=nw_texanim_instance(p,&source,(NwTexAnimConfig){.clock=3},NULL);
    CHECK(a && nw_texanim_instance_uv(a,&u) && u.binding && !u.mips && u.value[2]==1 && u.value[3]==1);
    nw_texanim_instance_drop(a); if (p) NuTexAnimProgDestroy(p);
    const char invalid[]="ats 2 bad\ntime seconds\nmipmaps automatic\n";
    CHECK(!nw_texanim_from_memory("mips",invalid,sizeof(invalid)-1));
}
static void test_playback(const char *disc, struct numtl_s *mtl, short tids[3])
{
    /* Independent frame tables: endpoints occur once, each frame lasts three ticks. */
    const int ping[]={0,1,2,1}, loop[]={0,1,2};
    for (int n=1; n<=13; n+=(n==1 ? 1 : 11)) for (int mode=0; mode<3; mode++) {
        NwTexAnimSequence s={.first=4,.last=3+n,.step_ticks=3,.start_ticks=5,.playback=mode,
                            .before_restore=true,.finish_restore=true};
        CHECK(nw_texanim_sample_sequence(&s,INT64_MIN)==-1);
        for (int t=0; t<200; t++) {
            int want=-1, step=(t-5)/3;
            if (t>=5) {
                if (!mode) want=step<n ? step : -1;
                else if (n==1) want=0;
                else if (mode==1) want=step%n;
                else { int period=2*(n-1), p=step%period; want=p<n ? p : period-p; }
            }
            CHECK(nw_texanim_sample_sequence(&s,t)==(want<0 ? -1 : want+4));
        }
        s.before_restore=false; CHECK(nw_texanim_sample_sequence(&s,INT64_MIN)==4);
        if (!mode) { s.finish_restore=false; CHECK(nw_texanim_sample_sequence(&s,INT64_MAX)==n+3); }
    }
    NwTexAnimSequence s={.first=0,.last=2,.step_ticks=1,.playback=NW_TEXANIM_PINGPONG};
    for (int j=0; j<32; j++) CHECK(nw_texanim_sample_sequence(&s,j)==ping[j%4]);
    s.playback=NW_TEXANIM_LOOP;
    for (int j=0; j<32; j++) CHECK(nw_texanim_sample_sequence(&s,j)==loop[j%3]);
    s.step_ticks=INT64_MAX; CHECK(nw_texanim_sample_sequence(&s,0)==-1);

    const char image[]="sequence images {\nframes 0..2\nstep 1/3\nplayback pingpong\nstart 1/2\nbefore restore\n}\n";
    const char scroll[]="uv scroll {\nstart 1/2\nbefore restore\nplayback loop\nduration 2/3\n"
        "offset linear {\nkey 0 0 0\nkey 2/3 4 2\n}\n}\n";
    const char spin[]="uv spin {\nplayback pingpong\nduration 1/5\nrotation linear {\nkey 0 0\nkey 1/5 90\n}\n}\n";
    const char other[]="sequence other {\nframes 1..2\nstep 1\nfinish restore\n}\n";
    char text[2048]; SDL_snprintf(text,sizeof(text),"ats 2 multi\ntime frames 30\n%s%s%s%s",image,scroll,spin,other);
    NwTexAnimSequence unchanged={.first=99}; NwTexAnimError error;
    CHECK(!read_v2(disc,text,&unchanged,&error) && unchanged.first==99 && error.message[0]);
    struct nutexanimprog_s *p=nw_texanim_from_memory("multi",text,strlen(text)); CHECK(p);
    if (!p) return;
    struct nutexanim_s source={.tids=tids,.numtids=3,.mtl=mtl}; NwTexAnimConfig config={.clock=19};
    CHECK(!nw_texanim_sequence(p) && !nw_texanim_instance(p,&source,config,NULL));
    /* Any V2 effect binds as an autonomous environment; only C3 sequences accept the host clock API. */
    struct nutexanim_s *own=NuTexAnimCreate(NULL,p,mtl,tids,3);
    CHECK(own && !nw_texanim_time(own,0));
    if (own) NuTexAnimDestroy(own);
    CHECK(!nw_texanim_instance_track(p,"Images",&source,config,NULL));
    NwTexAnimInstance *a=nw_texanim_instance_track(p,"images",&source,config,NULL);
    NwTexAnimInstance *b=nw_texanim_instance_track(p,"scroll",NULL,config,NULL);
    NwTexAnimInstance *c=nw_texanim_instance_track(p,"spin",NULL,config,NULL);
    NwTexAnimInstance *d=nw_texanim_instance_track(p,"other",&source,config,NULL);
    CHECK(a && b && c && d); NwUV u,v;
    CHECK(nw_texanim_instance_track(p,"images",&source,config,a)==a);
    nw_texanim_instance_drop(a);
    CHECK(!nw_texanim_instance_track(p,"other",&source,config,a));
    for (int t=0; t<300; t++) {
        /* LCM(2,3,5)=30 ticks/frame: 900 ticks/second for every track. */
        CHECK(nw_texanim_instance_clock(a,t,900) && nw_texanim_instance_time(b,t) && nw_texanim_instance_time(c,t));
        int frame=t<15 ? -1 : ping[((t-15)/10)%4];
        CHECK(nw_texanim_instance_frame(a)==frame && nw_texanim_instance_tid(a)==(frame<0 ? 0 : tids[frame]));
        CHECK(nw_texanim_instance_mask(a)==(t<15 ? 0 : NW_TEXANIM_IMAGE));
        CHECK(nw_texanim_instance_uv(b,&u) && nw_texanim_instance_uv(c,&v));
        CHECK(nw_texanim_instance_mask(b)==(t<15 ? 0 : NW_TEXANIM_OFFSET));
        CHECK(nw_texanim_instance_mask(c)==NW_TEXANIM_ROTATION);
        if (t>=15) CHECK(fabs(u.value[0]-4*((t-15)%20)/20.0)<1e-6);
        int turn=t%12; CHECK(fabs(v.value[4]-15*(turn<=6 ? turn : 12-turn))<1e-6);
        CHECK(nw_texanim_instance_uv_cycle(b)==(t<15 ? 0 : (t-15)/20));
        CHECK(nw_texanim_instance_uv_cycle(c)==t/6);
        unsigned owned=0;
        CHECK(nw_texanim_claim(&owned,nw_texanim_instance_mask(a)) && nw_texanim_claim(&owned,nw_texanim_instance_mask(b)));
        CHECK(nw_texanim_claim(&owned,nw_texanim_instance_mask(c)));
        unsigned before=owned;
        CHECK(!nw_texanim_claim(&owned,NW_TEXANIM_ROTATION) && owned==before);
    }
    CHECK(!nw_texanim_claim(NULL,1)); unsigned owned=0; CHECK(!nw_texanim_claim(&owned,64) && owned==0);
    CHECK(!nw_texanim_instance_complete(a) && !nw_texanim_instance_complete(b) && !nw_texanim_instance_complete(c));
    CHECK(nw_texanim_instance_frame(a)==ping[((299-15)/10)%4]);
    CHECK(nw_texanim_instance_time(d,59) && nw_texanim_instance_frame(d)==2);
    CHECK(nw_texanim_instance_time(d,60) && !nw_texanim_instance_mask(d));
    CHECK(nw_texanim_instance_complete(d) && !nw_texanim_instance_mask(d));
    CHECK(nw_texanim_instance_time(b,25) && nw_texanim_instance_uv(b,&u)); uint32_t identity=u.binding;
    nw_texanim_instance_stop(b); CHECK(!nw_texanim_instance_mask(b) && nw_texanim_instance_uv(b,&u) && !u.binding);
    CHECK(nw_texanim_instance_time(b,25) && nw_texanim_instance_uv(b,&u) && u.binding==identity && u.value[0]==2);
    CHECK(!nw_texanim_instance_clock(b,NAN,30) && nw_texanim_instance_uv(b,&v) && v.value[0]==u.value[0]);
    NuTexAnimProgSysInit(); /* All track descriptors/keys survive registry retirement. */
    CHECK(nw_texanim_instance_time(a,35) && nw_texanim_instance_frame(a)==2);
    nw_texanim_instance_drop(a); nw_texanim_instance_drop(b); nw_texanim_instance_drop(c); nw_texanim_instance_drop(d);

    /* Reordering tracks and releasing the input cannot change the sampled result. */
    SDL_snprintf(text,sizeof(text),"ats 2 multi\ntime frames 30\n%s%s%s%s",spin,other,scroll,image);
    p=nw_texanim_from_memory("reordered",text,strlen(text)); memset(text,0,sizeof(text));
    a=nw_texanim_instance_track(p,"images",&source,config,NULL); CHECK(a);
    CHECK(nw_texanim_instance_time(a,35) && nw_texanim_instance_frame(a)==2);
    nw_texanim_instance_drop(a); NuTexAnimProgSysInit();
    const char *bad[]={"uv x {\nplayback loop\noffset 1 0\n}\n", "uv x {\nfinish restore\nrotation 1\n}\n",
        "uv x {\nduration 1\nrotation linear {\nkey 2 1\n}\n}\n", "uv x {\nplayback pingpong\nduration 9223372036854775807\n}\n",
        "sequence x {\nframes 0..1\nstep 9223372036854775807\n}\n", "uv x {\nstart -1\n}\n",
        "uv x {\nstart 0\nstart 1\n}\n", "uv x {\nduration 0\n}\n", "uv x {\nbefore hold\n}\n",
        "uv x {\nfinish first\n}\n", "uv x {\nrotation 0\n}\nsequence x {\nframes 0..0\nstep 1\n}\n",
        "uv x {\nstart 1/3037000500\n}\nuv y {\nstart 1/3037000501\n}\n",
        "uv x {\nrotation 0\n}\nsequence y {\nframes 0..1\nstep 9223372036854775807\n}\n"};
    for (size_t j=0; j<SDL_arraysize(bad); j++) {
        SDL_snprintf(text,sizeof(text),"ats 2 invalid\ntime seconds\n%s",bad[j]);
        CHECK(!nw_texanim_from_memory("bad D1",text,strlen(text)) && !NuTexAnimProgFind("invalid"));
        CHECK(nw_texanim_error()->message[0] && nw_texanim_error()->line>0);
    }
    const char extreme[]="ats 2 extreme\ntime seconds\nsequence x {\nstart 9223372036854775807\nframes 0..2\nstep 1\n}\n"
        "uv y {\nstart 9223372036854775807\nrotation linear {\nkey 0 0\nkey 1 90\n}\n}\n";
    p=nw_texanim_from_memory("extreme",extreme,sizeof(extreme)-1); CHECK(p);
    config.origin=INT64_MAX;
    a=nw_texanim_instance_track(p,"x",&source,config,NULL); b=nw_texanim_instance_track(p,"y",NULL,config,NULL);
    CHECK(nw_texanim_instance_time(a,INT64_MIN) && nw_texanim_instance_frame(a)==0);
    CHECK(nw_texanim_instance_complete(a) && nw_texanim_instance_frame(a)==2);
    CHECK(nw_texanim_instance_complete(b) && nw_texanim_instance_uv(b,&u) && u.value[4]==90);
    CHECK(nw_texanim_instance_time(b,INT64_MAX) && nw_texanim_instance_uv(b,&u) && u.value[4]==0);
    nw_texanim_instance_drop(a); nw_texanim_instance_drop(b); NuTexAnimProgSysInit();
    const char uvonly[]="ats 2 only\ntime seconds\nuv x {\nstart 1\nbefore restore\nfinish restore\nduration 2\noffset 1 2\n}\n";
    p=nw_texanim_from_memory("UV only",uvonly,sizeof(uvonly)-1); config.origin=0;
    b=nw_texanim_instance_track(p,"x",NULL,config,NULL); CHECK(b && !nw_texanim_instance(p,NULL,config,NULL));
    for (int t=0; t<=4; t++) { CHECK(nw_texanim_instance_time(b,t)); CHECK(nw_texanim_instance_mask(b)==(t>=1 && t<3 ? NW_TEXANIM_OFFSET : 0)); }
    CHECK(nw_texanim_instance_complete(b) && !nw_texanim_instance_mask(b));
    nw_texanim_instance_drop(b); NuTexAnimProgSysInit();
    const char delayed[]="ats 2 delayed\ntime seconds\nsequence x {\nstart 1\nframes 0..2\nstep 1\n}\n";
    CHECK(read_v2(disc,delayed,&unchanged,NULL) && unchanged.start_ticks==1);
    p=nw_texanim_from_memory("delayed",delayed,sizeof(delayed)-1);
    struct nutexanimenv_s *delayed_env=p ? NuTexAnimEnvCreate(NULL,mtl,tids,p) : NULL;
    CHECK(delayed_env && !nw_texanim_instance(p,&source,config,NULL)); /* Autonomous env; no implicit C3 player. */
    NuTexAnimEnvDestroy(delayed_env);
    config.origin=INT64_MIN; a=nw_texanim_instance_track(p,"x",&source,config,NULL);
    CHECK(a && nw_texanim_instance_time(a,INT64_MAX) && nw_texanim_instance_frame(a)==2);
    CHECK(nw_texanim_instance_time(a,INT64_MIN) && nw_texanim_instance_frame(a)==0);
    nw_texanim_instance_drop(a); NuTexAnimProgSysInit(); config.origin=0;
    /* Exercise the name index and second parser pass past the original 4096-byte window. */
    size_t size=0; char *large=SDL_malloc(1000000);
    size+=(size_t)SDL_snprintf(large,1000000,"ats 2 indexed\ntime seconds\n");
    for (int j=0; j<512; j++) size+=(size_t)SDL_snprintf(large+size,1000000-size,"uv t%d {\nrotation %d\n}\n",j,j);
    p=nw_texanim_from_memory("indexed",large,size); CHECK(p);
    for (int j=0; j<512; j++) {
        SDL_snprintf(text,sizeof(text),"t%d",j); b=nw_texanim_instance_track(p,text,NULL,config,NULL);
        CHECK(b && nw_texanim_instance_uv(b,&u) && u.value[4]==j); nw_texanim_instance_drop(b);
    }
    NuTexAnimProgSysInit();
    size+=(size_t)SDL_snprintf(large+size,1000000-size,"uv t0 {\noffset 1 2\n}\n");
    CHECK(!nw_texanim_from_memory("late duplicate",large,size) && !NuTexAnimProgFind("indexed"));
    size=(size_t)SDL_snprintf(large,1000000,"ats 2 budget\ntime seconds\n");
    for (int j=0; j<26000; j++) size+=(size_t)SDL_snprintf(large+size,1000000-size,"uv t%d {\nrotation 0\n}\n",j);
    CHECK(!nw_texanim_from_memory("budget",large,size) && strstr(nw_texanim_error()->message,"budget"));
    SDL_free(large);
}

static void test_composition(struct numtl_s *mtl, short tids[3])
{
    const char text[]="ats 2 compose\ntime seconds\nsequence image {\nframes 0..2\nstep 1\nfinish restore\n}\n"
        "uv scroll {\nstart 1\nbefore restore\nplayback loop\nduration 2\noffset linear {\nkey 0 0 0\nkey 2 1 2\n}\n}\n"
        "uv spin {\nrotation linear {\nkey 0 0\nkey 2 90\n}\npivot 0.25 0.75\naddress mirror\n}\n";
    struct nutexanimprog_s *p=nw_texanim_from_memory("compose",text,sizeof(text)-1); CHECK(p);
    int original=mtl->tid; mtl->tid=tids[2];
    struct nutexanim_s source={.mtl=mtl,.tids=tids,.numtids=3}; NwTexAnimConfig config={.clock=5};
    NwTexAnimInstance *items[4]; const char *names[]={"image","scroll","spin"};
    for (int n=0; n<3; n++) { items[n]=nw_texanim_instance_track(p,names[n],&source,config,NULL); CHECK(items[n]); }
    uint32_t id=nw_texanim_binding(); CHECK(id);
    for (int t=0; t<=6; t++) {
        for (int n=0; n<3; n++) CHECK(nw_texanim_instance_time(items[n],t));
        NwMtlBinding a={.material=mtl,.tid=tids[2]}, b=a;
        CHECK(nw_texanim_compose(items,3,id,&a));
        NwTexAnimInstance *reverse[]={items[2],items[1],items[0]};
        CHECK(nw_texanim_compose(reverse,3,id,&b) && !memcmp(&a,&b,sizeof(a)));
        CHECK(a.tid==tids[t<3 ? t : 2] && a.uv.binding==id && mtl->tid==tids[2]);
        CHECK(a.uv.value[0]==(t<1 ? 0 : (t-1)%2*.5f) && a.uv.value[4]==(t<2 ? 45*t : 90));
        CHECK(a.uv.value[5]==.25f && a.uv.value[6]==.75f && a.uv.address==2);
        CHECK(a.uv.cycle[0]==(t<1 ? 0 : (t-1)/2) && a.uv.owner[2]);
        CHECK((a.uv.owner[0]!=0)==(t>=1) && a.uv.owner[0]!=a.uv.owner[2]);
    }
    NwMtlBinding base={.material=mtl,.tid=tids[2]}, out=base;
    items[3]=nw_texanim_instance_track(p,"image",&source,config,NULL);
    CHECK(nw_texanim_instance_time(items[0],0));
    CHECK(!nw_texanim_compose(items,4,id,&out) && !memcmp(&base,&out,sizeof(out)));
    nw_texanim_instance_drop(items[3]); items[3]=items[0];
    CHECK(nw_texanim_compose(items,4,id,&out)); /* Explicitly shared owner is not two writers. */
    out=base; CHECK(!nw_texanim_compose(items,3,0,&out) && !memcmp(&base,&out,sizeof(out)));
    nw_texanim_instance_stop(items[2]); out=base;
    CHECK(nw_texanim_compose(items,3,id,&out) && out.uv.value[4]==0 && out.uv.value[5]==.5f);
    for (int n=0; n<3; n++) nw_texanim_instance_stop(items[n]);
    out=base; CHECK(nw_texanim_compose(items,3,id,&out) && !out.uv.binding && out.tid==tids[2]);
    CHECK(!nw_texanim_compose(NULL,1,id,&out) && !nw_texanim_compose(items,3,id,NULL));
    NuTexAnimProgSysInit(); CHECK(nw_texanim_instance_time(items[1],4));
    out=base; CHECK(nw_texanim_compose(items,3,id,&out) && out.uv.value[0]==.5f);
    for (int n=0; n<3; n++) nw_texanim_instance_drop(items[n]);
    const char mip[]="ats 2 compositemip\ntime seconds\nmipmaps generated\nsequence image {\nframes 0..2\nstep 1\n}\n"
        "uv scroll {\noffset 1 0\n}\n";
    p=nw_texanim_from_memory("mip compose",mip,sizeof(mip)-1);
    items[0]=nw_texanim_instance_track(p,"image",&source,config,NULL);
    items[1]=nw_texanim_instance_track(p,"scroll",&source,config,NULL);
    CHECK(nw_texanim_instance_array(items[0],NULL) && nw_texanim_instance_array(items[1],items[0]));
    CHECK(nw_texanim_instance_texture(items[0])==nw_texanim_instance_texture(items[1]));
    out=base; CHECK(nw_texanim_compose(items,2,id,&out) && out.texture && out.uv.mips && out.layer==0);
    nw_texanim_instance_stop(items[0]); out=base;
    CHECK(nw_texanim_compose(items,2,id,&out) && out.texture==nw_texanim_instance_texture(items[1]) && out.layer==2);
    const char off[]="ats 2 off\ntime seconds\nmipmaps off\nuv x {\nrotation 10\n}\n";
    struct nutexanimprog_s *q=nw_texanim_from_memory("off",off,sizeof(off)-1);
    items[2]=nw_texanim_instance_track(q,"x",NULL,config,NULL); out=base;
    CHECK(!nw_texanim_compose(items,3,id,&out) && !memcmp(&out,&base,sizeof(out)));
    for (int n=0; n<3; n++) nw_texanim_instance_drop(items[n]);
    NuTexAnimProgSysInit(); mtl->tid=original;
    printf("host sizes: UV=%zu command=%zu material=%zu\n",sizeof(NwUV),sizeof(NwCmd),sizeof(NwMtlSnap));
}

struct nw_embed_file { const char *name; const unsigned char *data; size_t size; };
extern const struct nw_embed_file nw_opening_assets[];
extern const size_t nw_opening_assets_count;

/* ---- param tracks (script/curves.h), checked against the timeline formulas they replaced ---- */

static struct nutexanimprog_s *load_asset(const char *name)
{
    for (size_t n = 0; n < nw_opening_assets_count; n++) {
        if (strcmp(nw_opening_assets[n].name, name)) continue;
        struct nutexanimprog_s *p = nw_texanim_from_memory(name, nw_opening_assets[n].data, nw_opening_assets[n].size);
        if (!p) fprintf(stderr, "%s\n", nw_texanim_error()->message);
        return p;
    }
    fprintf(stderr, "%s is not embedded\n", name);
    return NULL;
}

/* Old subtitle timeline: reveal ramps over text_len, flash is 1 - reveal while it grows. Game-thread arithmetic (the
   EE's add, subtract and divide), as the curves and the code they replaced compute it. */
static void reference_subtitle(float time, float *reveal, float *flash)
{
    float in = nw_fpu_div(time, .45f);
    in = in < 0 ? 0 : in > 1 ? 1 : in;
    *reveal = in;
    *flash = in > 0 && in < 1 ? nw_fpu_sub(1.0f, in) : 0.0f;
}

/* Old meter timeline: fill .9 s, five .2 s blinks, rest .15 s, looping; a 1.02 s raised cosine. */
static float reference_meter(float time, float *flash)
{
    volatile float fill = .9f, blink = .2f, blinks = 5, rest = .15f;
    *flash = nw_fpu_sub(.5f, .5f * cosf(nw_fpu_div(time * 6.2831853f, 1.02f)));
    time = fmodf(time, fill + blinks * blink + rest);
    if (time < fill) return 15 * time / fill;
    time -= fill;
    if (time < blinks * blink) return fmodf(time, blink) < blink * .5f ? 15 : 0;
    return 0;
}

static uint32_t reference_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; return x ^ (x >> 16);
}

/* Old button timeline with blink 2.6: two sine pulses per 155 frames (2.6f * 60 rounded toward zero, as on the EE). */
static float reference_button(int32_t frame)
{
    int period = 155, slot = frame / period, at = frame % period;
    at -= (int)(reference_hash((uint32_t)slot * 2246822519u) % (uint32_t)(period / 3 + 1));
    float glow = 0;
    for (int k = 0; k < 2; k++) {
        float f = (float)(at - k * 14) / 9.0f;
        if (f >= 0 && f < 1) glow = sinf(f * 3.14159265f);
    }
    return .08f + 1.1f * glow;
}

/* The meter's step keys may land a float rounding away from the old modulo test, only on a 0.1 s edge. */
static bool near_tenth(float time)
{
    float t = fmodf(time, 2.05f) * 10;
    return fabsf(t - roundf(t)) < 1e-3f;
}

static void test_param_assets(void)
{
    float reveal = 9, flash = 9, level = 9, glow = 9, start = .5f, text_in = .25f, meter_in = .8f;
    const NwCurveOutput outputs[] = {
        {"subtitle.reveal", &reveal}, {"subtitle.flash", &flash}, {"meter.level", &level},
        {"meter.flash", &flash}, {"button.glow", &glow},
    };
    struct nutexanimprog_s *subtitle = load_asset("subtitle.ats"), *meter = load_asset("meter.ats"),
                           *button = load_asset("button.ats");
    CHECK(subtitle && meter && button);
    if (!subtitle || !meter || !button) return;
    NwCurveSet set;
    NwCurvePlayer player = {0};

    CHECK(nw_texanim_curves(subtitle, &set) && set.count == 2 && !strcmp(set.effect, "subtitle"));
    CHECK(nw_curve_player_init(&player, set, outputs, 5, &start, &text_in));
    for (int32_t i = 6000; i >= 0; i--) {
        float expected_reveal, expected_flash;
        reference_subtitle(nw_fpu_sub(nw_fpu_sub(nw_fpu_div((float)i, 60.0f), start), text_in), &expected_reveal,
                           &expected_flash);
        CHECK(nw_curve_player_sample(&player, i, 60) && reveal == expected_reveal && flash == expected_flash);
    }

    CHECK(nw_texanim_curves(meter, &set) && set.count == 2 && !strcmp(set.curves[0].name, "level"));
    int edges = 0;
    for (int i = 6000; i >= 0; i--) {
        float time = (float)i * .02f, expected_flash, expected = reference_meter(time, &expected_flash);
        float value = nw_curve_sample(&set.curves[0], (NwCurveTime){.seconds = time});
        CHECK(nw_curve_sample(&set.curves[1], (NwCurveTime){.seconds = time}) == expected_flash);
        if (value != expected) {
            CHECK(near_tenth(time));
            edges++;
        }
    }
    printf("param meter: %d of 6001 samples differ, all on a 0.1 s step edge\n", edges);
    CHECK(nw_curve_player_init(&player, set, outputs, 5, &start, &meter_in));
    CHECK(nw_curve_player_sample(&player, 60, 60) && level == 0 && flash == 0); /* Before origin + delay. */

    CHECK(nw_texanim_curves(button, &set) && set.count == 1);
    CHECK(nw_curve_player_init(&player, set, outputs, 5, NULL, NULL));
    for (int32_t frame = 30000; frame >= 0; frame--)
        CHECK(nw_curve_player_sample(&player, frame, 60) && glow == reference_button(frame));
    const int32_t boundaries[] = {154, 155, 156, 16777215, 16777216, 16777217, INT32_MAX - 1, INT32_MAX};
    for (size_t i = 0; i < SDL_arraysize(boundaries); i++)
        CHECK(nw_curve_player_sample(&player, boundaries[i], 60) && glow == reference_button(boundaries[i]));

    const NwCurveOutput unknown[] = {{"button.other", &glow}};
    CHECK(!nw_curve_player_init(&player, set, unknown, 1, NULL, NULL) && strstr(player.error, "button.glow"));
    CHECK(!nw_curve_player_init(&player, set, outputs, 5, &glow, NULL) && strstr(player.error, "clock input"));
    CHECK(!nw_curve_player_sample(&player, -1, 60));
    nw_curve_player_destroy(&player);
    NuTexAnimProgDestroy(subtitle); NuTexAnimProgDestroy(meter); NuTexAnimProgDestroy(button);
}

/* ---- Autonomous V2: scene lists, V1 replacement, game and cut clocks (no host) ---- */

extern s32 cutmovie, cutworldix, Level;
extern struct instNUGCUTSCENE_s *CutInst[];

static const char *const memory_files[][2] = {
    {"list.txt", ";; test list\ncut 3\n\tauto.ats\n\tcut.ats ; the cut-clocked title\n\nlevel 9\n\tlevel.ats\n"},
    {"auto.ats", "ats 2 autofx\ntime frames 60\nsequence s {\nframes 0..2\nstep 1\nplayback loop\n}\n"
                 "param level {\nkey 0 0 linear\nkey 4 1\n}\n"},
    {"level.ats", "ats 2 levelfx\ntime seconds\nsequence s {\nframes 0..1\nstep 1\n}\n"},
    {"cut.ats", "ats 2 title\ntime frames 30\nclock cut\nsequence fill {\nframes 0..2\nstep 1\nstart 10\n}\n"
                "param formed {\nkey 10 0 linear\nkey 12 1\n}\n"},
};

static bool memory_read(void *context, const char *path, NwTexAnimFile *data)
{
    (void)context;
    for (size_t i = 0; i < SDL_arraysize(memory_files); i++)
        if (!strcmp(path, memory_files[i][0])) {
            *data = (NwTexAnimFile){.bytes = memory_files[i][1], .size = strlen(memory_files[i][1])};
            return true;
        }
    return SDL_SetError("no memory file %s", path);
}

static void memory_release(void *context, NwTexAnimFile *data)
{
    (void)context;
    *data = (NwTexAnimFile){0};
}

static void count_curve(void *context, const char *effect, const NwCurve *curve)
{
    bool known = (!strcmp(effect, "autofx") && !strcmp(curve->name, "level")) ||
                 (!strcmp(effect, "title") && !strcmp(curve->name, "formed"));
    *(int *)context += known ? 1 : 100;
}

/* A model material bound to a listed effect and stepped only by NuTexAnimProcess. */
static struct nutexanim_s *bound_model(const char *effect, struct numtl_s *mtl, short tids[3])
{
    struct nutexanim_s *anim = NuTexAnimCreate(NULL, NuTexAnimProgFind((char *)effect), mtl, tids, 3);
    if (anim) NuTexAnimAddList(anim);
    return anim;
}

static void test_autonomous(const char *disc, struct numtl_s *mtl, short tids[3])
{
    s32 saved_movie = cutmovie, saved_world = cutworldix, saved_level = Level;
    struct instNUGCUTSCENE_s *saved_cuts[2] = {CutInst[0], CutInst[1]}, cuts[2];
    SDL_zeroa(cuts);
    CHECK(nw_texanim_scene_list("list.txt", (NwTexAnimIO){.read = memory_read, .release = memory_release}));

    /* Every listed param track, in all sections: the signals a host provides, then hands back to TexAnim. */
    int listed = 0;
    nw_texanim_scene_curves("list.txt", (NwTexAnimIO){.read = memory_read, .release = memory_release}, count_curve, &listed);
    CHECK(listed == 2 && !NuTexAnimProgFind("autofx") && !NuTexAnimProgFind("title"));
    float *level = nw_signal("autofx.level"), *formed = nw_signal("title.formed");
    CHECK(level && formed);
    if (!level || !formed) return;
    *level = *formed = -1;

    /* Each section loads with its scene only. */
    cutmovie = -1; Level = 9;
    NuTexAnimProgSysInit();
    CHECK(NuTexAnimProgFind("levelfx") && !NuTexAnimProgFind("autofx"));
    cutmovie = 3;
    NuTexAnimProgSysInit();
    CHECK(NuTexAnimProgFind("autofx") && NuTexAnimProgFind("title") && !NuTexAnimProgFind("levelfx"));

    /* A retail V1 with a listed name yields the V2. */
    struct nutexanimprog_s *v2 = NuTexAnimProgFind("title");
    char *path = NULL;
    SDL_asprintf(&path, "%sV1.ATS", disc);
    static const char v1[] = "Scriptname title\nStart:\n\tTex 0\nEnd\n";
    CHECK(SDL_SaveFile(path, v1, sizeof(v1) - 1));
    SDL_free(path);
    CHECK(NuTexAnimProgReadScript(NULL, "v1.ats") == v2 && NuTexAnimProgFind("title") == v2);

    /* Game clock: one frame per NuTexAnimProcess, looping 0 1 2. */
    (void)mtl;
    struct numtl_s *own = NuMtlCreate(1);
    own->tid = tids[0];
    NuMtlUpdate(own);
    struct nutexanim_s *game = bound_model("autofx", own, tids);
    CHECK(game != NULL);
    for (int step = 0; game && step < 5; step++) {
        NuTexAnimProcess();
        CHECK(nw_texanim_env_frame(game->env) == step % 3);
        CHECK(*level == (step < 4 ? (float)step / 4 : 1)); /* its param track, on the same frames */
    }

    /* Cut clock: the cutscene's frames from start 10, then held complete once another cut plays. */
    struct numtl_s *other = NuMtlCreate(1);
    other->tid = tids[0];
    NuMtlUpdate(other);
    struct nutexanim_s *cut = bound_model("title", other, tids);
    CHECK(cut != NULL);
    CutInst[0] = &cuts[0]; CutInst[1] = &cuts[1]; cutworldix = 0; cutmovie = 3;
    const float times[] = {0, 10, 10.9f, 11, 12.5f, 11};
    const int frames[] = {0, 0, 0, 1, 2, 1};
    for (size_t i = 0; cut && i < SDL_arraysize(times); i++) {
        cuts[0].time = times[i];
        NuTexAnimProcess();
        CHECK(nw_texanim_env_frame(cut->env) == frames[i]);
        float expected = times[i] <= 10 ? 0 : times[i] >= 12 ? 1 : (times[i] - 10) / 2;
        CHECK(*formed == expected); /* in CUT frames, the file's own units */
    }
    cutworldix = 1;
    NuTexAnimProcess();
    CHECK(cut && nw_texanim_env_frame(cut->env) == 2 && *formed == 1); /* finished with its cut */

    /* Reset restarts an autonomous effect; destroying the model drops its players. */
    if (game) {
        NuTexAnimEnvReset(game->env);
        NuTexAnimProcess();
        CHECK(nw_texanim_env_frame(game->env) == 0);
    }
    NuTexAnimRemoveList(game);
    NuTexAnimRemoveList(cut);
    if (game) NuTexAnimDestroy(game);
    if (cut) NuTexAnimDestroy(cut);
    CHECK(nw_texanim_scene_list(NULL, (NwTexAnimIO){0}));
    cutmovie = saved_movie; cutworldix = saved_world; Level = saved_level;
    CutInst[0] = saved_cuts[0]; CutInst[1] = saved_cuts[1];
    NuTexAnimProgSysInit();
    CHECK(!NuTexAnimProgFind("autofx") && !NuTexAnimProgFind("title"));
    printf("autonomous ATS: scene lists, V1 replacement, game and cut clocks, param signals checked\n");
}

/* ---- Reload: listed files read again in place, clocks carried, failures keep the old program ---- */

static const char *reload_files[][2] = {
    {"reload.txt", "cut 3\n\tgame.ats\n\tcut.ats\n"},
    {"game.ats", "ats 2 rgame\ntime frames 60\nsequence s {\nframes 0..2\nstep 1\nplayback loop\n}\n"},
    {"cut.ats", "ats 2 rcut\ntime frames 30\nclock cut\nsequence s {\nframes 0..2\nstep 1\n}\n"},
    {"new.ats", "ats 2 rnew\ntime seconds\nsequence s {\nframes 0..1\nstep 1\n}\n"},
};

static bool reload_read(void *context, const char *path, NwTexAnimFile *data)
{
    (void)context;
    for (size_t i = 0; i < SDL_arraysize(reload_files); i++)
        if (!strcmp(path, reload_files[i][0])) {
            *data = (NwTexAnimFile){.bytes = reload_files[i][1], .size = strlen(reload_files[i][1])};
            return true;
        }
    return SDL_SetError("no memory file %s", path);
}

static void test_reload(short tids[3])
{
    s32 saved_movie = cutmovie, saved_world = cutworldix;
    struct instNUGCUTSCENE_s *saved_cuts[2] = {CutInst[0], CutInst[1]}, cuts[2];
    SDL_zeroa(cuts);
    CHECK(nw_texanim_scene_list("reload.txt", (NwTexAnimIO){.read = reload_read, .release = memory_release}));
    cutmovie = 3;
    cutworldix = 0;
    CutInst[0] = &cuts[0];
    CutInst[1] = &cuts[1];
    NuTexAnimProgSysInit();
    struct numtl_s *a = NuMtlCreate(1), *b = NuMtlCreate(1);
    a->tid = b->tid = tids[0];
    NuMtlUpdate(a);
    NuMtlUpdate(b);
    struct nutexanim_s *game = bound_model("rgame", a, tids), *cut = bound_model("rcut", b, tids);
    CHECK(game && cut);
    if (!game || !cut) return;
    struct nutexanimprog_s *old = game->env->prog;
    cuts[0].time = 1.5f;
    NuTexAnimProcess();
    NuTexAnimProcess(); /* game: frames 0, 1; cut: frame 1 at 1.5 */
    CHECK(nw_texanim_env_frame(game->env) == 1 && nw_texanim_env_frame(cut->env) == 1);

    /* A saved edit: each effect wears its new program from where its clock was. */
    reload_files[1][1] = "ats 2 rgame\ntime frames 60\nsequence s {\nframes 0..2\nstep 2\nplayback loop\n}\n";
    reload_files[2][1] = "ats 2 rcut\ntime frames 30\nclock cut\nsequence s {\nframes 0..2\nstep 2\n}\n";
    nw_texanim_scene_reload();
    CHECK(game->env->prog != old && NuTexAnimProgFind("rgame") == game->env->prog);
    cuts[0].time = 3;
    NuTexAnimProcess(); /* game's third frame at step 2: 1 (restarted, it would be 0); cut at 3: 1 */
    CHECK(nw_texanim_env_frame(game->env) == 1 && nw_texanim_env_frame(cut->env) == 1);

    /* Once its cut is over the cut-clocked effect stays complete, reload or not. */
    cutworldix = 1;
    NuTexAnimProcess();
    nw_texanim_scene_reload();
    NuTexAnimProcess();
    CHECK(nw_texanim_env_frame(cut->env) == 2);

    /* A broken file, or one that renames the effect, keeps the program in use; a file new to the list loads. */
    struct nutexanimprog_s *kept = game->env->prog;
    reload_files[1][1] = "ats 2 rgame\ntime frames 60\nbogus\n";
    nw_texanim_scene_reload();
    CHECK(game->env->prog == kept && NuTexAnimProgFind("rgame") == kept);
    reload_files[1][1] = "ats 2 renamed\ntime frames 60\nsequence s {\nframes 0..2\nstep 1\n}\n";
    reload_files[0][1] = "cut 3\n\tgame.ats\n\tcut.ats\n\tnew.ats\n";
    nw_texanim_scene_reload();
    CHECK(game->env->prog == kept && NuTexAnimProgFind("rgame") == kept && !NuTexAnimProgFind("renamed"));
    CHECK(NuTexAnimProgFind("rnew") != NULL);
    NuTexAnimProcess();
    CHECK(nw_texanim_env_frame(game->env) >= 0);

    NuTexAnimRemoveList(game);
    NuTexAnimRemoveList(cut);
    NuTexAnimDestroy(game);
    NuTexAnimDestroy(cut);
    CHECK(nw_texanim_scene_list(NULL, (NwTexAnimIO){0}));
    cutmovie = saved_movie;
    cutworldix = saved_world;
    CutInst[0] = saved_cuts[0];
    CutInst[1] = saved_cuts[1];
    NuTexAnimProgSysInit();
    CHECK(!NuTexAnimProgFind("rgame") && !NuTexAnimProgFind("rnew"));
    printf("ATS reload: new programs in place, clocks carried, failures kept the old one\n");
}

/* ---- Scene effects: listed effects with only param tracks play with their scene, no model needed ---- */

static const char *scene_files[][2] = {
    {"scene.txt", "cut 4\n\tbolt.ats\n\tlate.ats\n\tramp.ats\n\tcutev.ats\n\ttrack.ats\n\tfx/glow.ats\n\tfx/none.ats\n"},
    {"bolt.ats", "ats 2 sbolt\ntime frames 60\nparam rise {\nkey 0 0 linear\nkey 10 1\nevent sbolt.hit at 1 after 0.5\n}\n"},
    {"late.ats", "ats 2 slate\ntime seconds\nstart sbolt.hit after 0.25\nparam level {\nkey 0 0 linear\nkey 1 1\n}\n"},
    {"ramp.ats", "ats 2 sramp\ntime frames 30\nclock cut\nparam in {\nkey 10 0 linear\nkey 20 1\n}\n"
                 "param down {\nkey 10 1 linear\nkey 20 0\nevent sramp.low below 0.5 after 0\n}\n"},
    {"cutev.ats", "ats 2 scut\ntime frames 30\nclock cut\nevent scut.open frame 12\nevent scut.next after scut.open 0.25\n"},
    {"track.ats", "ats 2 strack\ntime seconds\nparam a {\nstart sbolt.hit after 0.5\nkey 0 0 linear\nkey 1 1\n}\n"
                  "param b {\nkey 0 0 linear\nkey 1 1\n}\n"},
    {"fx/glow.ats", "ats 2 sglow\ntime seconds\nsequence glow {\nsource resource glow.png\nframes 0..0\nstep 1\n}\n"},
    {"fx/none.ats", "ats 2 snone\ntime seconds\nsequence glow {\nsource resource none.png\nframes 0..0\nstep 1\n}\n"},
};
static const uint8_t png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,3,0,0,0,2,8,3,0,0,
    0,170,170,150,40,0,0,0,6,80,76,84,69,17,29,43,71,83,97,92,88,215,225,0,0,0,2,116,
    82,78,83,1,255,66,138,19,244,0,0,0,16,73,68,65,84,120,156,99,96,96,100,96,96,100,96,4,
    0,0,18,0,4,14,137,175,91,0,0,0,0,73,69,78,68,174,66,96,130};

static bool scene_read(void *context, const char *path, NwTexAnimFile *data)
{
    (void)context;
    if (!strcmp(path, "fx/glow.png")) { /* beside fx/glow.ats */
        *data = (NwTexAnimFile){.bytes = (const char *)png, .size = sizeof(png)};
        return true;
    }
    for (size_t i = 0; i < SDL_arraysize(scene_files); i++)
        if (!strcmp(path, scene_files[i][0])) {
            *data = (NwTexAnimFile){.bytes = scene_files[i][1], .size = strlen(scene_files[i][1])};
            return true;
        }
    return SDL_SetError("no memory file %s", path);
}

static void test_scene_effects(void)
{
    s32 saved_movie = cutmovie, saved_world = cutworldix;
    struct instNUGCUTSCENE_s *saved_cuts[2] = {CutInst[0], CutInst[1]}, cuts[2];
    SDL_zeroa(cuts);
    static int ticks;
    CHECK(nw_texanim_scene_list("scene.txt", (NwTexAnimIO){.read = scene_read, .release = memory_release}));
    nw_texanim_scene_clock(&ticks, 60);
    cutmovie = 4;
    cutworldix = 0;
    CutInst[0] = &cuts[0];
    CutInst[1] = &cuts[1];
    NuTexAnimProgSysInit(); /* bolt first: its event exists before late starts from it */
    struct nutexanimprog_s *glow = NuTexAnimProgFind("sglow"); /* an external image beside its file; none.png is not */
    CHECK(glow && !strcmp(nw_texanim_sequence(glow)->source, "glow.png") && !NuTexAnimProgFind("snone"));
    const float *rise = nw_signal_find("sbolt.rise"), *hit = nw_signal_find("sbolt.hit");
    const float *level = nw_signal_find("slate.level"), *in = nw_signal_find("sramp.in");
    const float *a = nw_signal_find("strack.a"), *b = nw_signal_find("strack.b");
    CHECK(rise && hit && level && in && a && b && *hit == 1e9f);
    if (!rise || !hit || !level || !in || !a || !b) return;

    /* The host's clock: rise reaches 1 on tick 10, the event half a second later, late a quarter after that. */
    for (ticks = 0; ticks <= 10; ticks++) NuTexAnimProcess();
    CHECK(*rise == 1 && *hit == nw_fpu_add(nw_fpu_div(10, 60), .5f));
    for (; ticks < 55; ticks++) NuTexAnimProcess();
    CHECK(*level == 0 && *a == 0 && *b > .85f); /* a starts from the event, half a second after it; b at once */
    for (; ticks <= 115; ticks++) NuTexAnimProcess();
    CHECK(*level > .9f && *level <= 1 && *a > .7f && *a < .8f);

    /* clock cut: the cutscene's word, at its time; held complete once another plays. Frame events wait for its
       frame, the events after them follow; a below event for the first fall. */
    const float *open = nw_signal_find("scut.open"), *next = nw_signal_find("scut.next");
    const float *low = nw_signal_find("sramp.low");
    cuts[0].time = 5;
    nw_texanim_scene_cut(&cuts[0]);
    CHECK(!nw_signal_happened(open) && !nw_signal_happened(low));
    cuts[0].time = 15;
    nw_texanim_scene_cut(&cuts[0]);
    CHECK(*in == .5f && nw_signal_happened(open) && !nw_signal_happened(next) && !nw_signal_happened(low));
    cuts[1].time = 12;
    nw_texanim_scene_cut(&cuts[1]); /* not its cutscene */
    CHECK(*in == .5f);
    cuts[0].time = 16;
    nw_texanim_scene_cut(&cuts[0]);
    CHECK(nw_signal_happened(low));
    ticks += 20;
    NuTexAnimProcess();
    CHECK(nw_signal_happened(next) && *next == *open + .25f);
    cutworldix = 1;
    NuTexAnimProcess();
    CHECK(*in == 1);

    /* A reload rebuilds the player from the new file; the event that happened stays. */
    float happened = *hit;
    scene_files[2][1] = "ats 2 slate\ntime seconds\nstart sbolt.hit after 0.5\nparam level {\nkey 0 0 linear\nkey 1 1\n}\n";
    nw_texanim_scene_reload();
    NwCurveSet set;
    struct nutexanimprog_s *late = NuTexAnimProgFind("slate");
    CHECK(late && nw_texanim_curves(late, &set) && set.after && *set.after == .5f && *hit == happened);
    CHECK(NuTexAnimProgFind("sglow") && NuTexAnimProgFind("sglow") != glow); /* its image read again */
    NuTexAnimProcess();

    /* With the scene gone its effects go: nothing plays or writes. */
    CHECK(nw_texanim_scene_list(NULL, (NwTexAnimIO){0}));
    NuTexAnimProgSysInit();
    float before = *level;
    ticks += 100;
    NuTexAnimProcess();
    CHECK(!NuTexAnimProgFind("slate") && *level == before);
    nw_texanim_scene_clock(NULL, 0);
    cutmovie = saved_movie;
    cutworldix = saved_world;
    CutInst[0] = saved_cuts[0];
    CutInst[1] = saved_cuts[1];
    printf("scene effects: played without a model, on the host's and the cut's clocks, events, images, reload\n");
}

static void test_param_syntax(void)
{
    static const char *const bad[] = {
        "param x {\n}\n",                                              /* no generator */
        "param x {\nkey 0 0\nwave cosine 1\nrange 0 1\n}\n",           /* two generators */
        "param x {\nwave cosine 1\n}\n",                               /* wave without range */
        "param x {\npulse sine 2 14 9\nrange 0 1\n}\n",                /* pulse without period */
        "param x {\npulse sine 2 8 9\nperiod 100\nrange 0 1\n}\n",     /* width beyond spacing */
        "param x {\nkey 1 0\nkey 1 1\n}\n",                            /* keys must increase */
        "param x {\nkey 0 0 fancy\n}\n",                               /* unknown interpolation */
        "param x {\nkey 0 0\nevent a at 1\n}\n",                        /* event without its delay */
        "param x {\nkey 0 0\nevent a at 1 after -1\n}\n",               /* negative delay */
        "start\nparam x {\nkey 0 0\n}\n",                               /* start without its event */
        "start a later 1\nparam x {\nkey 0 0\n}\n",                     /* start EVENT after SECONDS */
        "param x {\nkey 0 0\nrange 0 1\n}\n",                          /* range belongs to wave/pulse */
        "param x {\nkey 0 0\nduration 2\n}\n",                         /* duration without loop */
        "param x {\nplayback loop\nduration 1\nkey 0 0\nkey 2 1\n}\n", /* loop shorter than keys */
        "param x {\npulse sine 1 2 1\nperiod 9\njitter 3/2 1\nrange 0 1\n}\n", /* delay beyond period */
        "param x {\nwave cosine 1\nrange 0 1\nrange 0 2\n}\n",         /* duplicate property */
        "param x {\nwave cosine 0\nrange 0 1\n}\n",                    /* zero period */
        "param x {\nkey 0 0\n}\nparam x {\nkey 0 1\n}\n",              /* duplicate name */
        "param x {\nkey 0 0\n",                                        /* unterminated */
        "param x\n",                                                   /* missing { */
        "param x {\nkey 0 0\nevent a under 1 after 0\n}\n",           /* at|below */
        "param x {\nkey 0 0\nstart\n}\n",                             /* a track's start without its event */
        "event a frame 10\nparam x {\nkey 0 0\n}\n",                  /* frame events need clock cut */
        "event a after\nparam x {\nkey 0 0\n}\n",                     /* after without its event */
        "event a after a\n",                                          /* follows itself */
        "event a when b\n",                                           /* frame or after */
    };
    char text[512];
    for (size_t i = 0; i < SDL_arraysize(bad); i++) {
        SDL_snprintf(text, sizeof(text), "ats 2 badparam\ntime seconds\n%s", bad[i]);
        CHECK(!nw_texanim_from_memory("bad param", text, strlen(text)) && !NuTexAnimProgFind("badparam"));
    }
    /* Params sit beside texture tracks; a texture-only effect has no curves. */
    static const char mixed[] = "ats 2 mixed\ntime frames 30\nsequence fill {\nframes 0..2\nstep 1\n}\n"
                                "param fade {\nkey 0 1 linear\nkey 30 0\n}\n";
    struct nutexanimprog_s *p = nw_texanim_from_memory("mixed", mixed, sizeof(mixed) - 1);
    NwCurveSet set;
    CHECK(p && nw_texanim_sequence(p) && nw_texanim_curves(p, &set) && set.count == 1);
    if (p) CHECK(nw_curve_sample(&set.curves[0], (NwCurveTime){.seconds = .5f}) == .5f); /* 15 of 30 frames. */
    if (p) NuTexAnimProgDestroy(p);
    static const char plain[] = "ats 2 plain\ntime frames 30\nsequence fill {\nframes 0..2\nstep 1\n}\n";
    p = nw_texanim_from_memory("plain", plain, sizeof(plain) - 1);
    CHECK(p && !nw_texanim_curves(p, &set));
    if (p) NuTexAnimProgDestroy(p);
    /* A file of events only; in clock cut, frame is the clock's own signal, not a param. */
    static const char events[] = "ats 2 evonly\ntime frames 30\nclock cut\nevent a frame 10 after 0.5\nevent b after a 0.25\n";
    p = nw_texanim_from_memory("events", events, sizeof(events) - 1);
    CHECK(p && nw_texanim_curves(p, &set) && !set.count && set.event_count == 2);
    if (p) CHECK(set.events[0].frame == 10 && set.events[0].after == .5f && !set.events[0].follows[0] &&
                 !strcmp(set.events[1].follows, "a") && set.events[1].after == .25f);
    if (p) NuTexAnimProgDestroy(p);
    static const char frame[] = "ats 2 badparam\ntime frames 30\nclock cut\nparam frame {\nkey 0 0\n}\n";
    CHECK(!nw_texanim_from_memory("frame param", frame, sizeof(frame) - 1));
}

float nw_smooth(float t, float t0, float t1);

/* Smooth keys give the opening's nw_smooth bit for bit, linear keys its old ramp; start and event are read into the
   set and the curve. */
static void test_smooth_events(void)
{
    static const char text[] = "ats 2 evfx\ntime frames 30\nstart bolt.test after 0.8\n"
                               "param out {\nkey 630 0 smooth\nkey 700 1\nevent out.done at 1 after 0.5\n}\n"
                               "param in {\nkey 495 0 linear\nkey 560 1\n}\n";
    struct nutexanimprog_s *p = nw_texanim_from_memory("evfx", text, sizeof(text) - 1);
    NwCurveSet set;
    CHECK(p && nw_texanim_curves(p, &set) && set.count == 2);
    if (!p) return;
    CHECK(set.start && !strcmp(set.start, "bolt.test") && set.after && *set.after == .8f);
    const NwCurve *out = &set.curves[0], *in = &set.curves[1];
    CHECK(!strcmp(out->event, "out.done") && out->event_at == 1 && out->event_after == .5f && !in->event[0]);
    unsigned mismatches = 0;
    for (int i = 0; i <= 50000; i++) {
        float t = 480 + (float)i * .005f; /* CUT frames 480..730 */
        float a = nw_curve_sample(out, (NwCurveTime){.seconds = t, .in_units = true}), b = nw_smooth(t, 630, 700);
        float c = nw_curve_sample(in, (NwCurveTime){.seconds = t, .in_units = true});
        float d = t <= 495 ? 0 : t >= 560 ? 1 : nw_fpu_div(nw_fpu_sub(t, 495), 560 - 495);
        mismatches += (unsigned)(memcmp(&a, &b, sizeof(a)) != 0) + (unsigned)(memcmp(&c, &d, sizeof(c)) != 0);
    }
    CHECK(mismatches == 0);
    NuTexAnimProgDestroy(p);
    static const char plain[] = "ats 2 nostart\ntime seconds\nparam x {\nkey 0 0\n}\n";
    p = nw_texanim_from_memory("nostart", plain, sizeof(plain) - 1);
    CHECK(p && nw_texanim_curves(p, &set) && !set.start);
    if (p) NuTexAnimProgDestroy(p);
    printf("smooth keys, start and event: %d samples bit for bit\n", 2 * 50001);
}

static void test_burst_players(const char *disc, struct numtl_s *mtl, short tids[3])
{
    const NwBurst recipe={.frames={0,1},.slot=90,.seed={2654435761u,40503},.delay={0,39},.flashes={2,5},.on={2,4},.off={2,5}};
    const char *body="frames 0 1\nalgorithm hash_bursts_1\nslot 90\nseed 2654435761 40503\nseed_mode %s\n"
        "delay 0 39\nflashes 2 5\non 2 4\noff 2 5\n";
    char text[2048], properties[512]; SDL_snprintf(properties,sizeof(properties),body,"fixed");
    /* The former opening lightning.ats, kept here as the burst reference. */
    static const char lightning[] = "ats 2 lightning\ntime frames 60\nsharing independent\nburst flashes {\n"
        "frames 0 1\nalgorithm hash_bursts_1\nslot 90\nseed 2654435761 40503\nseed_mode fixed\n"
        "delay 0 39\nflashes 2 5\non 2 4\noff 2 5\n}\n";
    SDL_snprintf(text,sizeof(text),"%s\nuv extra {\noffset linear {\nkey 0 0 0\nkey 1/3 1 0\n}\n}\n",lightning);
    NwTexAnimSequence sequence={.first=99}; NwTexAnimError error;
    CHECK(!read_v2(disc,text,&sequence,&error) && sequence.first==99);
    struct nutexanimprog_s *p=nw_texanim_from_memory("burst",text,strlen(text)); CHECK(p);
    if (!p) return;
    struct nutexanim_s source={.tids=tids,.numtids=3,.mtl=mtl}; NwTexAnimInstance *players[6];
    int original=mtl->tid; NwTexAnimConfig config={.clock=9,.variant=123};
    CHECK(!nw_texanim_sequence(p) && !nw_texanim_instance(p,&source,config,NULL));
    struct nutexanim_s *own=NuTexAnimCreate(NULL,p,mtl,tids,3); /* Bursts bind autonomously too. */
    CHECK(own && !nw_texanim_time(own,0));
    if (own) NuTexAnimDestroy(own);
    for (int n=0; n<6; n++) { config.channel=(uint32_t)n; players[n]=nw_texanim_instance_track(p,"flashes",&source,config,NULL); CHECK(players[n]); }
    const float chances[]={0,.5f,55,100}; unsigned comparisons=0;
    for (size_t c=0; c<SDL_arraysize(chances); c++) for (int n=0; n<6; n++) {
        CHECK(nw_texanim_instance_inputs(players[n],(NwTexAnimInputs){chances[c],true,true}));
        for (int tick=0; tick<6000; tick++) {
            bool on=false; CHECK(nw_burst_sample(&recipe,tick,(uint32_t)n,chances[c],0,&on));
            CHECK(nw_texanim_instance_clock_integer(players[n],tick,60));
            CHECK(nw_texanim_instance_frame(players[n])==(int)on && mtl->tid==original); comparisons++;
        }
        const int64_t edges[]={16777215,16777216,16777217,INT32_MAX-1,INT32_MAX};
        for (size_t k=0; k<SDL_arraysize(edges); k++) {
            bool on; CHECK(nw_burst_sample(&recipe,edges[k],(uint32_t)n,chances[c],0,&on));
            CHECK(nw_texanim_instance_clock_integer(players[n],edges[k],60) && nw_texanim_instance_frame(players[n])==(int)on);
        }
    }
    NwTexAnimInstance *a=players[0];
    CHECK(nw_texanim_instance_inputs(a,(NwTexAnimInputs){55,true,false}) && nw_texanim_instance_time(a,300) && nw_texanim_instance_frame(a)==0);
    CHECK(nw_texanim_instance_inputs(a,(NwTexAnimInputs){100,false,true}) && nw_texanim_instance_time(a,300) && !nw_texanim_instance_mask(a));
    NwMtlBinding out={.material=mtl,.tid=tids[2]};
    CHECK(nw_texanim_compose(&a,1,nw_texanim_binding(),&out) && out.tid==tids[2]);
    CHECK(!nw_texanim_instance_inputs(a,(NwTexAnimInputs){NAN,true,true}) && !nw_texanim_instance_mask(a));
    CHECK(!nw_texanim_instance_complete(a));
    NuTexAnimProgSysInit(); /* Recipe lives with the retired program, not in parser scratch. */
    CHECK(nw_texanim_instance_inputs(a,(NwTexAnimInputs){100,true,true}) && nw_texanim_instance_clock_integer(a,42,60));
    int frame=nw_texanim_instance_frame(a);
    CHECK(!nw_texanim_instance_clock_integer(a,INT64_MAX,1) && nw_texanim_instance_frame(a)==frame);
    CHECK(!nw_texanim_instance_clock_integer(a,1,0));
    for (int n=0; n<6; n++) nw_texanim_instance_drop(players[n]);

    SDL_snprintf(properties,sizeof(properties),body,"session");
    SDL_snprintf(text,sizeof(text),"ats 2 session\ntime frames 60\nburst flash {\n%s}\n",properties);
    p=nw_texanim_from_memory("session",text,strlen(text)); memset(text,0,sizeof(text));
    config=(NwTexAnimConfig){.sharing=NW_TEXANIM_SHARED,.clock=9,.channel=2,.variant=123};
    a=nw_texanim_instance_track(p,"flash",&source,config,NULL);
    NwTexAnimInstance *b=nw_texanim_instance_track(p,"flash",&source,config,a); CHECK(a && b==a); nw_texanim_instance_drop(b);
    config.variant=456; CHECK(!nw_texanim_instance_track(p,"flash",&source,config,a));
    b=nw_texanim_instance_track(p,"flash",&source,config,NULL); unsigned different=0;
    for (int tick=0; tick<6000; tick++) {
        bool want; CHECK(nw_burst_sample(&recipe,tick,2u^123u,100,0,&want));
        CHECK(nw_texanim_instance_clock_integer(a,tick,60) && nw_texanim_instance_frame(a)==(int)want);
        CHECK(nw_texanim_instance_clock_integer(b,tick,60)); different+=nw_texanim_instance_frame(a)!=nw_texanim_instance_frame(b);
        if (!(tick%90)) { /* Pause, reverse seek, restart: never consume RNG or change the token. */
            frame=nw_texanim_instance_frame(a); CHECK(nw_texanim_instance_clock_integer(a,0,60));
            CHECK(nw_texanim_instance_clock_integer(a,tick,60) && nw_texanim_instance_frame(a)==frame);
        }
    }
    CHECK(different>100); nw_texanim_instance_drop(a); nw_texanim_instance_drop(b); NuTexAnimProgSysInit();
    const char clockuv[]="ats 2 integer\ntime frames 3/2\nuv x {\noffset linear {\nkey 0 0 0\nkey 100 100 0\n}\n}\n";
    p=nw_texanim_from_memory("integer",clockuv,sizeof(clockuv)-1); config=(NwTexAnimConfig){.clock=3};
    a=nw_texanim_instance_track(p,"x",NULL,config,NULL); NwUV uv;
    for (int tick=-10; tick<100; tick++) {
        CHECK(nw_texanim_instance_clock_integer(a,tick,2) && nw_texanim_instance_uv(a,&uv));
        CHECK(uv.value[0]==(tick<0 ? 0 : floorf(tick*1.5f)*.5f));
    }
    CHECK(nw_texanim_instance_clock_integer(a,INT64_MAX,UINT32_MAX) && nw_texanim_instance_uv(a,&uv) && uv.value[0]==100);
    CHECK(nw_texanim_instance_clock_integer(a,INT64_MIN,3) && nw_texanim_instance_uv(a,&uv) && uv.value[0]==0);
    CHECK(!nw_texanim_instance_clock_integer(a,INT64_MIN,2) && !nw_texanim_instance_clock_integer(a,INT64_MAX,2));
    CHECK(!nw_texanim_instance_inputs(a,(NwTexAnimInputs){100,true,true}));
    nw_texanim_instance_drop(a); NuTexAnimProgSysInit();
    const char clockedge[]="ats 2 edge\ntime frames 3/2\nsequence x {\nstart 3221225471\nframes 0..2\nstep 1\n}\n";
    p=nw_texanim_from_memory("edge",clockedge,sizeof(clockedge)-1); a=nw_texanim_instance_track(p,"x",&source,config,NULL);
    CHECK(nw_texanim_instance_clock_integer(a,INT64_MAX,UINT32_MAX) && nw_texanim_instance_frame(a)==1);
    CHECK(!nw_texanim_instance_clock_integer(a,INT64_MAX,2) && nw_texanim_instance_frame(a)==1);
    nw_texanim_instance_drop(a); NuTexAnimProgSysInit();
    SDL_snprintf(text,sizeof(text),"ats 2 delay\ntime frames 60\nburst x {\n%sstart 1/2\nbefore restore\n}\n",properties);
    char *pair=strstr(text,"frames 0 1"); CHECK(pair); if (pair) memcpy(pair,"frames 2 0",10);
    p=nw_texanim_from_memory("delay",text,strlen(text)); a=nw_texanim_instance_track(p,"x",&source,config,NULL); CHECK(a);
    CHECK(!nw_texanim_instance_mask(a));
    for (int tick=1; tick<600; tick++) {
        bool on; CHECK(nw_burst_sample(&recipe,tick-1,config.channel^config.variant,100,0,&on));
        CHECK(nw_texanim_instance_clock_integer(a,tick,60) && nw_texanim_instance_frame(a)==(on ? 0 : 2));
    }
    nw_texanim_instance_drop(a); source.numtids=1;
    CHECK(!nw_texanim_instance_track(p,"x",&source,config,NULL)); source.numtids=3; NuTexAnimProgSysInit();
    const char *bad[]={"slot 0", "slot 4294967296", "slot -1", "algorithm unknown", "frames 0 2147483648", "on 0 1",
        "delay 5 4", "off 0 4294967295", "flashes 0 1", "seed_mode random", "slot 1/2", "duration 1", "playback loop", "finish hold", "source resource x"};
    for (size_t k=0; k<SDL_arraysize(bad); k++) {
        char key[32], invalid[512]; SDL_strlcpy(key,bad[k],sizeof(key)); *strchr(key,' ')=0;
        const char *at=strstr(properties,key), *end=at ? strchr(at,'\n') : NULL;
        if (at) SDL_snprintf(invalid,sizeof(invalid),"%.*s%s%s",(int)(at-properties),properties,bad[k],end);
        else SDL_snprintf(invalid,sizeof(invalid),"%s%s\n",properties,bad[k]);
        SDL_snprintf(text,sizeof(text),"ats 2 bad\ntime frames 60\nburst flash {\n%s}\n",invalid);
        CHECK(!nw_texanim_from_memory("bad burst",text,strlen(text)) && !NuTexAnimProgFind("bad"));
        CHECK(nw_texanim_error()->line>0 && nw_texanim_error()->message[0]);
    }
    SDL_snprintf(text,sizeof(text),"ats 2 duplicate\ntime frames 60\nburst x {\n%sslot 90\n}\n",properties);
    CHECK(!nw_texanim_from_memory("duplicate burst property",text,strlen(text)));
    const char budget[]="ats 2 budgetburst\ntime frames 60\nburst x {\nframes 0 1\nalgorithm hash_bursts_1\nslot 100000\n"
        "seed 1 1\ndelay 0 0\nflashes 5000 5000\non 1 1\noff 0 0\n}\n";
    p=nw_texanim_from_memory("budgetburst",budget,sizeof(budget)-1); a=nw_texanim_instance_track(p,"x",&source,config,NULL);
    CHECK(a); frame=nw_texanim_instance_frame(a);
    CHECK(!nw_texanim_instance_time(a,6000) && nw_texanim_instance_frame(a)==frame);
    nw_texanim_instance_drop(a); NuTexAnimProgSysInit();
    printf("burst ATS: %u grid-exact comparisons, fixed/session/transport/input checks passed\n",comparisons);
}

static void test_external_image(struct numtl_s *mtl)
{
    const char text[] = "ats 2 external\ntime seconds\nsequence x {\nsource resource image\nframes 0..0\nstep 1\n}\n";
    nw_tex_set_max_side(2048);
    NwTexture *image = nw_texture_png(png,sizeof(png)); CHECK(image);
    if (!image) return;
    int tid = nw_texture_tid(image); const NwGpuTex *gpu = nw_rndr_texture(tid);
    CHECK(gpu->tex.levels[0].width == 3 && gpu->tex.levels[0].height == 2);
    CHECK(!memcmp(gpu->tex.levels[0].texels,(uint8_t[]){17,29,43,1},4));
    CHECK(!nw_texanim_from_memory("external",text,sizeof(text)-1));
    CHECK(!nw_texanim_from_memory_resolved("external",text,sizeof(text)-1,image_resolver,NULL));
    CHECK(!NuTexAnimProgFind("external"));
    const char multi[]="ats 2 resources\ntime seconds\nsequence a {\nsource resource image\nframes 0..0\nstep 1\n}\n"
        "sequence b {\nsource resource image\nframes 0..0\nstep 1\n}\n";
    const char latefail[]="ats 2 resources\ntime seconds\nsequence a {\nsource resource image\nframes 0..0\nstep 1\n}\n"
        "sequence b {\nsource resource missing\nframes 0..0\nstep 1\n}\n";
    CHECK(!nw_texanim_from_memory_resolved("late resource failure",latefail,sizeof(latefail)-1,image_resolver,image));
    CHECK(!NuTexAnimProgFind("resources") && nw_rndr_texture(tid)==gpu);
    struct nutexanimprog_s *rp=nw_texanim_from_memory_resolved("two resources",multi,sizeof(multi)-1,image_resolver,image);
    NwTexAnimInstance *ra=nw_texanim_instance_track(rp,"a",NULL,(NwTexAnimConfig){.clock=1},NULL);
    NwTexAnimInstance *rb=nw_texanim_instance_track(rp,"b",NULL,(NwTexAnimConfig){.clock=1},NULL);
    CHECK(rp && ra && rb && nw_texanim_instance_tid(ra)==tid && nw_texanim_instance_tid(rb)==tid);
    NuTexAnimProgDestroy(rp); nw_texanim_instance_drop(ra);
    CHECK(nw_texanim_instance_complete(rb) && nw_texanim_instance_tid(rb)==tid);
    nw_texanim_instance_drop(rb); CHECK(nw_rndr_texture(tid)==gpu);
    struct nutexanimprog_s *p = nw_texanim_from_memory_resolved("external",text,sizeof(text)-1,image_resolver,image);
    CHECK(p && !nw_texanim_from_memory_resolved("duplicate",text,sizeof(text)-1,image_resolver,image));
    if (!p) { nw_texture_release(image); return; }
    NwTexAnimConfig config = {.clock=1};
    NwTexAnimInstance *a = nw_texanim_instance(p,NULL,config,NULL);
    NwTexAnimInstance *b = nw_texanim_instance(p,NULL,config,NULL);
    CHECK(a && b && nw_texanim_instance_tid(a) == tid && nw_texanim_instance_tid(b) == tid);
    CHECK(nw_texanim_instance_array(a,NULL) && !nw_texanim_instance_texture(a));
    const char miptext[]="ats 2 external-mip\ntime seconds\nmipmaps generated\nsequence x {\nsource resource image\nframes 0..0\nstep 1\n}\n";
    struct nutexanimprog_s *mp=nw_texanim_from_memory_resolved("mip PNG",miptext,sizeof(miptext)-1,image_resolver,image);
    NwTexAnimInstance *c=nw_texanim_instance(mp,NULL,config,NULL); CHECK(c && nw_texanim_instance_array(c,NULL));
    const NwGpuTex *mip=nw_texanim_instance_texture(c); NwUV uv;
    CHECK(mip && mip!=gpu && mip->tex.nlevels==2 && !mip->tex.layers && gpu->tex.nlevels==1);
    CHECK(nw_texanim_instance_uv(c,&uv) && uv.mips && uv.binding);
    nw_texanim_instance_drop(c); NuTexAnimProgDestroy(mp);
    short ignored = 0;
    struct nutexanim_s *anim = NuTexAnimCreate(NULL,p,mtl,&ignored,0);
    CHECK(anim && nw_texanim_time(anim,INT64_MAX) && mtl->tid == tid && anim->env->tids[0] == tid);
    nw_texture_release(image); NuTexAnimProgSysInit(); /* Catalog/registry gone; consumers survive. */
    CHECK(nw_texanim_instance_time(a,INT64_MIN) && nw_texanim_instance_frame(a) == 0);
    CHECK(drawn_texture(mtl) == gpu);
    NuTexAnimDestroy(anim); CHECK(mtl->tid == 0); nw_texanim_instance_drop(a);
    CHECK(nw_rndr_texture(tid) == gpu && nw_texanim_instance_clock(b,123,30));
    nw_texanim_instance_drop(b); CHECK(!nw_rndr_texture(tid));
    NwGpuJob *jobs = NULL; uint32_t cap = 0;
    uint32_t n = nw_gpu_take(0,&jobs,&cap);
    for (uint32_t i = 0; i < n; i++) CHECK(jobs[i].op != NW_GPU_FREE_TEX || jobs[i].obj != gpu);
    n = nw_gpu_take(UINT64_MAX,&jobs,&cap); bool freed = false;
    for (uint32_t i = 0; i < n; i++) if (jobs[i].op == NW_GPU_FREE_TEX && jobs[i].obj == gpu) freed = true;
    CHECK(freed); SDL_free(jobs);
    const char *bad[] = {"source resource image\nsource resource image", "source model 0", "source resource", "source resource image extra"};
    for (size_t i = 0; i < SDL_arraysize(bad); i++) {
        char invalid[256]; SDL_snprintf(invalid,sizeof(invalid),"ats 2 invalid\ntime seconds\nsequence x {\n%s\nframes 0..0\nstep 1\n}\n",bad[i]);
        CHECK(!nw_texanim_from_memory("invalid",invalid,strlen(invalid)));
    }
    const char invalid[] = "ats 2 invalid\ntime seconds\nsequence x {\nsource resource image\nframes 0..1\nstep 1\n}\n";
    CHECK(!nw_texanim_from_memory("invalid",invalid,sizeof(invalid)-1));
    mtl->tid = 0;
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
    SDL_asprintf(&path, "%sEDGES.ATS", disc);
    CHECK(SDL_SaveFile(path, edge_script, sizeof(edge_script) - 1));
    SDL_free(path);
    write_parser_fixture(disc);
    SDL_asprintf(&path, "%sV2.ATS", disc);
    CHECK(SDL_SaveFile(path, "", 0)); /* Reserve the path before the VFS directory scan. */
    SDL_free(path);
    CHECK(nw_vfs_init(&vfs, root));
    nw_shim_file_init(&vfs);
    NuFileInitEx(1, 1);
    test_parser_buffers();
    test_v2_sequences(disc);

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
    test_independent_envs(prog, tids);
    test_signal_edges(mtl, tids);
    NuTexAnimProgDestroy(prog);

    test_v2_runtime(disc, mtl, tids);
    test_instances(mtl, tids);
    test_array_players(mtl, tids);
    test_uv_program(disc,mtl,tids);
    test_playback(disc,mtl,tids);
    test_composition(mtl,tids);
    test_burst_players(disc,mtl,tids);
    test_param_assets();
    test_param_syntax();
    test_smooth_events();
    test_autonomous(disc,mtl,tids);
    test_reload(tids);
    test_external_image(mtl); /* publishes the texture limit the scene's image needs */
    test_scene_effects();

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

static unsigned legacy_rounding_cases;

static void check_cut_clock(struct nutexanim_s *anim, float time, float origin)
{
    float local = time - origin, frame = local / .6f;
    int legacy = frame <= 0 ? 0 : frame >= 12 ? 12 : (int)frame;
    int expected = local >= 8 ? 12 : 0;
    if (local > 0 && local < 8) {
        uint32_t bits;
        memcpy(&bits,&local,sizeof(bits));
        unsigned shift = 150 - ((bits >> 23) & 255);
        /* Independent rational reference: IEEE float mantissa * 5 / (3 * 2^shift). */
        uint64_t index = shift < 63 ? ((uint64_t)((bits & 0x7fffff) | 0x800000) * 5) / (UINT64_C(3) << shift) : 0;
        expected = index >= 12 ? 12 : (int)index;
    }
    if (legacy != expected) legacy_rounding_cases++;
    CHECK(nw_texanim_clock(anim, local, 30));
    if (anim->env->tex_ix != expected)
        fprintf(stderr,"CUT mismatch time %.9g origin %.9g local %.9g reference %.9g expected %d got %d\n",time,origin,local,frame,expected,anim->env->tex_ix);
    CHECK(anim->env->tex_ix == expected && anim->mtl->tid == anim->tids[expected]);
}

/* Uses the shipped ATS and the real 13-texture TITLE.GHG, after the unchanged V1 timing regression. */
static void test_cut_clock(struct nutexanim_s *anim)
{

    /* The host-clocked vortex (origin supplied by the caller): the legacy environment API's reference. */
    static const char vortex[] = "ats 2 vortex\ntime frames 30\nsequence fill {\nframes 0..12\nstep 0.6\n}\n";
    const char nul[] = "ats 2 x\0extra";
    CHECK(!nw_texanim_from_memory("nul",nul,sizeof(nul)-1));
    CHECK(!nw_texanim_from_memory("empty",vortex,0));
    CHECK(!nw_texanim_from_memory("large",vortex,(size_t)INT32_MAX+1));
    for (int i = 0; i < 24; i++) CHECK(!nw_texanim_from_memory("bad","ats 3 x",7));
    struct nutexanimprog_s *p = nw_texanim_from_memory("vortex",vortex,sizeof(vortex)-1);
    CHECK(p && nw_texanim_retain(p));
    if (!p) return;
    const NwTexAnimSequence *sequence = nw_texanim_sequence(p);
    CHECK(sequence && sequence->last == 12 && sequence->step_ticks == 3 && sequence->ticks_per_second == 150);
    NuTexAnimProgSysInit(); /* The catalog owns p even though no environment has bound it yet. */
    CHECK(!NuTexAnimProgFind("vortex") && nw_texanim_bind(anim,p));
    nw_texanim_release(p); /* From here, only the model protects the retired program. */
    const float origin = 864.4f, end = origin + 8;
    uint32_t first, last;
    memcpy(&first,&origin,sizeof(first));
    memcpy(&last,&end,sizeof(last));
    for (uint32_t bits = first; bits <= last; bits++) {
        float time;
        memcpy(&time,&bits,sizeof(time));
        check_cut_clock(anim,time,origin);
    }
    for (int index = 1; index <= 12; index++) {
        float boundary = (float)index * .6f;
        uint32_t center;
        memcpy(&center,&boundary,sizeof(center));
        for (uint32_t bits = center-64; bits <= center+64; bits++) {
            float time;
            memcpy(&time,&bits,sizeof(time));
            check_cut_clock(anim,time,0);
        }
    }
    for (int route = 0; route < 2; route++) {
        float time = route ? 800 : 1; /* Normal run and skip destination. */
        for (int tick = 0; tick < 2000 && time < origin+9; tick++, time += .6f)
            check_cut_clock(anim,time,origin);
    }
    const float seek[] = {872,864.4f,868,800,871,1,866};
    for (size_t i = 0; i < SDL_arraysize(seek); i++) check_cut_clock(anim,seek[i],origin);
    CHECK(nw_texanim_clock(anim,1e300,30) && anim->env->tex_ix == 12);
    CHECK(nw_texanim_clock(anim,-1e300,30) && anim->env->tex_ix == 0);
    CHECK(!nw_texanim_clock(anim,NAN,30) && !nw_texanim_clock(anim,INFINITY,30));
    CHECK(!nw_texanim_clock(anim,1,0) && !nw_texanim_clock(anim,1,INFINITY));
    CHECK(!nw_texanim_clock(anim,0,1e-310) && anim->env->tex_ix == 0 && anim->mtl->tid == anim->tids[0]);
    CHECK(nw_texanim_time(anim,INT64_MAX) && anim->env->tex_ix == 12); /* station/end */
    NuTexAnimEnvReset(anim->env);
    CHECK(nw_texanim_clock(anim,0,30) && anim->env->tex_ix == 0);
    CHECK(legacy_rounding_cases == 5); /* Explicitly approved V2 exact-time semantics. */
    printf("CUT vortex: %u positions, 1548 boundary samples, normal/skip/seek exact; %u legacy float edge cases\n",
           last-first+1,legacy_rounding_cases);
}

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
    test_cut_clock(logo);
    NuHGobjDestroy(h);
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
