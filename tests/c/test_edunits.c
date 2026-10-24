/* The runtime half of the editor units (port/units.toml `runtime`) on the imported disc: every level's file loads
   into the tables the game reads, as InitWorld loads it over the level scene, and the update InitWorld then runs
   leaves them finite. Usage: test_edunits edobj|edanim|edgra. Local only: exits 77 without NUWORLD_TEST_DATA. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "data/image.h"
#include "gfx/packet.h"
#include "gfx/vif.h"
#include "gamelib/terrain.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nucore/nufile.h"
#include "port/game.h"
#include "port/nugscn.h"
#include "shim/shim.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

/* As in decomp/src/gamelib/edobj.c, which has no header; no pointers, so the host layout is retail's. */
struct edobject_s {
    int instance;
    float anim_speed;
    int oscillate;
    int repeating;
    float anim_pause;
    int num_waypoints;
    int total_time;
    int num_particles;
    int num_sounds;
    int anim_start_offset;
    int platinst;
    struct nuvec_s waypoints[8];
    float waypoint_speed[8];
    struct nuvec_s waypoint_rot[8];
    int waypoint_time[8];
    int switch_type;
    int switch_id;
    float switch_var;
    float switch_delay;
    char particle_name[8][16];
    int particle_type[8];
    int particle_rate[8];
    int particle_switch[8];
    struct nuvec_s particle_pos[8];
    short particle_rotz[8];
    short particle_roty[8];
    float prev_time;
    char sound_name[8][16];
    int sound_id[8];
    int sound_type[8];
    float sound_timing[8];
    struct nuvec_s sound_pos[8];
    float bouncy_player_grav;
    float bouncy_tension;
    float bouncy_damping;
};
_Static_assert(sizeof(struct edobject_s) * 64 == 0xFB00, "retail ObjectPath");

struct edobjinstance_s {
    struct numtx_s mtx;
    int id;
    struct nuinstflags_s flags;
    struct nuinstanim_s *anim;
    char pad4c[0x50 - 0x4C];
};

/* As in decomp/src/gamelib/edanim.c. */
struct edanimparam_s {
    int id;
    int particle_count;
    int sound_count;
    int switch_type;
    int switch_id;
    float switch_var;
    float switch_delay;
    char particle_name[8][16];
    int particle_type[8];
    int particle_rate[8];
    int particle_switch[8];
    struct nuvec_s particle_pos[8];
    short particle_rotz[8];
    short particle_roty[8];
    float sound_last_time;
    char sound_name[8][16];
    int sound_type[8];
    int sound_switch[8];
    float sound_timing[8];
    struct nuvec_s sound_pos[8];
    int bouncy_instance;
    float bouncy_player_grav;
    float bouncy_tension;
    float bouncy_damping;
};
_Static_assert(sizeof(struct edanimparam_s) * 64 == 0xB400, "retail AnimParams");

/* As in decomp/src/gamelib/edgra.c. */
struct grassclump_s {
    int type;
    int size;
    struct nuvec_s pos;
    float radius;
    float wind;
    int reactive;
    float height;
};
_Static_assert(sizeof(struct grassclump_s) * 64 == 0x900, "retail GrassClumps");

extern struct edobject_s ObjectPath[64];
extern struct edobjinstance_s ObjectInstance[64];
void NuRndrInitWorld(void);
void buildBitCountTable(void);
void edobjRegisterBaseScene(struct nugscn_s *scene);
void edobjObjectReset(void);
int edobjFileLoadObjects(char *file);
void edobjUpdateObjects(float dt);
extern struct edanimparam_s AnimParams[64];
void edanimRegisterBaseScene(struct nugscn_s *scene);
void edanimParamReset(void);
int edanimFileLoad(char *filename);
void edanimUpdateObjects(float dt);
extern struct grassclump_s GrassClumps[64];
extern int edgra_units_used, NuWindGCount, NuWindMtxIndex;
extern struct numtx_s NuWindMtxs[1024];
void edbitsRegisterBaseScene(struct nugscn_s *scene);
void edgraClumpsReset(void);
int edgraFileLoad(char *filename);
void NuWindUpdate(struct nuvec_s *pos);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

typedef struct Census {
    unsigned files, bad, versions[16], entries, n[4], nonfinite;
} Census;

/* A unit's load of one level file over the level scene; fills its four counts of the file's `count` entries. */
typedef struct Mode {
    const char *name, *ext, *count, *labels[4];
    bool (*level)(char *file, struct nugscn_s *sc, int count, unsigned n[4], unsigned *nonfinite);
} Mode;

static NwVfs retail_vfs;
static Census census;

