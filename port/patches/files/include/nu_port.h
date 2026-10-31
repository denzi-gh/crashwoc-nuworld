#ifndef NU_PORT_H
#define NU_PORT_H

/* Store or read a T at p and step p past it; the EE spelling is gcc's cast-as-lvalue extension, which ISO C lacks. */
#ifdef NW_HOST
#define NW_PUT(T, p, v) (*(T *)(p) = (v), (p) = (void *)((T *)(p) + 1))
#define NW_GET(T, p) ((p) = (void *)((T *)(p) + 1), ((T *)(p))[-1])
#else
#define NW_PUT(T, p, v) *((T *)p)++ = v
#define NW_GET(T, p) *((T *)p)++
#endif

/* Ends a retail non-void function that falls off its end; empty on the EE, where callers ignore the value. */
#ifdef NW_HOST
#define NW_HOST_RETURN(v) return v
#else
#define NW_HOST_RETURN(v)
#endif

/* Alignment only the host needs (a header before 16-byte aligned data); the EE layout stays. */
#ifdef NW_HOST
#define NW_HOST_ALIGNED(n) __attribute__((aligned(n)))
#else
#define NW_HOST_ALIGNED(n)
#endif


#ifdef NW_HOST
void nw_ee_idle(void);
#define NW_HOST_IDLE() nw_ee_idle()
#else
#define NW_HOST_IDLE()
#endif

/* n records x just carved off a game heap: the host sets the cursor n * ps2size past x (retail's heap layout) and
 * points x at zeroed twins beside the heap (NuWorld's src/port/heap.c); the EE build checks sizeof(*x) == ps2size. */
#ifdef NW_HOST
void *nw_heap_twin(void *at, __SIZE_TYPE__ charge, __SIZE_TYPE__ size, __SIZE_TYPE__ align);
#define NW_HEAP_TWIN(x, cursor, n, ps2size) \
    ((cursor) = (void *)((char *)(x) + (__SIZE_TYPE__)(n) * (ps2size)), \
     (x) = nw_heap_twin((x), (__SIZE_TYPE__)(n) * (ps2size), (__SIZE_TYPE__)(n) * sizeof(*(x)), __alignof__(*(x))))
#else
#define NW_HEAP_TWIN(x, cursor, n, ps2size) ((void)sizeof(char[sizeof(*(x)) == (ps2size) ? 1 : -1]))
#endif

/* The record a load returns for the heap address at: at on the EE, its twin rec on the host. */
#ifdef NW_HOST
#define NW_HEAP_REC(at, rec) ((__typeof__(at))(rec))
#else
#define NW_HEAP_REC(at, rec) at
#endif

/* On the host, a pointer p aimed at the carved record at the heap address at is moved to its twin rec. */
#ifdef NW_HOST
#define NW_HEAP_AIM(p, at, rec) ((p) == (void *)(at) ? (void)((p) = (rec)) : (void)0)
#else
#define NW_HEAP_AIM(p, at, rec)
#endif

/* main.c's superbuffer: the loads inside it align to 0x80, so the host keeps retail's address modulo the alignment. */
#ifdef NW_HOST
void *nw_superbuffer_alloc(int size, char *file, int line);
#define NW_SUPERBUFFER_ALLOC(size, file, line) nw_superbuffer_alloc(size, file, line)
#else
#define NW_SUPERBUFFER_ALLOC(size, file, line) NuMemAllocFn(size, file, line)
#endif

/* A faithful out-of-object read: ps2 reads retail's layout, host reads the same bytes from the host layout. */
#ifdef NW_HOST
#define NW_RETAIL(ps2, host) (host)
#else
#define NW_RETAIL(ps2, host) (ps2)
#endif

/* obj->member[i], possibly past the array into the next members; the host indexes from obj to stay inside obj. */
#ifdef NW_HOST
#define NW_PAST(obj, member, i) \
    (((__typeof__(&(obj)->member[0]))((char *)(obj) + __builtin_offsetof(__typeof__(*(obj)), member)))[i])
#else
#define NW_PAST(obj, member, i) ((obj)->member[i])
#endif

/* EE-float lerp: range 0 (two keys at one time) gives the largest float where IEEE gives inf or NaN. */
#ifdef NW_HOST
float nw_ee_lerp(float a, float dt, float range, float b);
#define NW_EE_LERP(a, dt, range, b) nw_ee_lerp(a, dt, range, b)
#else
#define NW_EE_LERP(a, dt, range, b) ((a) + (dt) / (range) * ((b) - (a)))
#endif

/* (int)x as the EE's cvt.w.s: saturating where C's conversion is undefined. */
#ifdef NW_HOST
int nw_ee_s32(float x);
#define NW_EE_S32(x) nw_ee_s32(x)
#else
#define NW_EE_S32(x) ((int)(x))
#endif

