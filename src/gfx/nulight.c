/* nu3d/nulight's effect draws (port/units.toml overrides): retail's tables and passes, their GS packets as VERTS
   records; where retail reads the Z buffer back byte by byte through palette render targets, a depth CLUT
   (NwZClut) reads the GS Z under each pixel instead. */
#include <math.h>
#include <string.h>

#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nulight.h"
#include "nu3d/nurndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "nu3d/nutex.h"
#include "nu3d/nuvport.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"
#include "numath/nuvec.h"

extern int NuRndrFogClear;
extern float NuRndrFogNear;
extern float NuRndrFogFar;
extern int NuRndrFogCol;
extern int NuRndrFogBlur;
extern u8 NuRndrTopBit[256];
extern float NuTrigTable[];
extern struct numtl_s *NuLgtSurf[18];
extern int NuLgtHSurf[18];
extern struct numtl_s *NuLgtBackAlphaBuff;
extern struct numtl_s *NuLgtBackBuff;
extern int HazeColour;
extern float Hazeampx;
extern float Hazeampy;
extern int NuLightHazePolyCnt;
extern void *NuLgtArcMtl;
extern float NuLgtArcU0;
extern float NuLgtArcV0;
extern float NuLgtArcU1;
extern float NuLgtArcV1;
extern int NuLgtArcLaserCnt;
extern int NuLgtArcLaserOldCnt;
extern int NuLgtArcLaserFrame;
extern int NuLgtSeed;
extern int NuLightSpotCnt;
extern int NuLightOldSpotCnt;
extern void *NuLightHazePolymtl;
extern float NuLightHazePolyus[4];
extern float NuLightHazePolyvs[4];
extern struct rndrstream_s *rndrstream_3d;
extern struct rndrstream_s *rndrstream_3d_u;
extern u8 *rndrstream_free;
extern int txupload_on;
extern int nurndr_pixel_width;
extern int nurndr_pixel_height;
extern int PS2_REZ_W;
extern int PS2_REZ_H;
extern int PS2_VCNTR_X;
extern int PS2_VCNTR_Y;

void *vpDmaTag_Next(void *packet, uint32_t addr);
void NuRndrStreamLink(struct rndrstream_s *stream);
int NuRndrBeginSceneEx(int target, int zbuffer, int halfoffset);
void NuRndrEndSceneEx(int flags);
void NuLightHaze(int z, int paused);
void NuLightPolyHaze(int paused);
void NuVec4MtxTransformVU0(struct nuvec4_s *dst, struct nuvec4_s *src, struct numtx_s *mtx);
void NuVec4ScaleXYZVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale);
void NuVec4ScaleVU0(struct nuvec4_s *dst, struct nuvec4_s *src, float scale);
f32 NuFsqrt(f32 x);

/* nulight.c's; NuLightAddSpot and NuLightAddSpotXSpanFade fill them. */
struct nulgtspot_s {
    struct nuvec_s pos;
    float size;
    u32 colour;
    u32 mode;
    float brightness;
    u8 xspan;
    u8 fade;
    char pad[2];
};
extern struct nulgtspot_s NuLightSpots[8];

/* nulight.c's; NuLgtArcLaser fills them. */
struct nulgtarclaser_s {
    struct nuvec_s start;
    struct nuvec_s end;
    struct nuvec_s mid;
    float sizew;
    float sizel;
    float sizewob;
    float wobspeed;
    char type;
    char pad[3];
    u8 rgba[4];
    int seed;
};
extern struct nulgtarclaser_s NuLgtArcLaserData[16];

/* nulight.c's; NuLightAddHazePoly fills them. */
struct nulgtpolyhaze_s {
    struct nuvec_s pos;
    float size;
    float rot;
};
extern struct nulgtpolyhaze_s NuLgtPolyHazeData[32];

/* The fog's depth CLUT of this frame: NuLgtHSurf[4]'s alpha, which the haze composite reads back unless the haze
   polys drew their masks over it this frame. */
static NwZClut nw_lgt_fog;
static bool nw_lgt_polyhaze;

/* rndrstream_3d_u with mtl, as retail starts each pass's packets. */
static void nw_lgt_stream(struct numtl_s *mtl)
{
    rndrstream_3d = rndrstream_3d_u;
    rndrstream_3d->mtl = 0;
    rndrstream_3d->tid = 0;
    NuRndrStreamLink(rndrstream_3d);
    NuRndrStreamSetMisc(rndrstream_3d);
    NuRndrStreamAddMtl(rndrstream_3d, mtl);
}

/* Each frame's GS memory for the light passes: the temporary surfaces and the views that alias them (NuLgtSurf[5],
   [9], [12] and [15] are NuLgtHSurf[4]). Retail also copies the Z buffer into NuLgtHSurf[1], [8] and [7] there, and
   splits its bytes through the fx microcode's sprite programs (NuRndrFxCode), for the passes that read Z back; the
   depth CLUTs read the frame's Z instead. */
void NuLightFxSetup(void)
{
    static const int8_t clones[12][2] = { { 3, 1 }, { 6, 1 }, { 8, 1 },  { 13, 1 }, { 17, 1 }, { 7, 2 },
                                          { 11, 2 }, { 14, 2 }, { 5, 4 }, { 9, 4 },  { 12, 4 }, { 15, 4 } };
    NuTexFlushTemp();
    NuTexAssignTempAddr(NuLgtHSurf[1]);
    NuTexAssignTempAddr(NuLgtHSurf[4]);
    NuTexAssignTempAddr(NuLgtHSurf[2]);
    for (int i = 0; i < 12; i++) {
        NuTexCloneTempAddr(NuLgtSurf[clones[i][0]]->tid, NuLgtSurf[clones[i][1]]->tid);
    }
}

