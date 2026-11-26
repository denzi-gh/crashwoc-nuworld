#ifndef NW_PORT_NUTEXANM_H
#define NW_PORT_NUTEXANM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "script/curves.h"

/* Immutable tracks share the effect's rational clock. Zero-valued controls preserve C3 defaults. */
typedef enum { NW_TEXANIM_DEFAULT, NW_TEXANIM_SHARED, NW_TEXANIM_INDEPENDENT } NwTexAnimSharing;
typedef enum { NW_TEXANIM_ONCE, NW_TEXANIM_LOOP, NW_TEXANIM_PINGPONG } NwTexAnimPlayback;
typedef struct {
    char effect[32], track[32];
    char source[32]; /* Optional resource ID; empty selects the model's texture list. */
    int first, last;
    int64_t ticks_per_second, ticks_per_unit, step_ticks;
    NwTexAnimSharing sharing;
    int64_t start_ticks, duration_ticks;
    NwTexAnimPlayback playback;
    bool before_restore, finish_restore;
} NwTexAnimSequence;

typedef struct {
    int line; /* One-based physical source line; zero if opening failed. */
    char message[256];
} NwTexAnimError;

/* Reads through NuFPar/VFS. Failure leaves output unchanged; error may be NULL. */
bool nw_texanim_read_sequence(const char *path, NwTexAnimSequence *output, NwTexAnimError *error);
/* Local asset ticks, not video updates. Pure sampling; -1 if invalid or restored/inactive. */
int nw_texanim_sample_sequence(const NwTexAnimSequence *sequence, int64_t ticks);
struct nutexanim_s;
struct nutexanimprog_s;
struct NwHeapSide;
/* Binding is transactional; local ticks use the compiled asset's time base.
   The environment time API is texture-only; UV/mip tracks use the instance API below. */
bool nw_texanim_bind(struct nutexanim_s *anim, struct nutexanimprog_s *program);
bool nw_texanim_time(struct nutexanim_s *anim, int64_t ticks);
bool nw_texanim_own_list(struct NwHeapSide *side, struct nutexanim_s **list);
/* Also suitable as a heap cleanup callback; clears the owner's list pointer. */
void nw_texanim_release_list(void *list);
const NwTexAnimError *nw_texanim_error(void);
const NwTexAnimSequence *nw_texanim_sequence(struct nutexanimprog_s *program);
/* The ATS V2 effects each scene loads, from a text list read through io; NULL path disables it.
   "cut N" / "level N" open a section, indented lines name .ats files. Read at each NuTexAnimProgSysInit,
   before the retail scripts: a listed V2 replaces a retail V1 of the same name. */
/* A file the host reads for TexAnim (disc, overlay or built in); release gives it back. */
typedef struct { const char *bytes; size_t size; void *owner; } NwTexAnimFile;
typedef struct {
    void *context;
    bool (*read)(void *context, const char *path, NwTexAnimFile *file);
    void (*release)(void *context, NwTexAnimFile *file);
} NwTexAnimIO;
bool nw_texanim_scene_list(const char *path, NwTexAnimIO io);
/* Reads the current scene's listed files again: each effect that compiles replaces its program (nw_texanim_replace),
   one new to the list loads, one that fails is logged and the old program stays. */
void nw_texanim_scene_reload(void);
/* Listed effects with only param tracks are scene effects: played with their scene, no model needed. "clock cut"
   follows the cutscene; the others, and the events (port/signals.h), the host's clock if it gives one (ticks at rate a
   second, borrowed), else the frames the scene has played. */
void nw_texanim_scene_clock(const int *ticks, uint32_t rate);
/* The host's word that a cutscene has moved on: the clock cut scene effects playing in it follow its time now. */
struct instNUGCUTSCENE_s;
void nw_texanim_scene_cut(const struct instNUGCUTSCENE_s *scene);
/* Visits every param track of every effect a list names, in all sections: the signals (port/signals.h) they write
   while they play, EFFECT.TRACK. */
void nw_texanim_scene_curves(const char *list, NwTexAnimIO io,
                             void (*each)(void *context, const char *effect, const NwCurve *curve), void *context);
struct nutexanimenv_s;
/* An autonomous environment's current image frame; -1 before its first NuTexAnimProcess or without images. */
int nw_texanim_env_frame(const struct nutexanimenv_s *env);
/* The effect's param tracks (borrowed, live while the program is retained); false if it has none. */
bool nw_texanim_curves(struct nutexanimprog_s *program, NwCurveSet *set);
/* Registered result is borrowed; resource owners retain it across registry resets. */
struct nutexanimprog_s *nw_texanim_from_memory(const char *path, const void *text, size_t size);
struct NwTexture;
typedef struct NwTexture *(*NwTexAnimResolver)(void *context, const char *id); /* Borrowed result. */
struct nutexanimprog_s *nw_texanim_from_memory_resolved(const char *path, const void *text, size_t size,
                                                     NwTexAnimResolver resolve, void *context);
