#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nu3d/nustream.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"
#include "numath/nuvec4.h"

void NuMemCopy128(void *dst, void *src, int count);
float NuFsqrt(float x);
void NuMtxScaleVU0(struct numtx_s *m, struct nuvec_s *scale);
void NuMtxPreScaleVU0(struct numtx_s *m, struct nuvec_s *scale);
void NuMtxInvVU0(struct numtx_s *dst, struct numtx_s *src);
void NuMtxSetRotateXYZVU0(struct numtx_s *m, struct nuangvec_s *a);
void NuMtxMulVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b);
void NuMtxMulArrayVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b, int count);
void NuMtxMulRVU0(struct numtx_s *dst, struct numtx_s *a, struct numtx_s *b);
void NuVecMtxRotateVU0(struct nuvec_s *dst, struct nuvec_s *src, struct numtx_s *mtx);
void NuVecConvertToIntVU0(void *dst, struct nuvec_s *src);
void NuVec4MtxTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx);
void NuVec4MtxInvTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx);
void NuVec4ScaleVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale);
void NuVec4ScaleXYZVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale);
float XFormVuTriZ(float *depth, void *tri, struct numtx_s *mtx, int count);
int NuPlnLineVU0(struct nuplane_s *pln, struct nuvec_s *p0, struct nuvec_s *p1, struct nuvec_s *hit);
void NuCameraInitClipTestVU0(struct numtx_s *vpc, struct numtx_s *scissor, struct numtx_s *wm);
int NuCameraClipTestExtentsVU0(struct nuvec_s *min, struct nuvec_s *max);
void NuRndrStreamLink(struct rndrstream_s *stream);
void *vpDmaTag_CntEx(void *packet, uint32_t vif0, uint32_t vif1);
void *vpDmaTag_Next(void *packet, uint32_t addr);
void *vpDmaTag_RefEx(void *packet, uint32_t addr, uint32_t vif0, uint32_t vif1);
void *vpDmaTag_Call(void *packet, uint32_t addr);
void *vpDmaTag_CallEx(void *packet, uint32_t addr, uint32_t vif0, uint32_t vif1);
void *vpDmaTag_Ret(void *packet);
void *vpDmaTag_RetEx(void *packet, uint32_t vif0, uint32_t vif1);
void *vpDmaTag_Copy(void *packet, void *tag);
void *vpDmaTag_RepointCopy(void *packet, void *tag, uint32_t addr);
void *vpDmaTag_Close(void *packet);

extern u8 *rndrstream_free;

static struct numtx_s ident = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f * (1.0f + fabsf(a) + fabsf(b));
}

static int mtx_near(const struct numtx_s *a, const struct numtx_s *b)
{
    const float *x = &a->_00, *y = &b->_00;
    for (int i = 0; i < 16; i++) {
        if (!near(x[i], y[i])) {
            return 0;
        }
    }
    return 1;
}

static void rigid(struct numtx_s *m, int ax, int ay, int az, float tx, float ty, float tz)
{
    struct nuangvec_s a = { ax, ay, az };
    NuMtxSetRotateXYZ(m, &a);
    m->_30 = tx, m->_31 = ty, m->_32 = tz;
}

static void test_mtx(void)
{
    static const struct nuangvec_s angles[] = { { 0, 0, 0 }, { 0x1234, 0x4000, 0x9ABC }, { -0x2000, 0x7FFF, 0x10001 } };
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; i++) {
        struct nuangvec_s a = angles[i];
        struct numtx_s m, ref;
        NuMtxSetRotateXYZVU0(&m, &a);
        NuMtxSetRotateXYZ(&ref, &a);
        CHECK(mtx_near(&m, &ref));
    }

    struct numtx_s a, b, m, ref, arr[2], arr_a[2], arr_b[2];
    rigid(&a, 0x1111, 0x2222, 0x3333, 1.0f, -2.0f, 3.0f);
    rigid(&b, 0x0F00, 0x7000, 0xC000, -4.0f, 5.0f, 0.5f);

    NuMtxMulVU0(&m, &a, &b);
    NuMtxMul(&ref, &a, &b);
    CHECK(mtx_near(&m, &ref));
    m = a;
    NuMtxMulVU0(&m, &m, &b);
    CHECK(mtx_near(&m, &ref));

    arr_a[0] = a, arr_a[1] = b, arr_b[0] = b, arr_b[1] = a;
    NuMtxMulArrayVU0(arr, arr_a, arr_b, 2);
    CHECK(mtx_near(&arr[0], &ref));
    NuMtxMul(&ref, &b, &a);
    CHECK(mtx_near(&arr[1], &ref));

    NuMtxMulRVU0(&m, &a, &b);
    NuMtxMulR(&ref, &a, &b);
    CHECK(mtx_near(&m, &ref));

    NuMtxInvVU0(&m, &a);
    NuMtxInv(&ref, &a);
    CHECK(mtx_near(&m, &ref));
    NuMtxMulVU0(&m, &m, &a);
    CHECK(mtx_near(&m, &ident));

    struct nuvec_s s = { 2.0f, 3.0f, 4.0f };
    m = a;
    NuMtxScaleVU0(&m, &s);
    CHECK(m._00 == a._00 * 2.0f && m._11 == a._11 * 3.0f && m._32 == a._32 * 4.0f && m._03 == a._03);
    m = a;
    NuMtxPreScaleVU0(&m, &s);
    CHECK(m._02 == a._02 * 2.0f && m._10 == a._10 * 3.0f && m._22 == a._22 * 4.0f && m._30 == a._30);
}