/* A corner of a DIRECT GIF packet: GS coordinates, s and t (Q 1), the colour. */
static void nw_lgt_corner(NwVuSpriteVertex *v, int x, int y, float s, float t, u32 colour)
{
    memset(v, 0, sizeof(*v));
    v->pos[0] = (float)x;
    v->pos[1] = (float)y;
    v->pos[2] = (float)0x7FFFFF;
    v->pos[3] = 1.0f;
    v->tex[0] = s * 4096.0f;
    v->tex[1] = t * 4096.0f;
    memcpy(v->col, &colour, 4);
}

/* The haze passes' copy of the frame (NuLgtBackBuff) into the current target, in grey with alpha a, at z 0xFFFFFF:
   a strip of quads per column of 64 pixels, rows of 16, each corner moved by Hazetab's phase (Hazeampx, Hazeampy
   in 1/16 pixels, less half a pixel) over the frame's texel there; then retail's undistorted strips over the left
   and right four pixels and the top and bottom three rows. A frame wider than retail's 640 pixels takes Hazetab's
   20 columns mirrored, so that neighbouring strips still share their corners. */
static void nw_lgt_haze_copy(u32 a)
{
    const float p2h = (float)NuPower2(PS2_REZ_H), p2w = (float)NuPower2(PS2_REZ_W);
    const int rows = NuPower2(PS2_REZ_H) >> 4, cols = (PS2_REZ_W + 63) >> 6;
    const u32 colour = 0x808080 | a << 24;
    const int xoffset = (PS2_VCNTR_X - (nurndr_pixel_width >> 1)) << 4;
    const int yoffset = (PS2_VCNTR_Y - (nurndr_pixel_height >> 1)) << 4;
    NwVuSpriteVertex q[4], *v;
    nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, (uint32_t)(cols * rows + 4) * 6, &v);
    for (int i = 0; i < cols; i++) {
        for (int j = 0; j < rows; j++) {
            for (int k = 0; k < 4; k++) {
                const int col = (i + (k & 1)) % 40;
                const struct nurndrscreenvtx_s *h = &Hazetab[j + (k >> 1)][col <= 20 ? col : 40 - col];
                int dx = (int)(NuTrigTable[h->x & 0xFFFF] * Hazeampx) - 8;
                int dy = (int)(NuTrigTable[(h->y + 0x4000) & 0xFFFF] * Hazeampy) - 8;
                nw_lgt_corner(&q[k], ((i + (k & 1)) << 10) + dx + xoffset, ((j + (k >> 1)) << 8) + dy + yoffset,
                              (float)(i + (k & 1)) * 64.0f / p2w, (float)((j + (k >> 1)) << 4) / p2h, colour);
            }
            v = nw_rndr_quad(v, q);
        }
    }
    const int h = PS2_REZ_H << 4, bottom = (PS2_REZ_H - 3) << 4, w = PS2_REZ_W << 4;
    const float sr = ((float)PS2_REZ_W - 0.5f) / p2w;
    const float vb = ((float)PS2_REZ_H - 0.5f) / p2h;
    const struct {
        int x[4], y[4];
        float s[4], t[4];
    } strips[4] = {
        { { 0, 0x40, 0, 0x40 }, { 0, 0, h, h }, { 0.0f, 3.5f / p2w, 0.0f, 3.5f / p2w },
          { 0.0f, 0.0f, vb, vb } },
        { { w - 0x40, w, w - 0x40, w }, { 0, 0, h, h },
          { (float)(PS2_REZ_W - 4) / p2w, sr, (float)(PS2_REZ_W - 4) / p2w, (float)(PS2_REZ_W - 1) / p2w },
          { 0.0f, 0.0f, vb, vb } },
        { { 0, w, 0, w }, { 0, 0, 0x30, 0x30 }, { 0.0f, sr, 0.0f, sr },
          { 0.5f / p2h, 0.5f / p2h, 2.5f / p2h, 2.5f / p2h } },
        { { 0, w, 0, w }, { bottom, bottom, h, h }, { 0.0f, sr, 0.0f, sr },
          { (float)(PS2_REZ_H - 3) / p2h, (float)(PS2_REZ_H - 3) / p2h, vb, vb } },
    };
    for (int s = 0; s < 4; s++) {
        for (int k = 0; k < 4; k++) {
            nw_lgt_corner(&q[k], xoffset + strips[s].x[k], yoffset + strips[s].y[k], strips[s].s[k], strips[s].t[k],
                          colour);
        }
        v = nw_rndr_quad(v, q);
    }
}

/* Each frame Hazetab's phases move by their speeds. */
static void nw_lgt_haze_step(void)
{
    for (int i = 0; i < 21; i++) {
        for (int j = 0; j < (NuPower2(PS2_REZ_H) >> 4) + 1; j++) {
            Hazetab[j][i].x += Hazetab[j][i].u;
            Hazetab[j][i].y += Hazetab[j][i].v;
        }
    }
}

/* The heat haze (HazeColour's alpha, water_r's and volcano's): the warped copy into NuLgtHSurf[5], which is
   NuLgtHSurf[4] as 24 bits and so keeps the fog's alpha there, then that blended over the frame in the haze colour
   at half its alpha (ALPHA 0x44) where the frame is beyond z, fog-near; the alpha is the fog's depth CLUT's. */
