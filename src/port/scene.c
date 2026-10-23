#include "port/scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/gsc.h"
#include "nu3d/nucamera.h"
#include "nu3d/nufont.h"
#include "nu3d/nugobj.h"
#include "nu3d/nugscn.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "nu3d/nuvport.h"
#include "nucore/nuerror.h"
#include "nucore/nufile.h"
#include "numath/nutrig.h"
#include "port/nugscn.h"
#include "vfs/vfs.h"

#define NW_SCENE_BUFFER 0x2400000 /* main's superbuffer, doubled for the host's larger records */

/* Declared by no header. */
void NuPs2Init(void);
void NuPs2InitVU(void);
void NuPs2InitVideo(int width, int height, int psm, int zpsm, int format);
void NuPs2VideoSetPos(int x, int y);
void NuGScnUpdate(struct nugscn_s *scene, float dt);
void NuGScnRndr3(struct nugscn_s *scene);
void buildBitCountTable(void);
void NuRndrInitEx(int streamsize);
int NuRndrBeginScene(int target);
void NuRndrClear(int flags, int colour, float depth);
void NuRndrEndScene(void);
int NuRndrSwapScreen(int target);
void NuMtlAnimate(float dt);
void NuCameraSet(struct nucamera_s *cam);
struct nucamera_s *NuCameraCreate(void);
void RotateDirectionalLight(struct nuvec_s *v, int xrot, int yrot);
void SetLights(struct nucolour3_s *col0, struct nuvec_s *dir0, struct nucolour3_s *col1, struct nuvec_s *dir1,
               struct nucolour3_s *col2, struct nuvec_s *dir2, struct nuvec_s *ambient);
int NuGScnLoad(char **buffer, char *bufend, char *filename);
struct nugscn_s *NuGScnLoadFix(void);
void NuRndrInitWorld(void);

/* DrawWorld's lights (SetLevelLights' level branch), roots of the data image. */
extern struct nucolour3_s lcol[3];
extern struct nuvec_s ldir[3];
extern struct nuvec_s acol;

static SDL_Mutex *nw_scene_mutex;
static SDL_InitState nw_scene_init;
static NwSceneInfo nw_scene;
static NwSceneCam nw_scene_cam;
static char nw_scene_path[NW_VFS_PATH_MAX]; /* the next scene, "" for none */

/* Any thread: the scene's state is shared with the viewer. */
static void nw_scene_lock(void)
{
    if (SDL_ShouldInit(&nw_scene_init)) {
        nw_scene_mutex = SDL_CreateMutex();
        SDL_SetInitialized(&nw_scene_init, nw_scene_mutex != NULL);
    }
    SDL_LockMutex(nw_scene_mutex);
}

