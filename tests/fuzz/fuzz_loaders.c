/* libFuzzer target for the game's loaders and the port's decoders (no game data). The first byte picks the reader,
   the rest is its input:
     0  a .gsc scene (NuGScnRead)      1  a .ghg character (NuGHGRead)    2  a .cut cutscene (NuGCutSceneLoad)
     3  a .ani anim (NuAnimDataLoadBuff) 4  a .shd shadow set (ShadDataLoad) 5  a .ter terrain (ReadTerrain)
     6  the viewer's .gsc reader (nw_gsc_open)  7  a texture (nw_tex_decode)
     8  an ordering-table batch, 9  face-on sprites, 10  a skin list (nw_vif_otbatch, _sprites, _skin)
     11  a .vag sound (nw_vag_header, nw_vag_decode)  12  settings.toml (nw_settings_load)
   The loaders read files, so each input is written to a scratch folder first. A load's fatal error (NuErrorProlog)
   is the loader rejecting the file: the hook returns from it, leaving what that load held (retail never recovers),
   so leak detection is off for this target. Built with -DNUWORLD_FUZZ=ON; nuworld_fuzz_seeds writes its seeds. */
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio/vag.h"
#include "frontend/settings.h"
#include "gfx/gsc.h"
#include "gfx/packet.h"
#include "gfx/tex.h"
#include "gfx/vif.h"
#include "nu3d/nuanim.h"
#include "nu3d/nugscn.h"
#include "nu3d/nuhgobj.h"
#include "gamelib/gcutscn.h"
#include "gamelib/terrain.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nucore/nucoretypes.h"
#include "nucore/nufile.h"
#include "platform/paths.h"
#include "port/game.h"
#include "shim/shim.h"
#include "vfs/extract.h"
#include "vfs/vfs.h"

/* Declared by no header. */
struct shaddata_s *ShadDataLoad(char *file, union variptr_u *buffer);
s32 ReadTerrain(char *name, s32 situ, short **store, TerrainSetType *terrset);

enum {
    FUZZ_GSC,
    FUZZ_GHG,
    FUZZ_CUT,
    FUZZ_ANI,
    FUZZ_SHD,
    FUZZ_TER,
    FUZZ_VIEWER,
    FUZZ_TEX,
    FUZZ_OTBATCH,
    FUZZ_SPRITES,
    FUZZ_SKIN,
    FUZZ_VAG,
    FUZZ_SETTINGS,
    FUZZ_READERS
};
static const char *const fuzz_names[FUZZ_READERS] = { "FUZZ.GSC", "FUZZ.GHG", "FUZZ.CUT", "FUZZ.ANI", "FUZZ.SHD",
                                                      "FUZZ.TER" };

static char *scratch; /* <tmp>/nuworld-fuzz-loaders-<pid>/ */
static NwVfs vfs;
static jmp_buf rejected;
static _Alignas(16) char heap[0x400000];
static TerrainSetType terrain;

static void fuzz_die(const char *what)
{
    SDL_Log("fuzz_loaders: %s: %s", what, SDL_GetError());
    abort();
}

static void fuzz_fatal(const char *message)
{
    (void)message;
    longjmp(rejected, 1);
}

static void fuzz_save(const char *name, const void *data, size_t size)
{
    char *path = NULL;
    if (SDL_asprintf(&path, "%s%s/%s", scratch, NW_DISC_DIR, name) < 0 || !SDL_SaveFile(path, size ? data : "", size)) {
        fuzz_die(name);
    }
    SDL_free(path);
}

/* The render thread's side of the queued GPU jobs: the frees free the objects. */
static void fuzz_drain(void)
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        if (jobs[i].op == NW_GPU_FREE_MESH) {
            nw_vif_free(&((NwGpuMesh *)jobs[i].obj)->mesh);
        } else if (jobs[i].op == NW_GPU_FREE_SPRITES) {
            nw_vif_sprites_free(&((NwGpuSprites *)jobs[i].obj)->sprites);
        } else if (jobs[i].op == NW_GPU_FREE_SKIN) {
            nw_vif_skin_free(&((NwGpuSkin *)jobs[i].obj)->skin);
        }
        if (jobs[i].op >= NW_GPU_FREE_MESH && jobs[i].op != NW_GPU_FREE_TEX) {
            SDL_free(jobs[i].obj);
        }
    }
    SDL_free(jobs);
}