/* Compiles text in old's place: the materials wearing old wear the new program, autonomous ones going on from their
   clock, and old leaves the registry. On failure (or a new effect name) old stays as it was and NULL comes back.
   Borrowed result; the caller's references to old are its own to release. */
struct nutexanimprog_s *nw_texanim_replace(struct nutexanimprog_s *old, const char *path, const void *text, size_t size);
bool nw_texanim_retain(struct nutexanimprog_s *program);
void nw_texanim_release(struct nutexanimprog_s *program);
/* Local source units at rate units/second: floor to asset ticks, saturating at int64 limits. */
bool nw_texanim_clock(struct nutexanim_s *anim, double local, double rate);
typedef struct NwTexAnimInstance NwTexAnimInstance;
typedef struct {
    NwTexAnimSharing sharing;
    uint32_t clock; /* Stable caller-assigned clock ID, not a pointer or draw order. */
    int64_t origin; /* Asset ticks in that clock. */
    uint32_t channel, variant; /* Stable asset channel and host playback token; fixed mode ignores variant. */
} NwTexAnimConfig;
/* Copies model IDs, retains the program. External images are owned through that program; source may be NULL.
   Model textures must outlive consumers/queued draws.
   share=NULL creates a group; otherwise joins a shared group with identical configuration/source.
   Call once per binding, then drop each reference. No implicit global sharing/cache. */
NwTexAnimInstance *nw_texanim_instance(struct nutexanimprog_s *program, const struct nutexanim_s *source,
                                     NwTexAnimConfig config, NwTexAnimInstance *share);
/* Exact track names; NULL selects an unambiguous sequence/UV pair with C3 playback defaults. */
NwTexAnimInstance *nw_texanim_instance_track(struct nutexanimprog_s *program, const char *track,
    const struct nutexanim_s *source, NwTexAnimConfig config, NwTexAnimInstance *share);
enum { NW_TEXANIM_OFFSET=1, NW_TEXANIM_SCALE=2, NW_TEXANIM_ROTATION=4, NW_TEXANIM_PIVOT=8,
       NW_TEXANIM_ADDRESS=16, NW_TEXANIM_IMAGE=32 };
/* Active ownership, not a renderer binding; zero releases the selected properties. */
unsigned nw_texanim_instance_mask(const NwTexAnimInstance *instance);
/* UV loop/half-cycle identity: D2 must not interpolate across a wrap or reversal. */
int64_t nw_texanim_instance_uv_cycle(const NwTexAnimInstance *instance);
bool nw_texanim_claim(unsigned *owned, unsigned properties);
bool nw_texanim_instance_complete(NwTexAnimInstance *instance);
void nw_texanim_instance_stop(NwTexAnimInstance *instance); /* A subsequent time/clock call resumes. */
void nw_texanim_instance_drop(NwTexAnimInstance *instance);
bool nw_texanim_instance_time(NwTexAnimInstance *instance, int64_t ticks);
bool nw_texanim_instance_clock(NwTexAnimInstance *instance, double local, double rate);
/* Exact floor conversion; unrepresentable clocks fail without changing the player. */
bool nw_texanim_instance_clock_integer(NwTexAnimInstance *instance, int64_t value, uint32_t source_rate);
typedef struct { float chance; bool enabled, condition; } NwTexAnimInputs;
/* Update burst inputs for the next sample; affects all references to an explicitly shared player. */
bool nw_texanim_instance_inputs(NwTexAnimInstance *instance, NwTexAnimInputs inputs);
int nw_texanim_instance_frame(const NwTexAnimInstance *instance);
int nw_texanim_instance_tid(const NwTexAnimInstance *instance);
/* Prepare once before drawing: copies compatible model frames, optionally reuses another player's image.
   Incompatible sources keep their 2D binding; failure is reported, not silently downgraded. */
bool nw_texanim_instance_array(NwTexAnimInstance *instance, const NwTexAnimInstance *reuse);
struct NwGpuTex;
const struct NwGpuTex *nw_texanim_instance_texture(const NwTexAnimInstance *instance);
struct NwUV;
bool nw_texanim_instance_uv(const NwTexAnimInstance *instance, struct NwUV *output);
struct NwMtlBinding;
uint32_t nw_texanim_binding(void); /* Unique host identity; zero reports exhaustion. */
/* Compose into a base material snapshot transactionally. Prepare images once before sampling. */
bool nw_texanim_compose(NwTexAnimInstance *const *instances, size_t count, uint32_t binding,
                       struct NwMtlBinding *output);
#endif