static void test_vec(void)
{
    struct numtx_s m;
    rigid(&m, 0x1111, 0x2222, 0x3333, 1.0f, -2.0f, 3.0f);

    struct nuvec_s v = { 1.5f, -2.5f, 7.0f }, r, ref;
    NuVecMtxRotateVU0(&r, &v, &m);
    NuVecMtxRotate(&ref, &v, &m);
    CHECK(near(r.x, ref.x) && near(r.y, ref.y) && near(r.z, ref.z));

    struct nuvec4_s v4 = { 1.5f, -2.5f, 7.0f, 1.0f }, r4, ref4, back;
    NuVec4MtxTransformVU0(&r4, &v4, &m);
    NuVec4MtxTransform(&ref4, &v4, &m);
    CHECK(near(r4.x, ref4.x) && near(r4.y, ref4.y) && near(r4.z, ref4.z) && near(r4.w, ref4.w));
    NuVec4MtxInvTransformVU0(&back, &r4, &m);
    CHECK(near(back.x, v4.x) && near(back.y, v4.y) && near(back.z, v4.z) && back.w == r4.w);

    NuVec4ScaleVU0(&r4, &v4, 2.0f);
    CHECK(r4.x == 3.0f && r4.y == -5.0f && r4.z == 14.0f && r4.w == 2.0f);
    NuVec4ScaleXYZVU0(&r4, &v4, 2.0f);
    CHECK(r4.x == 3.0f && r4.y == -5.0f && r4.z == 14.0f && r4.w == 1.0f);

    int out[3];
    struct nuvec_s f = { -2.75f, 1e20f, -1e20f };
    NuVecConvertToIntVU0(out, &f);
    CHECK(out[0] == -2 && out[1] == 0x7FFFFFFF && out[2] == (int)0x80000000);

    CHECK(NuFsqrt(16.0f) == 4.0f && NuFsqrt(-4.0f) == 2.0f);
}

static void test_tri_z(void)
{
    float buf[2][40] = { { 0 } };
    for (int t = 0; t < 2; t++) {
        for (int v = 0; v < 3; v++) {
            buf[t][4 + v * 12 + 2] = (float)(t * 10 + v + 1);
        }
    }
    struct numtx_s m = ident;
    m._32 = 0.5f;
    float depth[2] = { 0.0f, 0.0f };
    float zmin = XFormVuTriZ(&depth[1], buf[1], &m, 2);
    CHECK(depth[1] == (11.5f + 12.5f + 13.5f) * 3.0f);
    CHECK(depth[0] == (1.5f + 2.5f + 3.5f) * 3.0f);
    CHECK(zmin == depth[0]);
}

static void test_plane(void)
{
    struct nuplane_s pln = { 0.0f, 1.0f, 0.0f, -1.0f };
    struct nuvec_s p0 = { 0.0f, 0.0f, 0.0f }, p1 = { 4.0f, 2.0f, -2.0f }, hit = { 9.0f, 9.0f, 9.0f };
    CHECK(NuPlnLineVU0(&pln, &p0, &p1, &hit) == 1);
    CHECK(near(hit.x, 2.0f) && near(hit.y, 1.0f) && near(hit.z, -1.0f));
    p1.y = 0.5f;
    hit.x = 9.0f;
    CHECK(NuPlnLineVU0(&pln, &p0, &p1, &hit) == 0 && hit.x == 9.0f);
}