static void drain_gpu(void)
{
    NwGpuJob *jobs = NULL;
    uint32_t cap = 0, n = nw_gpu_take(UINT64_MAX, &jobs, &cap);
    for (uint32_t i = 0; i < n; i++) {
        if (jobs[i].op == NW_GPU_FREE_MESH) {
            nw_vif_free(&((NwGpuMesh *)jobs[i].obj)->mesh);
        } else if (jobs[i].op == NW_GPU_FREE_SPRITES) {
            nw_vif_sprites_free(&((NwGpuSprites *)jobs[i].obj)->sprites);
        }
        if (jobs[i].op >= NW_GPU_FREE_MESH && jobs[i].op != NW_GPU_FREE_TEX) {
            SDL_free(jobs[i].obj);
        }
    }
    SDL_free(jobs);
}

/* The game's spelling of a manifest path; `ext` replaces the extension when set. */
static void game_path(char *out, size_t size, const char *path, const char *ext)
{
    SDL_strlcpy(out, path, size);
    if (ext) {
        SDL_strlcpy(strrchr(out, '.'), ext, size - (size_t)(strrchr(out, '.') - out));
    }
    for (char *c = out; *c; c++) {
        *c = *c == '/' ? '\\' : *c;
    }
}

/* The file's version and count words, as the loaders read them first. */
static bool header(const char *path, int *version, int *count)
{
    NwVfsFile *f = nw_vfs_open(&retail_vfs, path, NW_VFS_READ);
    int32_t w[2] = { 0, 0 };
    bool ok = f && nw_vfs_read(f, w, sizeof(w)) == sizeof(w);
    if (f) {
        nw_vfs_close(f);
    }
    *version = w[0];
    *count = w[1];
    return ok;
}

static bool finite_mtx(const struct numtx_s *m)
{
    const float *f = &m->_00;
    for (int i = 0; i < 16; i++) {
        if (!isfinite(f[i])) {
            return false;
        }
    }
    return true;
}

static bool edobj_level(char *file, struct nugscn_s *sc, int count, unsigned n[4], unsigned *nonfinite)
{
    edobjRegisterBaseScene(sc);
    edobjObjectReset();
    if (!edobjFileLoadObjects(file)) {
        return false;
    }
    for (int i = 0; i < count && i < 64; i++) {
        n[0] += ObjectPath[i].instance != -1;
        n[1] += ObjectPath[i].num_waypoints;
        n[2] += ObjectPath[i].num_particles;
        n[3] += ObjectPath[i].num_sounds;
    }
    edobjUpdateObjects(0.0f);
    for (int i = 0; i < 64; i++) {
        *nonfinite += ObjectInstance[i].id != -1 && !finite_mtx(&ObjectInstance[i].mtx);
    }
    return true;
}

static bool edanim_level(char *file, struct nugscn_s *sc, int count, unsigned n[4], unsigned *nonfinite)
{
    edanimRegisterBaseScene(sc);
    edanimParamReset();
    if (!edanimFileLoad(file)) {
        return false;
    }
    for (int i = 0; i < count && i < 64; i++) {
        n[0] += AnimParams[i].id != -1;
        n[1] += AnimParams[i].switch_type != 0;
        n[2] += AnimParams[i].particle_count;
        n[3] += AnimParams[i].sound_count;
    }
    edanimUpdateObjects(0.0f);
    for (int i = 0; i < count && i < 64; i++) {
        struct nuinstance_s *inst = AnimParams[i].id != -1 ? sc->specials[AnimParams[i].id].instance : NULL;
        *nonfinite += inst && inst->anim && !finite_mtx(&inst->anim->mtx);
    }
    return true;
}

/* edgraInitAllClumps scatters a clump's points into a local array of 256. */
static bool edgra_level(char *file, struct nugscn_s *sc, int count, unsigned n[4], unsigned *nonfinite)
{
    struct nuvec_s player = { 0.0f, 0.0f, 0.0f };
    edbitsRegisterBaseScene(sc);
    edgraClumpsReset();
    if (!edgraFileLoad(file)) {
        return false;
    }
    for (int i = 0; i < count && i < 64; i++) {
        n[0] += GrassClumps[i].type != -1;
        *nonfinite += GrassClumps[i].size > 256;
    }
    n[1] = (unsigned)edgra_units_used;
    n[2] = (unsigned)NuWindGCount;
    n[3] = (unsigned)NuWindMtxIndex;
    NuWindUpdate(&player);
    for (int i = 0; i < NuWindMtxIndex; i++) {
        *nonfinite += !finite_mtx(&NuWindMtxs[i]);
    }
    return true;
}