static int nw_scene_cmp(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* A point of an object placed by an instance (row vector times the row-major matrix). */
static void nw_scene_place(const struct numtx_s *m, const float p[3], float out[3])
{
    const float *r = &m->_00;
    for (int c = 0; c < 3; c++) {
        out[c] = p[0] * r[c] + p[1] * r[4 + c] + p[2] * r[8 + c] + r[12 + c];
    }
}

/* The camera behind the median visible instance, looking down at it (outliers and skydomes only set the far clip);
   returns the extent of the instances. */
static float nw_scene_frame(const struct nugscn_s *sc, NwSceneInfo *info)
{
    float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
    uint32_t total = (uint32_t)sc->numinstance, n = 0;
    float *at = SDL_malloc(((size_t)total + 1) * 3 * sizeof(float));
    memset(info, 0, sizeof(*info));
    for (uint32_t i = 0; i < total && at; i++) {
        const struct nuinstance_s *in = &sc->instances[i];
        if (!in->flags.visible || in->objid < 0 || in->objid >= sc->numgobj) {
            continue;
        }
        const struct nugobj_s *gobj = sc->gobjs[in->objid];
        const float origin[3] = { 0.0f, 0.0f, 0.0f };
        float w[3];
        nw_scene_place(&in->mtx, origin, w);
        for (int k = 0; k < 3; k++) {
            at[k * total + n] = w[k];
        }
        n++;
        for (int s = 0; s < gobj->ngobjs; s++) {
            info->tris += (uint32_t)gobj[s].tricount;
            for (int corner = 0; corner < 8; corner++) {
                const float p[3] = { corner & 1 ? gobj[s].max.x : gobj[s].min.x,
                                     corner & 2 ? gobj[s].max.y : gobj[s].min.y,
                                     corner & 4 ? gobj[s].max.z : gobj[s].min.z };
                nw_scene_place(&in->mtx, p, w);
                for (int k = 0; k < 3; k++) {
                    lo[k] = fminf(lo[k], w[k]);
                    hi[k] = fmaxf(hi[k], w[k]);
                }
            }
        }
    }
    float extent = 0.0f, spread = 0.0f, centre[3] = { 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < 3 && n > 0 && lo[0] <= hi[0]; k++) {
        float *axis = at + k * total;
        qsort(axis, n, sizeof(float), nw_scene_cmp);
        centre[k] = axis[n / 2];
        spread = fmaxf(spread, (axis[n * 9 / 10] - axis[n / 10]) * 0.5f);
        extent += (hi[k] - lo[k]) * (hi[k] - lo[k]) * 0.25f;
    }
    SDL_free(at);
    extent = fmaxf(sqrtf(extent), 10.0f);
    spread = fmaxf(spread, extent * 0.02f);
    info->objects = (uint32_t)sc->numgobj;
    info->instances = n;
    info->cam = (NwSceneCam){ { centre[0], centre[1] + spread * 0.3f, centre[2] - spread * 0.8f }, 0.0f, -0.3f };
    info->speed = spread * 0.25f;
    return extent;
}

/* The Nu camera matrix of a fly camera: rows right, up, forward (left-handed, +z forward) and the position. */
static void nw_scene_camera(const NwSceneCam *c, struct numtx_s *m)
{
    float cy = cosf(c->yaw), sy = sinf(c->yaw), cp = cosf(c->pitch), sp = sinf(c->pitch);
    const float f[3] = { sy * cp, sp, cy * cp }, r[3] = { cy, 0.0f, -sy };
    const float u[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
    float *o = &m->_00;
    for (int k = 0; k < 3; k++) {
        o[k] = r[k];
        o[4 + k] = u[k];
        o[8 + k] = f[k];
        o[12 + k] = c->pos[k];
    }
    o[3] = o[7] = o[11] = 0.0f;
    o[15] = 1.0f;
}

bool nw_scene_info(NwSceneInfo *info)
{
    nw_scene_lock();
    bool ready = nw_scene.seq > 0;
    *info = nw_scene;
    SDL_UnlockMutex(nw_scene_mutex);
    return ready;
}

void nw_scene_set_camera(const NwSceneCam *cam)
{
    nw_scene_lock();
    nw_scene_cam = *cam;
    SDL_UnlockMutex(nw_scene_mutex);
}

void nw_scene_load(const char *path)
{
    nw_scene_lock();
    SDL_strlcpy(nw_scene_path, path, sizeof(nw_scene_path));
    for (char *c = nw_scene_path; *c; c++) {
        *c = *c == '/' ? '\\' : *c;
    }
    SDL_UnlockMutex(nw_scene_mutex);
}

/* NULL when the scene has nothing to draw. */
static struct nugscn_s *nw_scene_open(struct nugscn_s *sc, char *buf, char *end, char *path, struct nucamera_s *cam,
                                      float farclip)
{
    if (sc) {
        nw_gscn_remove(sc);
        sc = NULL;
    }
    NuRndrInitWorld();
    cam->farclip = farclip;
    static char *cursor; /* the loader keeps its address */
    cursor = buf;
    int size = NuGScnLoad(&cursor, end, path);
    NwGsc g;
    if (size > 0 && nw_gsc_open(&g, (const uint8_t *)cursor, (size_t)size)) {
        bool drawn = !g.truncated && g.nobjects > 0;
        nw_gsc_close(&g);
        sc = drawn ? NuGScnLoadFix() : NULL;
    }
    NwSceneInfo info;
    memset(&info, 0, sizeof(info));
    if (sc) {
        cam->farclip = fmaxf(farclip, nw_scene_frame(sc, &info) * 4.0f);
        info.drawn = true;
    }
    nw_scene_lock();
    info.seq = nw_scene.seq + 1;
    nw_scene = info;
    nw_scene_cam = info.cam;
    SDL_UnlockMutex(nw_scene_mutex);
    return sc;
}

int nw_scene_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char *buf = SDL_malloc(NW_SCENE_BUFFER);
    if (!buf) {
        NuErrorProlog("scene.c", 0)("unable to allocate the scene buffer");
    }
    char *end = buf + NW_SCENE_BUFFER;
    NuPs2Init();
    NuFileInitEx(1, 1);
    NuPs2InitVideo(0x280, 0x100, 0, 0x31, 3);
    NuPs2VideoSetPos(0, 0);
    NuPs2InitVU();
    NuTrigInit();
    NuVpInit();
    NuTexInit();
    NuGobjInit();
    NuMtlInit();
    NuRndrInitEx(0x200000);
    NuFntClose();
    struct nucamera_s *cam = NuCameraCreate();
    float farclip = cam->farclip;
    /* The key lookup of the instance animations, which retail builds in the load screen's NuAnimDataLoadBuff before
       any scene. */
    buildBitCountTable();
    struct nugscn_s *sc = NULL;
    for (unsigned frame = 0;; frame++) {
        char path[NW_VFS_PATH_MAX];
        NwSceneCam c;
        nw_scene_lock();
        SDL_strlcpy(path, nw_scene_path, sizeof(path));
        nw_scene_path[0] = 0;
        SDL_UnlockMutex(nw_scene_mutex);
        if (path[0]) {
            sc = nw_scene_open(sc, buf, end, path, cam, farclip);
        }
        nw_scene_lock();
        c = nw_scene_cam;
        SDL_UnlockMutex(nw_scene_mutex);
        nw_scene_camera(&c, &cam->mtx);
        NuCameraSet(cam);
        NuMtlAnimate(0.02f);
        if (sc) {
            NuGScnUpdate(sc, 1.0f); /* in frames, as main's game loop */
        }
        RotateDirectionalLight(ldir, -0x2000, (int)(((frame % 400) << 16) / 400));
        SetLights(&lcol[0], &ldir[0], &lcol[1], &ldir[1], &lcol[2], &ldir[2], &acol);
        NuRndrBeginScene(-1);
        NuRndrClear(7, 0, 1.0f);
        if (sc) {
            NuGScnRndr3(sc);
        }
        NuRndrEndScene();
        NuRndrSwapScreen(-1);
    }
}