void NuLightHaze(int z, int paused)
{
    if (!(HazeColour >> 24)) {
        return;
    }
    int colour = (HazeColour & 0xFFFFFF) + ((HazeColour >> 1) & 0xFF000000);
    txupload_on = 0;
    if (NuRndrBeginSceneEx(NuLgtHSurf[5], -2, 0)) {
        nw_lgt_stream(NuLgtBackBuff);
        nw_lgt_haze_copy(0x80);
        rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
        NuRndrEndSceneEx(0);
    }
    if (!paused) {
        nw_lgt_haze_step();
    }
    if (NuRndrBeginSceneEx(-1, -2, 0)) {
        const float uv[4] = { 0.0f, 0.0f, ((float)PS2_REZ_W - 0.5f) / NuPower2(PS2_REZ_W),
                              ((float)PS2_REZ_H - 0.5f) / NuPower2(PS2_REZ_H) };
        nw_lgt_stream(NuLgtSurf[12]);
        NwCmd *c = nw_rndr_rect3d(rndrstream_3d, -8, -8, PS2_REZ_W << 4, PS2_REZ_H << 4, uv, colour, z);
        c->alpha = 0x100 | 0x44;
        if (!nw_lgt_polyhaze) {
            *nw_rndr_zclut(c) = nw_lgt_fog;
        }
        rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
        NuRndrEndSceneEx(0);
    }
    txupload_on = 1;
}

/* A point on the line from p by d * t, bowed by mid along sin(t * pi), through the VPCS matrix and divided;
   returns 1 / w. */
static float nw_lgt_arc_point(struct nuvec4_s *out, const struct nuvec4_s *p, const struct nuvec4_s *d,
                              const struct nuvec4_s *mid, float t, bool bow, struct numtx_s *vpcs)
{
    float sine = bow ? NuTrigTable[(u16)(t * 32768.0f)] : 0.0f;
    out->x = p->x + d->x * t + mid->x * sine;
    out->y = p->y + d->y * t + mid->y * sine;
    out->z = p->z + d->z * t + mid->z * sine;
    out->w = 1.0f;
    NuVec4MtxTransformVU0(out, out, vpcs);
    float scale = 1.0f / out->w;
    NuVec4ScaleXYZVU0(out, out, scale);
    return scale;
}

/* The arc lasers (NuLgtArcLaser): each from start to end, cut where it nears the camera (w 0.5), in screen-space
   ribbons sizew wide of segments sizel long, bowed by mid at wobspeed and jittered across by sizewob from NuLgtSeed
   (but the last), in NuLgtArcMtl's UV box, as retail's DIRECT GIF strips of four vertices; a segment wholly off one
   side of the guard band is dropped (its triangles left degenerate). Paused, the last frame's lasers again. */
