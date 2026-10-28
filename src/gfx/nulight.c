/* nu3d/nulight's effect draws (port/units.toml overrides): retail's tables and passes, their GS packets as VERTS
   records; where retail reads the Z buffer back byte by byte through palette render targets, a depth CLUT
   (NwZClut) reads the GS Z under each pixel instead. */
#include <string.h>

#include "gfx/rndr.h"
#include "nu3d/nucamera.h"
#include "nu3d/nulight.h"
#include "nu3d/nurndr.h"
#include "nu3d/numtl.h"
#include "nu3d/nustream.h"
#include "numath/numtx.h"
#include "numath/nutrig.h"

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
extern struct rndrstream_s *rndrstream_3d;
extern struct rndrstream_s *rndrstream_3d_u;
extern u8 *rndrstream_free;
extern int txupload_on;
extern int nurndr_pixel_width;
extern int nurndr_pixel_height;
extern int PS2_REZ_H;
extern int PS2_VCNTR_X;
extern int PS2_VCNTR_Y;

void *vpDmaTag_Next(void *packet, uint32_t addr);
void NuRndrStreamLink(struct rndrstream_s *stream);
int NuRndrBeginSceneEx(int target, int zbuffer, int halfoffset);
void NuRndrEndSceneEx(int flags);
void NuLightHaze(int z, int paused);
void NuLightPolyHaze(int paused);

/* The fog's depth CLUT of this frame: NuLgtHSurf[4]'s alpha, which the haze composite reads back. */
static NwZClut nw_lgt_fog;

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
   and right four pixels and the top and bottom three rows. */
static void nw_lgt_haze_copy(u32 a)
{
    const float p2h = (float)NuPower2(PS2_REZ_H);
    const int rows = NuPower2(PS2_REZ_H) >> 4;
    const u32 colour = 0x808080 | a << 24;
    const int xoffset = (PS2_VCNTR_X - (nurndr_pixel_width >> 1)) << 4;
    const int yoffset = (PS2_VCNTR_Y - (nurndr_pixel_height >> 1)) << 4;
    NwVuSpriteVertex q[4], *v;
    nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, (uint32_t)(10 * rows + 4) * 6, &v);
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < rows; j++) {
            for (int k = 0; k < 4; k++) {
                const struct nurndrscreenvtx_s *h = &Hazetab[j + (k >> 1)][i + (k & 1)];
                int dx = (int)(NuTrigTable[h->x & 0xFFFF] * Hazeampx) - 8;
                int dy = (int)(NuTrigTable[(h->y + 0x4000) & 0xFFFF] * Hazeampy) - 8;
                nw_lgt_corner(&q[k], ((i + (k & 1)) << 10) + dx + xoffset, ((j + (k >> 1)) << 8) + dy + yoffset,
                              (float)(i + (k & 1)) * 64.0f / 1024.0f, (float)((j + (k >> 1)) << 4) / p2h, colour);
            }
            v = nw_rndr_quad(v, q);
        }
    }
    const int h = PS2_REZ_H << 4, bottom = (PS2_REZ_H - 3) << 4;
    const float vb = ((float)PS2_REZ_H - 0.5f) / p2h;
    const struct {
        int x[4], y[4];
        float s[4], t[4];
    } strips[4] = {
        { { 0, 0x40, 0, 0x40 }, { 0, 0, h, h }, { 0.0f, 3.5f / 1024.0f, 0.0f, 3.5f / 1024.0f },
          { 0.0f, 0.0f, vb, vb } },
        { { 0x27C0, 0x2800, 0x27C0, 0x2800 }, { 0, 0, h, h },
          { 636.0f / 1024.0f, 639.5f / 1024.0f, 636.0f / 1024.0f, 639.0f / 1024.0f }, { 0.0f, 0.0f, vb, vb } },
        { { 0, 0x2800, 0, 0x2800 }, { 0, 0, 0x30, 0x30 }, { 0.0f, 639.5f / 1024.0f, 0.0f, 639.5f / 1024.0f },
          { 0.5f / p2h, 0.5f / p2h, 2.5f / p2h, 2.5f / p2h } },
        { { 0, 0x2800, 0, 0x2800 }, { bottom, bottom, h, h }, { 0.0f, 639.5f / 1024.0f, 0.0f, 639.5f / 1024.0f },
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
        const float uv[4] = { 0.0f, 0.0f, 639.5f / 1024.0f, ((float)PS2_REZ_H - 0.5f) / NuPower2(PS2_REZ_H) };
        nw_lgt_stream(NuLgtSurf[12]);
        NwCmd *c = nw_rndr_rect3d(rndrstream_3d, -8, -8, 0x2800, PS2_REZ_H << 4, uv, colour, z);
        c->alpha = 0x100 | 0x44;
        *nw_rndr_zclut(c) = nw_lgt_fog;
        rndrstream_free = vpDmaTag_Next(rndrstream_3d->ptr.u32, 0);
        NuRndrEndSceneEx(0);
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
            NwCmd *c = nw_rndr_verts(rndrstream_3d, NW_VERTS_GS, 10 * 6, &v);
            c->alpha = 0x100 | 0x44;
            for (i = 0; i < 10; i++) {
                const float uv[4] = { (float)i * 64.0f / 1024.0f, 0.0f, ((float)i * 64.0f + 64.0f) / 1024.0f,
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
            for (i = 0; i <= 9; i++) {
                const float a[4] = { ((float)i * 64.0f + 2.0f) / 1024.0f, 0.0f,
                                     ((float)i * 64.0f + 62.0f + 2.0f) / 1024.0f, v1 };
                const float b[4] = { ((float)i * 64.0f + 64.0f) / 1024.0f, 0.0f,
                                     ((float)i * 64.0f + 64.0f + 2.0f) / 1024.0f, v1 };
                nw_rndr_rect3d(rndrstream_3d, (i << 10) - 8, (ys << 4) - 8, 0x3E0, ye << 4, a,
                               NuRndrFogBlur + 0x808080, (int)znear)->alpha = 0x100 | 0x54;
                if (i < 9) {
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