/* a / b as the EE's div.s: by zero gives the largest float with the operands' combined sign, not inf or NaN. */
#ifdef NW_HOST
float nw_ee_fdiv(float a, float b);
#define NW_EE_DIV(a, b) nw_ee_fdiv(a, b)
#else
#define NW_EE_DIV(a, b) ((a) / (b))
#endif

/* Exempts from -fsanitize=function a function calling through pointers with retail's mismatched prototypes, which both
 * ABIs accept. */
#ifdef NW_HOST
#define NW_HOST_LOOSE_CALLS __attribute__((no_sanitize("function")))
#else
#define NW_HOST_LOOSE_CALLS
#endif

/* A local whose address retail uses after returning (a looping sound's position): the EE stack keeps its last values,
 * the host makes it static. */
#ifdef NW_HOST
#define NW_HOST_KEPT static
#else
#define NW_HOST_KEPT
#endif

/* Host-only initializer for a variable retail reads before setting it: the value retail finds there. */
#ifdef NW_HOST
#define NW_HOST_INIT(v) = (v)
#else
#define NW_HOST_INIT(v)
#endif

/* Hor+ widescreen (NuWorld's src/port/wide.c): drawing widens by nw_wide.k (1 is retail), logic keeps the 4:3 tests.
 * NW_WIDE_PVS draws an instance the PVS hid when the wider view needs it; NW_WIDE_ANIM does the same for its animation
 * except where that carries collision; NW_WIDE_SEEN narrows a draw result to retail's 4:3 frustum for logic;
 * nw_wide_vp moves the viewport origin for the pause shrink (paused 0-25) to retail's 4:3 area, nw_wide_vp_x is its
 * offset from the frame's left edge in 1/16 pixels; NW_WIDE_HUD gives a HUD model past retail's 4:3 picture the
 * perspective it has at the picture's edge. */
#ifdef NW_HOST
struct nuinstance_s;
struct numtx_s;
struct nw_wide_s {
    float k;       /* view width over retail's 4:3 width */
    float aspect;  /* h/w */
    int width;     /* GS frame width, 640 at 4:3 */
    float hud;     /* HUD x reach over retail's */
    float draw;    /* draw radii over retail's */
    float draw2;
    int pvs_all;
};
extern struct nw_wide_s nw_wide;
int nw_wide_anim(struct nuinstance_s *inst, int test);
void nw_wide_mark(void);
int nw_wide_seen(int drawn);
void nw_wide_vp(int paused);
int nw_wide_vp_x(void);
void nw_wide_hud(struct numtx_s *m);
#define NW_WIDE(ps2, host) (host)
#define NW_WIDE_SCALE(x, f) ((x) * (f))
#define NW_WIDE_ONLY(stmt) stmt
#define NW_WIDE_PVS(test) ((test) || nw_wide.pvs_all)
#define NW_WIDE_ANIM(inst, test) nw_wide_anim(inst, test)
#define NW_WIDE_SEEN(drawn) (nw_wide_mark(), nw_wide_seen(drawn))
#define NW_WIDE_HUD(m) nw_wide_hud(m)
#else
#define NW_WIDE(ps2, host) (ps2)
#define NW_WIDE_SCALE(x, f) (x)
#define NW_WIDE_ONLY(stmt)
#define NW_WIDE_PVS(test) (test)
#define NW_WIDE_ANIM(inst, test) (test)
#define NW_WIDE_SEEN(drawn) (drawn)
#define NW_WIDE_HUD(m)
#endif

/* Display-rate interpolation (NuWorld's src/gfx/interp.c): NW_INTERP_KEY names the object the following draws belong
 * to (0: none) so a skipped draw does not shift later keys; NW_INTERP_SNAP marks a cut, drawn without blending. */
#ifdef NW_HOST
void nw_interp_key(const void *key);
void nw_interp_snap(void);
#define NW_INTERP_KEY(p) nw_interp_key(p)
#define NW_INTERP_SNAP() nw_interp_snap()
#else
#define NW_INTERP_KEY(p)
#define NW_INTERP_SNAP()
#endif

/* A value tied to the 50/60 Hz logic tick (NuWorld's src/port/hz.c), PAL's on the PS2; NW_HZ_NTSC builds the PS2 code
 * with the NTSC values for NuWorld's tools/ntsc_diff.py to compare with the US release. */
#ifdef NW_HOST
extern int nw_hz;
#define NW_HZ(pal, ntsc) (nw_hz == 60 ? (ntsc) : (pal))
#elif defined(NW_HZ_NTSC)
#define NW_HZ(pal, ntsc) (ntsc)
#else
#define NW_HZ(pal, ntsc) (pal)
#endif

/* Whether the title may start an attract demo (a .ppd recording, one pad state per 50 Hz tick); always on the PS2. */
#ifdef NW_HOST
int nw_demos(void);
#define NW_DEMOS() nw_demos()
#else
#define NW_DEMOS() 1
#endif

#endif /* NU_PORT_H */