void NuLgtArcLaserDraw(int paused)
{
    struct nuvec4_s pnts[11];
    const float v[4] = { NuLgtArcV0, NuLgtArcV0, NuLgtArcV1, NuLgtArcV1 };
    if (paused) {
        NuLgtArcLaserCnt = NuLgtArcLaserOldCnt;
    }
    if (!NuLgtArcMtl || !NuLgtArcLaserCnt) {
        NuLgtArcLaserOldCnt = 0;
        return;
    }
    struct numtx_s *vpcs = NuCameraGetVPCSMtx();
    if (!NuRndrBeginSceneEx(-1, -2, 0)) {
        return;
    }
    nw_lgt_stream(NuLgtArcMtl);
    for (int id = 0; id < NuLgtArcLaserCnt; id++) {
        struct nulgtarclaser_s *l = &NuLgtArcLaserData[id];
        pnts[4] = (struct nuvec4_s){ l->start.x, l->start.y, l->start.z, 1.0f };
        NuVec4MtxTransformVU0(&pnts[4], &pnts[4], vpcs);
        pnts[5] = (struct nuvec4_s){ l->end.x, l->end.y, l->end.z, 1.0f };
        NuVec4MtxTransformVU0(&pnts[5], &pnts[5], vpcs);
        if (!(0.5f <= pnts[4].w) && !(0.5f <= pnts[5].w)) {
            continue;
        }
        if (pnts[4].w < 0.5f) {
            float t = (pnts[5].w - 0.5f) / (pnts[5].w - pnts[4].w);
            pnts[6] = (struct nuvec4_s){ l->end.x + (l->start.x - l->end.x) * t, l->end.y + (l->start.y - l->end.y) * t,
                                         l->end.z + (l->start.z - l->end.z) * t, 1.0f };
        } else {
            pnts[6] = (struct nuvec4_s){ l->start.x, l->start.y, l->start.z, 1.0f };
        }
        if (pnts[5].w < 0.5f) {
            float t = (pnts[4].w - 0.5f) / (pnts[4].w - pnts[5].w);
            pnts[7] = (struct nuvec4_s){ l->start.x + (l->end.x - l->start.x) * t,
                                         l->start.y + (l->end.y - l->start.y) * t,
                                         l->start.z + (l->end.z - l->start.z) * t, 1.0f };
        } else {
            pnts[7] = (struct nuvec4_s){ l->end.x, l->end.y, l->end.z, 1.0f };
        }
        NuVec4MtxTransformVU0(&pnts[4], &pnts[6], vpcs);
        NuVec4ScaleXYZVU0(&pnts[4], &pnts[4], 1.0f / pnts[4].w);
        NuVec4MtxTransformVU0(&pnts[5], &pnts[7], vpcs);
        NuVec4ScaleXYZVU0(&pnts[5], &pnts[5], 1.0f / pnts[5].w);
        struct nuvec_s norm = { (pnts[5].y - pnts[4].y) * ((float)PS2_REZ_W / 224.0f), pnts[4].x - pnts[5].x, 0.0f };
        NuVecNorm(&norm, &norm);
        norm.x *= l->sizew * ((float)PS2_REZ_W * 2000.0f / 224.0f);
        norm.y *= l->sizew * 2000.0f;
        pnts[7].x -= pnts[6].x;
        pnts[7].y -= pnts[6].y;
        pnts[7].z -= pnts[6].z;
        pnts[7].w -= pnts[6].w;
        float step = sqrtf(pnts[7].x * pnts[7].x + pnts[7].y * pnts[7].y + pnts[7].z * pnts[7].z);
        if (!(0.0f < step)) {
            continue;
        }
        pnts[10] = (struct nuvec4_s){ l->mid.x, l->mid.y, l->mid.z, 0.0f };
        float scale = pnts[10].x * pnts[10].x + pnts[10].y * pnts[10].y + pnts[10].z * pnts[10].z;
        if (scale > 0.0f) {
            scale = l->wobspeed / NuFsqrt(scale);
        }
        pnts[10].x *= scale;
        pnts[10].y *= scale;
        pnts[10].z *= scale;
        NuLgtSeed = l->seed;
        step = l->sizel / step;
        if (step > 1.0f) {
            step = 1.0f;
        }
        float u[4] = { NuLgtArcU0, NuLgtArcU1, NuLgtArcU0, NuLgtArcU1 };
        scale = nw_lgt_arc_point(&pnts[4], &pnts[6], &pnts[7], &pnts[10], 0.0f, false, vpcs);
        pnts[1] = (struct nuvec4_s){ pnts[4].x - norm.x * scale, pnts[4].y - norm.y * scale, pnts[4].z, 0.0f };
        pnts[3] = (struct nuvec4_s){ pnts[4].x + norm.x * scale, pnts[4].y + norm.y * scale, pnts[4].z, 0.0f };
        int n = 0;
        for (float lp = 0.0f; lp < 1.0f; lp += step) {
            n++;
        }
        NwVuSpriteVertex *v6, q[4];
        nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, (uint32_t)n * 6, &v6);
        NwVuSpriteVertex *first = v6;
        for (float lp = 0.0f; lp < 1.0f; lp += step) {
            float next = lp + step;
            if (next >= 1.0f) {
                scale = nw_lgt_arc_point(&pnts[5], &pnts[6], &pnts[7], &pnts[10], 1.0f, false, vpcs);
                u[0] = u[2] = NuLgtArcU1 + (NuLgtArcU0 - NuLgtArcU1) * (1.0f - lp) / step;
            } else {
                scale = nw_lgt_arc_point(&pnts[5], &pnts[6], &pnts[7], &pnts[10], next, true, vpcs);
            }
            float rx = 0.0f, ry = 0.0f;
            if (lp < 1.0f - step * 1.5f && NuLgtSeed) {
                int seed = NuLgtSeed * 0x24CD + 1;
                NuLgtSeed = seed & 0xFFFF;
                ry = (float)((u8)seed - 0x80);
                rx = norm.x * ry * l->sizewob * scale;
                ry = norm.y * ry * l->sizewob * scale;
            }
            pnts[0] = (struct nuvec4_s){ pnts[5].x - norm.x * scale + rx, pnts[5].y - norm.y * scale + ry, pnts[5].z,
                                         0.0f };
            pnts[2] = (struct nuvec4_s){ pnts[5].x + norm.x * scale + rx, pnts[5].y + norm.y * scale + ry, pnts[5].z,
                                         0.0f };
            if ((!(pnts[0].x < 24576.0f) || !(pnts[1].x < 24576.0f)) &&
                (!(pnts[0].x > 40960.0f) || !(pnts[1].x > 40960.0f)) &&
                (!(pnts[0].y < 30208.0f) || !(pnts[1].y < 30208.0f)) &&
                (!(pnts[0].y > 35328.0f) || !(pnts[1].y > 35328.0f))) {
                for (int k = 0; k < 4; k++) {
                    memset(&q[k], 0, sizeof(q[k]));
                    q[k].pos[0] = (float)(int)pnts[k].x;
                    q[k].pos[1] = (float)(int)pnts[k].y;
                    q[k].pos[2] = (float)(int)pnts[k].z;
                    q[k].pos[3] = 1.0f;
                    q[k].tex[0] = u[k] * 4096.0f;
                    q[k].tex[1] = v[k] * 4096.0f;
                    memcpy(q[k].col, l->rgba, 4);
                }
                v6 = nw_rndr_quad(v6, q);
            }
            pnts[1] = pnts[0];
            pnts[3] = pnts[2];
        }
        while (v6 < first + n * 6) {
            memset(v6++, 0, sizeof(*v6));
        }
    }
    rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
    NuRndrEndSceneEx(0);
    NuLgtArcLaserOldCnt = NuLgtArcLaserCnt;
    NuLgtArcLaserCnt = 0;
    if (!paused) {
        NuLgtArcLaserFrame++;
    }
}

/* A spot fan's vertex: x and y over w, held to the GS area, at z, in colour. */
static void nw_lgt_spot_vertex(NwVuSpriteVertex *v, float x, float y, float z, u32 colour)
{
    x = x < 0.0f ? 0.0f : x > 65535.0f ? 65535.0f : x;
    y = y < 0.0f ? 0.0f : y > 65535.0f ? 65535.0f : y;
    int iz = (int)z;
    memset(v, 0, sizeof(*v));
    v->pos[0] = (float)(int)x;
    v->pos[1] = (float)(int)y;
    v->pos[2] = (float)(iz < 0 ? 0 : iz > 0x7FFFFF ? 0x7FFFFF : iz);
    v->pos[3] = 1.0f;
    memcpy(v->col, &colour, 4);
}

/* Mode 1's outer ring: rim vertex i held out to the screen's edge on its side (an octant of (i + 16) & 63). */
static void nw_lgt_spot_edge(int i, NwVuSpriteVertex *v)
{
    int o = (i + 16) & 0x3F;
    if ((o <= 8 || o >= 56) && v->pos[1] > 28672.0f) {
        v->pos[1] = 28672.0f;
    }
    if (o >= 8 && o <= 24 && v->pos[0] < 38912.0f) {
        v->pos[0] = 38912.0f;
    }
    if (o >= 24 && o <= 40 && v->pos[1] < 36864.0f) {
        v->pos[1] = 36864.0f;
    }
    if (o >= 40 && o <= 56 && v->pos[0] > 26624.0f) {
        v->pos[0] = 26624.0f;
    }
}

