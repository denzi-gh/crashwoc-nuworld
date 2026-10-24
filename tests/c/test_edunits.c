/* The runtime half of the editor units (port/units.toml `runtime`) on the imported disc: every level's file loads
   into the tables the game reads, as InitWorld loads it over the level scene, and the update InitWorld then runs
   leaves them finite. Usage: test_edunits edobj. Local only: exits 77 without NUWORLD_TEST_DATA. */
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

extern struct edobject_s ObjectPath[64];
extern struct edobjinstance_s ObjectInstance[64];
void NuRndrInitWorld(void);
void buildBitCountTable(void);
void edobjRegisterBaseScene(struct nugscn_s *scene);
void edobjObjectReset(void);
int edobjFileLoadObjects(char *file);
void edobjUpdateObjects(float dt);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

typedef struct Census {
    unsigned files, bad, versions[16], objects, resolved, waypoints, particles, sounds, nonfinite;
} Census;

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

static void edobj_level(const char *obj, struct nugscn_s *sc)
{
    int version, count;
    if (!header(obj, &version, &count) || version < 0 || version > 15) {
        fprintf(stderr, "retail: %s: no header\n", obj);
        census.bad++;
        return;
    }
    edobjRegisterBaseScene(sc);
    edobjObjectReset();
    if (!edobjFileLoadObjects((char *)obj)) {
        fprintf(stderr, "retail: %s does not load\n", obj);
        census.bad++;
        return;
    }
    unsigned resolved = 0, waypoints = 0, particles = 0, sounds = 0;
    for (int i = 0; i < count && i < 64; i++) {
        resolved += ObjectPath[i].instance != -1;
        waypoints += ObjectPath[i].num_waypoints;
        particles += ObjectPath[i].num_particles;
        sounds += ObjectPath[i].num_sounds;
    }
    edobjUpdateObjects(0.0f);
    unsigned nonfinite = 0;
    for (int i = 0; i < 64; i++) {
        nonfinite += ObjectInstance[i].id != -1 && !finite_mtx(&ObjectInstance[i].mtx);
    }
    printf("%s v%d objects=%d resolved=%u waypoints=%u particles=%u sounds=%u\n", obj, version, count, resolved,
           waypoints, particles, sounds);
    census.versions[version]++;
    census.objects += (unsigned)count;
    census.resolved += resolved;
    census.waypoints += waypoints;
    census.particles += particles;
    census.sounds += sounds;
    census.nonfinite += nonfinite;
}

/* On the game thread, per level file: its scene as world_scene[0], then the unit's load as InitWorld runs it. */
static int retail_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    size_t size = 0x2400000; /* the scene viewer's buffer */
    char *buffer = SDL_malloc(size);
    /* No platforms until the terrain loaders (#53): an empty set, as terraininit starts one. */
    const size_t terrain_size = 0x40000;
    void *terrain = SDL_malloc(terrain_size);
    const char *ext = ".OBJ";
    NuFileInitEx(1, 1);
    NuTexInit();
    NuMtlInit();
    buildBitCountTable();
    for (size_t i = 0; buffer && terrain && i < nw_disc_manifest.count; i++) {
        const NwManifestEntry *e = &nw_disc_manifest.entries[i];
        size_t n = strlen(e->path);
        if (n < 4 || SDL_strcasecmp(e->path + n - 4, ext) != 0 || (e->flags & NW_MANIFEST_SKIP)) {
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
        edobj_level(file, sc);
        nw_gscn_remove(sc);
        drain_gpu();
    }
    SDL_free(terrain);
    SDL_free(buffer);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2 || strcmp(argv[1], "edobj") != 0) {
        fprintf(stderr, "usage: test_edunits edobj\n");
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
    printf("TOTAL files=%u objects=%u resolved=%u waypoints=%u particles=%u sounds=%u nonfinite=%u versions",
           census.files, census.objects, census.resolved, census.waypoints, census.particles, census.sounds,
           census.nonfinite);
    for (int v = 0; v < 16; v++) {
        if (census.versions[v]) {
            printf(" v%d:%u", v, census.versions[v]);
        }
    }
    printf("\n");
    CHECK(state == NW_GAME_RETURNED && census.bad == 0 && census.nonfinite == 0);
    /* SNOW.OBJ names 6 specials SNOW.GSC does not have. */
    CHECK(census.files == 18 && census.objects == 373 && census.resolved == 367 && census.waypoints == 1020);
    CHECK(census.particles == 80 && census.sounds == 76);
    CHECK(census.versions[11] == 3 && census.versions[12] == 4 && census.versions[13] == 11);
    return failures ? 1 : 0;
}
