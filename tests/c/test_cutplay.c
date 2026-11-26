/* Actual CUT camera and GHG skeletal animation through the cut playback service. Local assets only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "port/cutplay.h"
#include "port/game.h"
#include "nucore/nufile.h"
#include "nu3d/nuanim.h"
#include "nu3d/numtl.h"
#include "nu3d/nutex.h"
#include "shim/shim.h"
#include "vfs/vfs.h"

void NuHGobjEvalAnim2__retail(struct NUHGOBJ_s *, struct nuanimdata2_s *, float, int, struct NUJOINTANIM_s *, struct numtx_s *);
void instNuGCutCamSysUpdate__retail(struct instNUGCUTSCENE_s *, float);
void nw_mul(struct numtx_s *, const struct numtx_s *, const struct numtx_s *);
void nw_affine_inverse(struct numtx_s *, const struct numtx_s *);
extern struct numtx_s cutscenecammtx;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,SDL_GetError()); return 1; } } while (0)
static char cut_heap[16 << 20], model_heap[16 << 20];
typedef struct {
    struct instNUGCUTSCENE_s scene;
    struct NUHGOBJ_s *model;
    struct NUGCUTCHAR_s *character;
    bool current;
} Fixture;
static struct instNUGCUTSCENE_s *scene(void *context, const char *id)
{
    return !strcmp(id,"logos") ? &((Fixture *)context)->scene : NULL;
}
static struct instNUGCUTSCENE_s *current(void *context)
{
    Fixture *f = context; return f->current ? &f->scene : NULL;
}
static bool pose_matches(NwCutPlayback *service, Fixture *f, float seconds, float origin, float delay)
{
    struct numtx_s base[256], result[256], expected[256], held[256];
    NuHGobjEvalAnim2__retail(f->model,f->character->joint_anim,960,0,NULL,base);
    memcpy(result,base,sizeof(base)); memcpy(expected,base,sizeof(base));
    float since = (seconds-origin)-delay;
    float t = 918.0f+(since > 0 ? since*36.0f : 0);
    if (t < 951) {
        NuHGobjEvalAnim2__retail(f->model,f->character->joint_anim,t,0,NULL,held);
        struct numtx_s inv, carry;
        nw_affine_inverse(&inv,&held[0]); nw_mul(&carry,&inv,&base[0]);
        for (unsigned j = 0; j < f->model->num_joints; j++) {
            const char *name = f->model->joints[j].name;
            bool panel = name && (!strncmp(name,"The_",4) || !strncmp(name,"Wrath_",6) || !strncmp(name,"Of_",3) ||
                                 !strncmp(name,"Cortex_",7) || !strncmp(name,"Dot_",4));
            if (panel) nw_mul(&expected[j],&held[j],&carry);
        }
    }
    return nw_cutplay_apply(service,f->model,960,result) && !memcmp(result,expected,f->model->num_joints*sizeof(*result));
}
int main(void)
{
    const char *root = getenv("NUWORLD_TEST_OPENING_DATA");
    if (!root) { puts("NUWORLD_TEST_OPENING_DATA is not set: skipped"); return 77; }
    nw_game_host_enter();
    NwVfs vfs; CHECK(nw_vfs_init(&vfs,root)); nw_shim_file_init(&vfs);
    NuFileInitEx(1,1); NuTexInit(); NuMtlInit();
    union variptr_u heap = {.charptr=cut_heap}, end = {.charptr=cut_heap+sizeof(cut_heap)};
    Fixture f = {0}; f.scene.def = NuGCutSceneLoad("levels\\b\\logos\\logosr.cut",&heap,&end); CHECK(f.scene.def);
    heap.charptr = model_heap; f.model = NuGHGRead(&heap,"levels\\b\\logos\\title.ghg"); CHECK(f.model);
    for (unsigned i = 0; i < f.scene.def->chardef->count; i++) {
        struct NUGCUTCHAR_s *c = &f.scene.def->chardef->chars[i];
        if (c->name && !strcmp(c->name,"title")) f.character = c;
    }
    CHECK(f.character && f.character->joint_anim); f.character->character = f.model;
    struct instNUGCUTCAM_s *cams = calloc(f.scene.def->camdef->ncutcams,sizeof(*cams)); CHECK(cams);
    struct instNUGCUTCAMSYS_s camera = {.current=0,.cams=cams}; f.scene.camsys = &camera;
    float origin = 0, delay = .25f, enabled = 1;
    static const char *const joints[] = {"The_*", "Wrath_*", "Of_*", "Cortex_*", "Dot_*"};
    const NwMotion view = {.id="view",.source="logos",.first=500,.last=700,.rate=36,.camera=0};
    const NwMotion letters = {.id="letters",.source="logos",.character="title",.joints=joints,.joint_count=5,
        .first=918,.last=951,.rate=36,.origin=&origin,.delay=&delay,.enabled=&enabled,.before_source="logos",.before=918,
        .camera=-1,.skeletal=true,.preserve_root=true};
    NwCutPlayback service = {0};
    nw_cutplay_init(&service,(NwCutPlaybackHost){.context=&f,.scene=scene,.current=current,
        .multiply=nw_mul,.inverse=nw_affine_inverse});
    volatile float tick_rate = 60;
    CHECK(nw_cutplay_play(&service,&letters,0));
    for (int ticks = 0; ticks <= 120; ticks++) {
        float now = (float)ticks/tick_rate;
        if (ticks == 20) CHECK(nw_cutplay_play(&service,&view,now));
        CHECK(nw_cutplay_update(&service,now));
        CHECK(pose_matches(&service,&f,now,origin,delay));
        struct numtx_s actual;
        CHECK(nw_cutplay_camera(&service,&actual) == (ticks >= 20));
        if (ticks >= 20) {
            float frame = 500.0f+(now-20.0f/tick_rate)*36.0f;
            instNuGCutCamSysUpdate__retail(&f.scene,frame);
            CHECK(!memcmp(&actual,&cutscenecammtx,sizeof(actual)));
        }
    }
    struct numtx_s sample;
    nw_cutplay_clear(&service); CHECK(!nw_cutplay_camera(&service,&sample) && service.clip_count == 0);
    CHECK(nw_cutplay_play(&service,&letters,0) && nw_cutplay_update(&service,.5f));
    enabled = 0; CHECK(pose_matches(&service,&f,2,origin,delay)); enabled = 1;
    f.current = true;
    struct numtx_s base[256], result[256];
    NuHGobjEvalAnim2__retail(f.model,f.character->joint_anim,918,0,NULL,base); memcpy(result,base,sizeof(base));
    CHECK(nw_cutplay_apply(&service,f.model,918,result) && !memcmp(base,result,f.model->num_joints*sizeof(*base)));
    camera.count = 1; CHECK(!nw_cutplay_camera_sample(&f.scene,0,500,&sample)); camera.count = 0;
    CHECK(!nw_cutplay_camera_sample(&f.scene,-2,500,&sample));
    CHECK(!nw_cutplay_camera_sample(&f.scene,0,f.scene.def->duration+1,&sample));
    nw_cutplay_destroy(&service);
    free(cams); nw_shim_file_init(NULL); nw_vfs_shutdown(&vfs);
    puts("cutplay: real CUT/GHG poses and camera passed"); return 0;
}