static void fuzz_cleanup(void)
{
    nw_shim_file_init(NULL);
    nw_vfs_shutdown(&vfs);
    nw_paths_remove_tree(scratch);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc, (void)argv;
    char *disc = NULL, *stamp = NULL;
    const char *tmp = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "TMPDIR");
    tmp = tmp && *tmp ? tmp : SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "TEMP");
    if (SDL_asprintf(&scratch, "%s/nuworld-fuzz-loaders-%llu/", tmp && *tmp ? tmp : "/tmp",
                     (unsigned long long)SDL_GetCurrentThreadID()) < 0 ||
        SDL_asprintf(&disc, "%s" NW_DISC_DIR, scratch) < 0 ||
        SDL_asprintf(&stamp, "%s/" NW_DISC_STAMP, disc) < 0) {
        fuzz_die("asprintf");
    }
    if (!nw_paths_remove_tree(scratch) || !SDL_CreateDirectory(disc) || !SDL_SaveFile(stamp, "", 0) ||
        !nw_vfs_init(&vfs, scratch)) {
        fuzz_die(scratch);
    }
    SDL_free(stamp);
    SDL_free(disc);
    nw_shim_file_init(&vfs);
    nw_game_on_fatal(fuzz_fatal);
    atexit(fuzz_cleanup);
    return 0;
}

/* One game load into the heap; a fatal error returns here. */
static void fuzz_load(int reader)
{
    union variptr_u buf, end;
    buf.charptr = heap;
    end.charptr = heap + sizeof(heap);
    char *name = SDL_strdup(fuzz_names[reader]);
    for (char *c = name; *c; c++) {
        *c = (char)SDL_tolower(*c);
    }
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    if (setjmp(rejected) == 0) {
        switch (reader) {
        case FUZZ_GSC:
            NuGScnRead(&buf.charptr, end.charptr, name);
            break;
        case FUZZ_GHG:
            NuGHGRead(&buf, name);
            break;
        case FUZZ_CUT:
            NuGCutSceneLoad(name, &buf, &end);
            break;
        case FUZZ_ANI:
            NuAnimDataLoadBuff(name, &buf, &end);
            break;
        case FUZZ_SHD:
            ShadDataLoad(name, &buf);
            break;
        case FUZZ_TER: {
            short *store = (short *)heap;
            name[strlen(name) - 4] = 0;
            ReadTerrain(name, 0, &store, &terrain);
            break;
        }
        }
    }
    SDL_free(name);
    fuzz_drain();
}

static void fuzz_decode(int reader, const uint8_t *data, size_t size)
{
    switch (reader) {
    case FUZZ_VIEWER: {
        NwGsc g;
        if (nw_gsc_open(&g, data, size)) {
            nw_gsc_close(&g);
        }
        break;
    }
    case FUZZ_TEX: {
        NwTex t;
        if (nw_tex_decode(&t, data, size)) {
            nw_tex_free(&t);
        }
        break;
    }
    case FUZZ_OTBATCH: {
        NwVifMesh m = { 0 };
        nw_vif_otbatch(&m, data, size);
        nw_vif_free(&m);
        break;
    }
    case FUZZ_SPRITES: {
        NwVifSprites s = { 0 };
        nw_vif_sprites(&s, data, size);
        nw_vif_sprites_free(&s);
        break;
    }
    case FUZZ_SKIN: {
        NwVifSkin s = { 0 };
        nw_vif_skin(&s, data, size);
        nw_vif_skin_free(&s);
        break;
    }
    case FUZZ_VAG: {
        NwVagHeader h;
        if (size >= 48 && nw_vag_header(&h, data, size)) {
            uint32_t count = (uint32_t)((size - 48) / NW_ADPCM_BLOCK);
            int16_t *pcm = SDL_malloc(((size_t)count + 1) * NW_ADPCM_SAMPLES * sizeof(*pcm));
            if (!pcm) {
                fuzz_die("malloc");
            }
            nw_vag_decode(data + 48, count, pcm);
            SDL_free(pcm);
        }
        break;
    }
    case FUZZ_SETTINGS: {
        NwSettings s;
        char *path = NULL;
        if (SDL_asprintf(&path, "%ssettings.toml", scratch) < 0 || !SDL_SaveFile(path, size ? (const void *)data : "", size)) {
            fuzz_die("settings.toml");
        }
        SDL_free(path);
        nw_settings_defaults(&s);
        nw_settings_load(&s, scratch);
        break;
    }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return 0;
    }
    int reader = data[0] % FUZZ_READERS;
    if (reader <= FUZZ_TER) {
        fuzz_save(fuzz_names[reader], data + 1, size - 1);
        fuzz_load(reader);
    } else {
        fuzz_decode(reader, data + 1, size - 1);
    }
    return 0;
}