/* The spot lights (NuLightAddSpot): each a glow about its point, size * 1.2 deep, in front of the frame's Z there,
   by a CLUT of the GS Z from its far side to its near side (squared, then eased back past 192, a cosine ramp of
   its alpha; xspan dims the colour and adds itself to every entry's alpha). Retail reads the CLUT through the Z
   buffer's bytes into NuLgtHSurf[4] in the spot's screen box, clears entries of 0x80 or more behind fogz (the
   first over 0x40) and, fading, in front of where it eases back, and draws NuLgtSurf[9] of it as a fan of 64
   triangles over the spot at its near Z; a depth CLUT does the reading here, the GS blend in the program. Mode 0
   adds (Cd - Cs) * As to the frame, 2 adds Cs * As; 1 multiplies the frame by As and adds Cs, over the whole
   screen with a ring out to its edges, a light in the dark. mode 1 replays the last frame's spots (paused). */
void NuLightProcessSpot(int mode)
{
    struct nuvec4_s pnts[8];
    struct nuvec_s vecs[4];
    u32 clut[256];
    struct numtx_s *pcs = NuCameraGetPCSMtx();
    struct numtx_s *scale = NuCameraGetScalingMtx();
    struct numtx_s *proj = NuCameraGetProjectionMtx();
    struct numtx_s *view = NuCameraGetViewMtx();
    float zoffset = proj->_22 * scale->_22 + scale->_32;
    float zscale = proj->_32 * scale->_22;
    txupload_on = 0;
    if (mode) {
        NuLightSpotCnt = NuLightOldSpotCnt;
    }
    for (int spot = 0; spot < NuLightSpotCnt; spot++) {
        struct nulgtspot_s *s = &NuLightSpots[spot];
        float f = 1.0f;
        pnts[2] = (struct nuvec4_s){ s->pos.x, s->pos.y, s->pos.z, f };
        NuVec4MtxTransformVU0(&pnts[7], &pnts[2], view);
        float depth = pnts[7].z + s->size * 1.2f;
        if (!(depth > f)) {
            continue;
        }
        if (pnts[7].z < f) {
            f = (depth - f) / (s->size * 1.2f);
            s->size *= f;
            s->colour = (s->colour & 0xFFFFFF) + ((u32)(int)((float)(s->colour >> 24) * f) << 24);
            pnts[7].z = 1.0f;
        }
        vecs[1] = (struct nuvec_s){ pnts[7].x, pnts[7].y, pnts[7].z + s->size * 1.2f };
        pnts[2] = (struct nuvec4_s){ vecs[1].x, vecs[1].y, vecs[1].z, 1.0f };
        NuVec4MtxTransformVU0(&pnts[2], &pnts[2], pcs);
        NuVec4ScaleXYZVU0(&pnts[2], &pnts[2], 1.0f / pnts[2].w);
        float far = pnts[7].z + s->size * 1.2f;
        NuVec4MtxTransformVU0(&pnts[1], &pnts[7], pcs);
        pnts[0].x = pnts[1].x - s->size * 8192.0f;
        pnts[0].z = pnts[1].x + s->size * 8192.0f;
        pnts[0].y = pnts[1].y - s->size * 4096.0f;
        pnts[0].w = pnts[1].y + s->size * 4096.0f;
        NuVec4ScaleVU0(&pnts[0], &pnts[0], 1.0f / pnts[1].w);
        if (!(pnts[0].x < (float)(0x8000 + (nurndr_pixel_width << 3)) &&
              pnts[0].z > (float)(0x8000 - (nurndr_pixel_width << 3)) &&
              pnts[0].y < (float)(0x8000 + (nurndr_pixel_height << 3)) &&
              pnts[0].w > (float)(0x8000 - (nurndr_pixel_height << 3)))) {
            continue;
        }
        int xs = (int)((pnts[0].x - 32768.0f) / 16.0f + (float)(nurndr_pixel_width >> 1));
        int ys = (int)((pnts[0].y - 32768.0f) / 16.0f + (float)(nurndr_pixel_height >> 1));
        int xe = (int)((pnts[0].z - 32768.0f) / 16.0f + (float)(nurndr_pixel_width >> 1));
        int ye = (int)((pnts[0].w - 32768.0f) / 16.0f + (float)(nurndr_pixel_height >> 1));
        xe = xe > nurndr_pixel_width ? nurndr_pixel_width : xe;
        xs = (xs < 0 ? 0 : xs) & 0xFFF8;
        xe = (xe + 7) & 0xFFF8;
        ys = ys < 0 ? 0 : ys;
        ye = ye > nurndr_pixel_height ? nurndr_pixel_height : ye;
        if (s->mode == 1) {
            xs = ys = 0;
            xe = (nurndr_pixel_width + 7) & 0xFFF8;
            ye = nurndr_pixel_height;
        }
        if (xs >= xe || ys >= ye) {
            continue;
        }
        vecs[2] = (struct nuvec_s){ pnts[7].x, pnts[7].y, pnts[7].z };
        float near;
        if (pnts[7].z - s->size * 1.2f < 1.0f) {
            near = 16777215.0f;
            vecs[3].z = 1.0f;
        } else {
            near = 0.0f;
            vecs[3].z = pnts[7].z - s->size * 1.2f;
        }
        pnts[1] = (struct nuvec4_s){ pnts[7].x, pnts[7].y, vecs[3].z, 1.0f };
        NuVec4MtxTransformVU0(&pnts[1], &pnts[1], pcs);
        NuVec4ScaleXYZVU0(&pnts[0], &pnts[1], 1.0f / pnts[1].w);
        if (near == 0.0f) {
            near = pnts[0].z;
        }
        int shift;
        if (pnts[0].w <= 0.0f || pnts[0].z < 0.0f) {
            shift = 16;
        } else {
            float range = (float)((((int)pnts[0].z + 0xFFFF) & 0xFF0000) - ((int)pnts[2].z & 0xFF0000)) / 256.0f;
            shift = range >= 256.0f ? NuRndrTopBit[(int)(range / 256.0f)] + 9 : NuRndrTopBit[(int)range] + 1;
        }
        float zbase = (float)(((int)pnts[2].z >> shift) << shift) - zoffset;
        int count = (int)(pnts[0].z - zbase - zoffset);
        count = ((count + (1 << shift) - 1) >> shift) + 8;
        if (count > 256) {
            count = 256;
        }
        int top = (int)pnts[2].z >> 16;
        if (top > 256) {
            top = 256;
        }
        if (shift < 9) {
            shift = 8;
        }
        float fogscale = 256.0f / (s->size * 1.2f * 2.0f);
        int val = ((int)pnts[2].z >> shift) & 0xFF;
        float fogz = 0.0f;
        u32 fogcol, alpha, add;
        if (s->xspan) {
            u32 k = 0x80u - s->xspan;
            fogcol = ((((s->colour & 0xFF0000) * k) >> 7) & 0xFF0000) + ((((s->colour & 0xFF00) * k) >> 7) & 0xFF00) +
                     (((s->colour & 0xFF) * k) >> 7);
            alpha = ((s->colour >> 24) * k) << 5;
            add = (u32)s->xspan << 24;
        } else {
            add = 0;
            fogcol = s->colour & 0xFFFFFF;
            alpha = (s->colour >> 24) << 12;
        }
        int last = -1, i;
        for (i = 0; i < count; i++) {
            float fog = zscale / (zbase + (float)(i << shift));
            fog = (far - fog) * fogscale;
            fog = fog * fog / 1024.0f + fog * 0.75f;
            if (fog >= 192.0f) {
                fog = (255.0f - fog) * 2.0f;
                if (fog < 63.0f && fog > 52.0f) {
                    last = i;
                }
            } else {
                fog *= 2.0f / 3.0f;
            }
            if (fog < 0.0f) {
                fog = 0.0f;
            }
            fog = (1.0f - NuTrigTable[(int)(fog * 256.0f + 16384.0f) & 0xFFFF]) * 64.0f;
            int a = (int)(fog * 32.0f);
            if (a < 0) {
                a = 0;
            }
            clut[(i + val) & 0xFF] = fogcol + ((alpha * (u32)a) & 0xFF000000) + add;
            if (fogz == 0.0f && clut[(i + val) & 0xFF] > 0x40000000) {
                fogz = zbase + zoffset + (float)(i << shift);
            }
        }
        for (; i < 256; i++) {
            clut[(i + val) & 0xFF] = add;
        }
        if (last > -1) {
            last = (int)(zbase + zoffset + (float)(last << shift));
        }
        NwZClut z;
        memset(&z, 0, sizeof(z));
        memcpy(z.clut, clut, sizeof(clut));
        z.shift = (uint32_t)shift;
        z.spot = true;
        if (shift >= 9) {
            z.base = (uint32_t)top << 16;
            z.far = z.near = (uint32_t)val;
        } else {
            /* retail clears the spot's box behind its far side instead (NuLgtGreyMat) */
            z.base = (int)pnts[2].z < 0 ? 0 : (uint32_t)(int)pnts[2].z + 1;
            z.far = NW_ZCLUT_ZERO;
            z.near = NW_ZCLUT_WRAP;
        }
        if (fogz != 0.0f) {
            z.clear_z[0] = (uint32_t)(int)fogz;
            z.clear_z[1] = s->fade && last >= 0 ? (uint32_t)last : 0;
        }
        if (!NuRndrBeginSceneEx(-1, -2, 0)) {
            continue;
        }
        nw_lgt_stream(NuLgtSurf[9]);
        int alpha_reg = s->mode == 1 ? 0x09 : s->mode == 2 ? 0x48 : 0x41;
        u32 c0 = 0xFF, c1 = 0x80, c2 = 0;
        if (s->mode == 1) {
            if (s->xspan) {
                c0 = 0xFFu - s->xspan < 0x80 ? 0x80 : 0xFFu - s->xspan;
                c2 = s->xspan;
            }
            near = 33554432.0f;
        }
        pnts[1] = (struct nuvec4_s){ vecs[2].x, vecs[2].y, vecs[2].z, 1.0f };
        NuVec4MtxTransformVU0(&pnts[1], &pnts[1], pcs);
        float invw = 1.0f / pnts[1].w;
        NwVuSpriteVertex centre, rim[65], *v;
        nw_lgt_spot_vertex(&centre, pnts[1].x * invw, pnts[1].y * invw, near, 0x808080 | c0 << 24);
        for (i = 0; i < 65; i++) {
            float x = pnts[1].x + NuTrigTable[((i << 10) + 0x4000) & 0xFFFF] * 8192.0f * s->size * s->brightness;
            float y = pnts[1].y + NuTrigTable[(i << 10) & 0xFFFF] * 4096.0f * s->size;
            nw_lgt_spot_vertex(&rim[i], x * invw, y * invw, near, c1 | c1 << 8 | c1 << 16 | c2 << 24);
        }
        NwCmd *c = nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, 64 * 3, &v);
        c->alpha = (uint16_t)(0x100 | alpha_reg);
        c->exact = true;
        *nw_rndr_zclut(c) = z;
        for (i = 0; i < 64; i++) {
            *v++ = centre;
            *v++ = rim[i];
            *v++ = rim[i + 1];
        }
        if (s->mode == 1) {
            NwVuSpriteVertex strip[130];
            for (i = 0; i < 65; i++) {
                strip[i * 2] = rim[i];
                strip[i * 2 + 1] = rim[i];
                nw_lgt_spot_edge(i, &strip[i * 2 + 1]);
            }
            c = nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, 128 * 3, &v);
            c->alpha = (uint16_t)(0x100 | alpha_reg);
            c->exact = true;
            *nw_rndr_zclut(c) = z;
            for (i = 0; i < 128; i++) {
                *v++ = strip[i];
                *v++ = strip[i + 1];
                *v++ = strip[i + 2];
            }
        }
        rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
        NuRndrEndSceneEx(0);
    }
    txupload_on = 1;
    NuLightOldSpotCnt = NuLightSpotCnt;
    NuLightSpotCnt = 0;
}