static void test_clip(void)
{
    struct numtx_s id = ident, wm = ident;
    NuCameraInitClipTestVU0(&id, &id, NULL);
    struct nuvec_s min = { -0.5f, -0.5f, -0.5f }, max = { 0.5f, 0.5f, 0.5f };
    CHECK(NuCameraClipTestExtentsVU0(&min, &max) == 0);
    min.x = 2.0f, max.x = 3.0f;
    CHECK(NuCameraClipTestExtentsVU0(&min, &max) == (0x01 << 8 | 0x01));
    min.x = 0.0f;
    CHECK(NuCameraClipTestExtentsVU0(&min, &max) == 0x01 << 8);
    wm._30 = -2.5f;
    NuCameraInitClipTestVU0(&id, &id, &wm);
    min.x = 2.0f;
    CHECK(NuCameraClipTestExtentsVU0(&min, &max) == 0);
}

static void test_dma(void)
{
    _Alignas(16) uint32_t p[32];
    uint32_t *q;

    memset(p, 0xAA, sizeof p);
    q = vpDmaTag_CntEx(p, 0x11, 0x22);
    CHECK(q == p + 4 && p[0] == 0x10000000u && p[1] == 0 && p[2] == 0x11 && p[3] == 0x22);
    q[0] = 1, q[1] = 2, q[2] = 3;
    q = vpDmaTag_Close(q + 3);
    CHECK(q == p + 8 && p[0] == 0x10000001u && p[7] == 0);

    vpDmaTag_Next(p, 0x1234);
    CHECK(p[0] == 0x20000000u && p[1] == 0x1234 && p[2] == 0 && p[3] == 0);
    vpDmaTag_RefEx(p, 0x5678, 7, 0x6C098003);
    CHECK(p[0] == 0x30000000u && p[1] == 0x5678 && p[2] == 7 && p[3] == 0x6C098003);
    vpDmaTag_Call(p, 0x40);
    CHECK(p[0] == 0x50000000u && p[1] == 0x40 && p[2] == 0 && p[3] == 0);
    vpDmaTag_CallEx(p, 0x40, 1, 2);
    CHECK(p[0] == 0x50000000u && p[1] == 0x40 && p[2] == 1 && p[3] == 2);
    vpDmaTag_Ret(p);
    CHECK(p[0] == 0x60000000u && p[1] == 0 && p[2] == 0 && p[3] == 0);
    vpDmaTag_RetEx(p, 3, 4);
    CHECK(p[0] == 0x60000000u && p[1] == 0 && p[2] == 3 && p[3] == 4);

    q = vpDmaTag_Copy(p + 4, p);
    CHECK(q == p + 8 && memcmp(p + 4, p, 16) == 0);
    q = vpDmaTag_RepointCopy(p + 8, p, 0x99);
    CHECK(q == p + 12 && p[8] == p[0] && p[9] == 0x99 && p[10] == 3 && p[11] == 4);
    q = vpDmaTag_Close(p + 13);
    CHECK(q == p + 16 && (p[0] & 0xFFFF) == 3);
}

static void test_stream_link(void)
{
    _Alignas(16) u8 buf[128];
    struct rndrstream_s s;
    u8 *saved = rndrstream_free;

    memset(&s, 0, sizeof s);
    rndrstream_free = buf + 64;
    NuRndrStreamLink(&s);
    CHECK(s.base.u8 == buf + 64 && s.ptr.u8 == buf + 64);

    s.base.u8 = buf;
    s.ptr.u8 = buf + 16;
    rndrstream_free = buf + 96;
    NuRndrStreamLink(&s);
    CHECK(((uint32_t *)(buf + 16))[1] == (uint32_t)(uintptr_t)(buf + 96) && s.ptr.u8 == buf + 96);

    s.ptr.u8 = buf + 16;
    rndrstream_free = buf + 32;
    NuRndrStreamLink(&s);
    CHECK(s.ptr.u8 == buf + 16 && s.base.u8 == buf);

    rndrstream_free = saved;
}

static void test_copy(void)
{
    u8 src[16 * 10], dst[16 * 10 + 1];
    for (unsigned i = 0; i < sizeof src; i++) {
        src[i] = (u8)i;
    }
    for (int n = 0; n <= 10; n++) {
        memset(dst, 0xEE, sizeof dst);
        NuMemCopy128(dst, src, n);
        CHECK(memcmp(dst, src, (size_t)n * 16) == 0 && dst[n * 16] == 0xEE);
    }
}

int main(void)
{
    NuTrigInit();
    test_mtx();
    test_vec();
    test_tri_z();
    test_plane();
    test_clip();
    test_dma();
    test_stream_link();
    test_copy();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("hle: all checks passed\n");
    return 0;
}