static const Mode modes[] = {
    { "edobj", ".OBJ", "objects", { "resolved", "waypoints", "particles", "sounds" }, edobj_level },
    { "edanim", ".ANM", "params", { "resolved", "switched", "particles", "sounds" }, edanim_level },
    { "edgra", ".GRA", "clumps", { "resolved", "points", "groups", "matrices" }, edgra_level },
};
static const Mode *mode;

/* On the game thread, per level file: its scene as world_scene[0], then the unit's load as InitWorld runs it. */
static int retail_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    size_t size = 0x2400000; /* the scene viewer's buffer */
    char *buffer = SDL_malloc(size);
    /* No platforms until the terrain loaders (#53): an empty set, as terraininit starts one. */
    const size_t terrain_size = 0x40000;
    void *terrain = SDL_malloc(terrain_size);
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    buildBitCountTable();
    for (size_t i = 0; buffer && terrain && i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
        size_t n = strlen(e->path);
        if (n < 4 || SDL_strcasecmp(e->path + n - 4, mode->ext) != 0 || (e->flags & NW_MANIFEST_SKIP)) {
            continue;
        }
        char file[NW_VFS_PATH_MAX], gsc[NW_VFS_PATH_MAX];
        game_path(file, sizeof(file), e->path, NULL);
        game_path(gsc, sizeof(gsc), e->path, ".GSC");
        census.files++;
        char *buf = buffer;
        NuRndrInitWorld();
        struct nugscn_s *sc = NuGScnRead(&buf, buffer + size, gsc);
        if (!sc) {
            fprintf(stderr, "retail: %s does not load\n", gsc);
            census.bad++;
            continue;
        }
        noterraininit();
        memset(terrain, 0, terrain_size);
        TerrainSetCur(terrain);
        int version, count;
        unsigned c[4] = { 0, 0, 0, 0 }, nonfinite = 0;
        if (!header(file, &version, &count) || version < 0 || version > 15 || count < 0 ||
            !mode->level(file, sc, count, c, &nonfinite)) {
            fprintf(stderr, "retail: %s does not load\n", file);
            census.bad++;
        } else {
            printf("%s v%d %s=%d %s=%u %s=%u %s=%u %s=%u\n", file, version, mode->count, count, mode->labels[0], c[0],
                   mode->labels[1], c[1], mode->labels[2], c[2], mode->labels[3], c[3]);
            census.versions[version]++;
            census.entries += (unsigned)count;
            for (int k = 0; k < 4; k++) {
                census.n[k] += c[k];
            }
            census.nonfinite += nonfinite;
        }
        nw_gscn_remove(sc);
        drain_gpu();
    }
    SDL_free(terrain);
    SDL_free(buffer);
    return 0;
}

int main(int argc, char *argv[])
{
    for (size_t i = 0; argc == 2 && i < SDL_arraysize(modes); i++) {
        mode = strcmp(argv[1], modes[i].name) == 0 ? &modes[i] : mode;
    }
    if (!mode) {
        fprintf(stderr, "usage: test_edunits edobj|edanim|edgra\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
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
    printf("TOTAL files=%u %s=%u %s=%u %s=%u %s=%u %s=%u nonfinite=%u versions", census.files, mode->count,
           census.entries, mode->labels[0], census.n[0], mode->labels[1], census.n[1], mode->labels[2], census.n[2],
           mode->labels[3], census.n[3], census.nonfinite);
    for (int v = 0; v < 16; v++) {
        if (census.versions[v]) {
            printf(" v%d:%u", v, census.versions[v]);
        }
    }
    printf("\n");
    CHECK(state == NW_GAME_RETURNED && census.bad == 0 && census.nonfinite == 0);
    const unsigned *v = census.versions, *c = census.n;
    if (mode->level == edobj_level) {
        /* SNOW.OBJ names 6 specials SNOW.GSC does not have. */
        CHECK(census.files == 18 && census.entries == 373 && c[0] == 367 && c[1] == 1020 && c[2] == 80 && c[3] == 76);
        CHECK(v[11] == 3 && v[12] == 4 && v[13] == 11);
    } else if (mode->level == edanim_level) {
        CHECK(census.files == 10 && census.entries == 35 && c[0] == 35 && c[1] == 0 && c[2] == 43 && c[3] == 0);
        CHECK(v[3] == 8 && v[4] == 2);
    } else if (mode->level == edgra_level) {
        CHECK(census.files == 8 && census.entries == 263 && c[0] == 263 && c[1] == 5178 && c[2] == 263);
        CHECK(c[3] == 5178 && v[1] == 1 && v[3] == 7);
    }
    return failures ? 1 : 0;
}