/* The haze polys (the candles' flames, LDATA flags 0x10): the warped copy into NuLgtHSurf[4] with alpha 0, then
   each poly from its point up by rot, size wide at its distance, in NuLightHazePolymtl under ALPHA 0x48 with colour
   0 and alpha 0xA0, which leaves the copy and writes the texture's shape as its alpha where the poly is in front of
   the frame's Z; that blended over the frame (ALPHA 0x44). Retail drops a poly with a corner behind w 0.2 or off
   the GS area. */
void NuLightPolyHaze(int paused)
{
    struct nuvec4_s pnts[6];
    float ratiox, ratioy;
    nw_lgt_polyhaze = false;
    if (!NuLightHazePolyCnt) {
        return;
    }
    txupload_on = 0;
    if (NuRndrBeginSceneEx(NuLgtHSurf[4], -2, 0)) {
        static NwVuSpriteVertex q[32][4];
        nw_lgt_stream(NuLgtBackBuff);
        nw_lgt_haze_copy(0);
        txupload_on = 1;
        NuRndrStreamAddMtl(rndrstream_3d, NuLightHazePolymtl);
        txupload_on = 0;
        struct numtx_s *vpcs = NuCameraGetVPCSMtx();
        NuCameraGetClippingRatios(&ratiox, &ratioy);
        struct nuviewport_s *vp = NuVpGetCurrentViewport();
        ratiox *= 2.0f;
        int n = 0;
        for (int i = 0; i < NuLightHazePolyCnt && i < 32; i++) {
            const struct nulgtpolyhaze_s *h = &NuLgtPolyHazeData[i];
            pnts[4] = (struct nuvec4_s){ h->pos.x, h->pos.y + h->rot, h->pos.z, 1.0f };
            NuVec4MtxTransformVU0(&pnts[4], &pnts[4], vpcs);
            pnts[5] = (struct nuvec4_s){ h->pos.x, h->pos.y, h->pos.z, 1.0f };
            NuVec4MtxTransformVU0(&pnts[5], &pnts[5], vpcs);
            if (!(pnts[4].w > 0.2f && pnts[5].w > 0.2f)) {
                continue;
            }
            NuVec4ScaleXYZVU0(&pnts[4], &pnts[4], 1.0f / pnts[4].w);
            NuVec4ScaleXYZVU0(&pnts[5], &pnts[5], 1.0f / pnts[5].w);
            float scale = (float)vp->width / (pnts[4].w * ratiox);
            for (int k = 0; k < 4; k++) {
                pnts[k] = pnts[4 + (k >> 1)];
                pnts[k].x += k & 1 ? h->size * scale : -h->size * scale;
            }
            if (!(pnts[0].x > 0.0f && pnts[1].x < 65535.0f && pnts[0].y > 0.0f && pnts[2].y < 65535.0f)) {
                continue;
            }
            for (int k = 0; k < 4; k++) {
                memset(&q[n][k], 0, sizeof(q[n][k]));
                q[n][k].pos[0] = (float)(int)pnts[k].x;
                q[n][k].pos[1] = (float)(int)pnts[k].y;
                q[n][k].pos[2] = (float)(int)pnts[k].z;
                q[n][k].pos[3] = 1.0f;
                q[n][k].tex[0] = NuLightHazePolyus[k] * 4096.0f;
                q[n][k].tex[1] = NuLightHazePolyvs[k] * 4096.0f;
                q[n][k].col[3] = 0xA0;
            }
            n++;
        }
        NwVuSpriteVertex *v;
        nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, (uint32_t)n * 6, &v)->alpha = 0x100 | 0x48;
        for (int i = 0; i < n; i++) {
            v = nw_rndr_quad(v, q[i]);
        }
        nw_lgt_polyhaze = true;
        rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
        NuRndrEndSceneEx(0);
    }
    if (!paused) {
        nw_lgt_haze_step();
    }
    if (NuRndrBeginSceneEx(-1, -2, 0)) {
        const float uv[4] = { 0.0f, 0.0f, ((float)PS2_REZ_W - 0.5f) / NuPower2(PS2_REZ_W),
                              ((float)PS2_REZ_H - 0.5f) / NuPower2(PS2_REZ_H) };
        nw_lgt_stream(NuLgtSurf[12]);
        nw_rndr_rect3d(rndrstream_3d, -8, -8, PS2_REZ_W << 4, PS2_REZ_H << 4, uv, (int)0x80808080, 0x1FFFFFF)->alpha =
            0x100 | 0x44;
        rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
        NuRndrEndSceneEx(0);
    }
    if (!paused) {
        NuLightHazePolyCnt = 0;
    }
    txupload_on = 1;
}

/* The fog CLUT by GS Z from fog-near to fog-far, a cosine ramp to the fog colour; retail builds the index (Z >> shift)
   & 0xFF in NuLgtHSurf[4] from the Z buffer's bytes, clamped to the far end below the 256 entries from Zfar's top
   byte, and draws NuLgtFogMat[1]'s CLUT of it into NuLgtSurf[9], which the composite reads. Seashell's
   NuLightFogClear masks its alpha to a band of Z. The composite and the blur, which moves the frame two pixels where
   its alpha is the fog's, are retail's; FogMat[0] and [2] are the byte passes' CLUTs and not needed. */
void NuLightFogDraw(int paused)
{
    u32 clut[256];
    if (NuRndrFogFar <= NuRndrFogNear) {
        return;
    }
    struct numtx_s *scale = NuCameraGetScalingMtx();
    struct numtx_s *proj = NuCameraGetProjectionMtx();
    float zoffset = proj->_22 * scale->_22 + scale->_32;
    float zscale = proj->_32 * scale->_22;
    txupload_on = 0;
    float far = NuRndrFogFar;
    float zfar = zscale / far + zoffset;
    float znear = zscale / NuRndrFogNear + zoffset;
    int ys = 0, ye = nurndr_pixel_height;
    float range = (float)((((int)znear + 0xFFFF) & 0xFF0000) - ((int)zfar & 0xFF0000)) / 256.0f;
    int shift = range >= 256.0f ? NuRndrTopBit[(int)(range / 256.0f)] + 9 : NuRndrTopBit[(int)range] + 1;
    if (shift < 9) {
        shift = 9;
    }
    float zbase = (float)(((int)zfar >> shift) << shift) - zoffset;
    int count = (int)(znear - zbase - zoffset);
    count = ((count + (1 << shift) - 1) >> shift) + 8;
    if (count > 256) {
        count = 256;
    }
    int top = (int)zfar >> 16;
    if (top > 256) {
        top = 256;
    }
    int last = count + 6;
    float fogscale = 256.0f / (NuRndrFogFar - NuRndrFogNear);
    int val = ((int)zfar >> shift) & 0xFF;
    int fogcol = NuRndrFogCol & 0xFFFFFF;
    int alpha = 0x1FE0000;
    int i;
    for (i = 0; i < count; i++) {
        float z = (float)(i << shift);
        float fog = 255.0f - (far - zscale / (zbase + z)) * fogscale;
        if (fog < 0.0f) {
            fog = 0.0f;
        }
        if (fog > 255.0f) {
            fog = 255.0f;
        }
        int ramp = (int)((1.0f - NuTrigTable[(int)(fog * 128.0f + 16384.0f) & 0xFFFF]) * 64.0f);
        clut[(i + val) & 0xFF] = fogcol + (((u32)alpha * (u32)ramp) & 0xFF000000);
    }
    for (; i < (last > 256 ? 256 : last); i++) {
        clut[(i + val) & 0xFF] = 0;
    }
    for (; i < 256; i++) {
        clut[(i + val) & 0xFF] = (NuRndrFogCol & 0xFFFFFF) + 0xFF000000;
    }
    memcpy(nw_lgt_fog.clut, clut, sizeof(clut));
    nw_lgt_fog.shift = (uint32_t)shift;
    nw_lgt_fog.base = (uint32_t)top << 16;
    nw_lgt_fog.far = (uint32_t)val;
    nw_lgt_fog.near = 0;
    nw_lgt_fog.clear = NuRndrFogClear != 0;
    nw_lgt_fog.clear_z[0] = (uint32_t)(int)((zfar * 3.0f + znear) * 0.25f);
    nw_lgt_fog.clear_z[1] = (uint32_t)(int)((zfar + znear * 3.0f) * 0.25f);
    nw_lgt_fog.alpha = true;
    znear = (float)((((int)znear >> shift) - 1) << shift);
    if (NuRndrFogCol || NuRndrFogBlur) {
        if (NuRndrBeginSceneEx(-1, -2, 0)) {
            NwVuSpriteVertex *v;
            nw_lgt_stream(NuLgtSurf[9]);
            const int cols = (PS2_REZ_W + 63) >> 6;
            const float p2w = (float)NuPower2(PS2_REZ_W);
            NwCmd *c = nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, (uint32_t)cols * 6, &v);
            c->alpha = 0x100 | 0x44;
            for (i = 0; i < cols; i++) {
                const float uv[4] = { (float)i * 64.0f / p2w, 0.0f, ((float)i * 64.0f + 64.0f) / p2w,
                                      ((float)PS2_REZ_H - 0.5f) / NuPower2(PS2_REZ_H) };
                v = nw_rndr_rect3d_quad(v, (i << 10) - 8, (ys << 4) - 8, 0x400, ye << 4, uv,
                                        (int)(((NuRndrFogCol & 0xFE000000) >> 1) | 0x808080), (int)znear);
            }
            NwZClut *z = nw_rndr_zclut(c);
            *z = nw_lgt_fog;
            z->alpha = false;
            rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
            NuRndrEndSceneEx(0);
        }
        if (NuRndrFogBlur && NuRndrBeginSceneEx(-1, -2, 0)) {
            nw_lgt_stream(NuLgtBackAlphaBuff);
            float v1 = (float)PS2_REZ_H / NuPower2(PS2_REZ_H);
            const int cols = (PS2_REZ_W + 63) >> 6;
            const float p2w = (float)NuPower2(PS2_REZ_W);
            for (i = 0; i < cols; i++) {
                const float a[4] = { ((float)i * 64.0f + 2.0f) / p2w, 0.0f,
                                     ((float)i * 64.0f + 62.0f + 2.0f) / p2w, v1 };
                const float b[4] = { ((float)i * 64.0f + 64.0f) / p2w, 0.0f,
                                     ((float)i * 64.0f + 64.0f + 2.0f) / p2w, v1 };
                nw_rndr_rect3d(rndrstream_3d, (i << 10) - 8, (ys << 4) - 8, 0x3E0, ye << 4, a,
                               NuRndrFogBlur + 0x808080, (int)znear)->alpha = 0x100 | 0x54;
                if (i < cols - 1) {
                    nw_rndr_rect3d(rndrstream_3d, (i << 10) + 0x3D8, (ys << 4) - 8, 0x20, ye << 4, b,
                                   NuRndrFogBlur + 0x808080, (int)znear)->alpha = 0x100 | 0x54;
                }
            }
            rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
            NuRndrEndSceneEx(0);
        }
    }
    NuLightPolyHaze(paused);
    NuLightHaze((int)znear, paused);
    txupload_on = 1;
}
