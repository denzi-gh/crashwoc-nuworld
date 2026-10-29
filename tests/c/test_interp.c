/* Display-rate interpolation (src/gfx/interp.c): the matrix blend, the keys a frame's draws get, their pairing with
   the frame before and the blend factor. Synthetic packets only. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gfx/interp.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static bool near(const float *a, const float *b, int n, float eps)
{
    for (int i = 0; i < n; i++) {
        if (fabsf(a[i] - b[i]) > eps) {
            return false;
        }
    }
    return true;
}

/* A turn of angle about y, scaled by s, at (x, y, z), as NuMtxRotateY then NuMtxTranslate make it. */
static void yaw(float m[16], float angle, float s, float x, float y, float z)
{
    float c = cosf(angle), n = sinf(angle);
    const float r[16] = { c * s, 0, -n * s, 0, 0, s, 0, 0, n * s, 0, c * s, 0, x, y, z, 1 };
    memcpy(m, r, sizeof(r));
}

static void test_mtx(void)
{
    float a[16], b[16], out[16], want[16];
    /* The ends are the matrices themselves. */
    yaw(a, 0.3f, 2.0f, 1, 2, 3);
    yaw(b, 1.2f, 1.0f, 5, 6, 7);
    nw_interp_mtx(out, a, b, 0.0f);
    CHECK(near(out, a, 16, 1e-5f));
    nw_interp_mtx(out, a, b, 1.0f);
    CHECK(near(out, b, 16, 1e-5f));
    /* A 170-degree turn goes through 85 degrees, not through a shrunk matrix. */
    yaw(a, 0.0f, 1.0f, 0, 0, 0);
    yaw(b, 170.0f * (float)M_PI / 180.0f, 1.0f, 10, 0, 0);
    nw_interp_mtx(out, a, b, 0.5f);
    yaw(want, 85.0f * (float)M_PI / 180.0f, 1.0f, 5, 0, 0);
    CHECK(near(out, want, 16, 1e-4f));
    /* The short way round: 350 degrees is -10. */
    yaw(b, 350.0f * (float)M_PI / 180.0f, 1.0f, 0, 0, 0);
    nw_interp_mtx(out, a, b, 0.5f);
    yaw(want, -5.0f * (float)M_PI / 180.0f, 1.0f, 0, 0, 0);
    CHECK(near(out, want, 16, 1e-4f));
    /* A mirror (a reflection's y flip) blends as one. */
    yaw(a, 0.2f, 1.0f, 0, 0, 0);
    yaw(b, 0.6f, 1.0f, 0, 4, 0);
    a[5] = b[5] = -1.0f;
    nw_interp_mtx(out, a, b, 0.5f);
    yaw(want, 0.4f, 1.0f, 0, 2, 0);
    want[5] = -1.0f;
    CHECK(near(out, want, 16, 1e-4f));
    /* A projection is lerped by element. */
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    a[11] = 1.0f;
    b[11] = 3.0f;
    nw_interp_mtx(out, a, b, 0.5f);
    CHECK(out[11] == 2.0f && out[0] == 0.0f);
}

/* One frame's draws of a gobj at two instance matrices and twice under one matrix on the stack. */
static void frame(NwPacket *p, int skip_first, float x)
{
    static float inst[2][16];
    static float stack[16];
    static int gobj;
    NwCmd *clear = nw_packet_cmd(p);
    clear->op = NW_CMD_CLEAR;
    clear->clear.mask = 3;
    for (int i = 0; i < 2; i++) {
        NwInterpScope s = nw_interp_begin(inst[i], &gobj);
        nw_interp_at(0, 0, 0);
        if (!(skip_first && i == 0)) {
            NwCmd *c = nw_packet_cmd(p);
            c->op = NW_CMD_DRAW;
            c->key = nw_interp_cmd(NW_CMD_DRAW);
            yaw(c->local, 0.0f, 1.0f, x + (float)i, 0, 0);
        }
        nw_interp_end(s);
    }
    for (int i = 0; i < 2; i++) {
        nw_interp_key(i ? &inst[1] : &inst[0]); /* NW_INTERP_KEY: an object each */
        NwInterpScope s = nw_interp_begin(stack, &gobj);
        if (!(skip_first && i == 0)) {
            NwCmd *c = nw_packet_cmd(p);
            c->op = NW_CMD_DRAW;
            c->key = nw_interp_cmd(NW_CMD_DRAW);
            yaw(c->local, 0.0f, 1.0f, x + 10.0f + (float)i, 0, 0);
        }
        nw_interp_end(s);
        nw_interp_key(NULL);
    }
    NwCmd *rect = nw_packet_cmd(p);
    rect->op = NW_CMD_RECT;
    rect->key = nw_interp_cmd(NW_CMD_RECT);
    nw_interp_publish(p);
}

static void test_keys(void)
{
    NwPacket a = { 0 }, b = { 0 };
    frame(&a, 0, 0.0f);
    frame(&b, 1, 10.0f);
    a.seq = 1;
    a.edge = 10;
    b.seq = 2;
    b.edge = 12;
    CHECK(a.ncmds == 6 && b.ncmds == 4);
    CHECK(a.cmds[1].key && a.cmds[1].key != a.cmds[2].key && a.cmds[3].key != a.cmds[4].key);
    CHECK(a.cmds[5].op == NW_CMD_RECT && a.cmds[5].key == 0);
    /* The draws skipped in b leave the keys of the others as they were. */
    CHECK(b.cmds[1].key == a.cmds[2].key && b.cmds[2].key == a.cmds[4].key);
    CHECK(nw_interp_redraws(&b));

    CHECK(nw_interp_t(&a, &b, 12.0) == 0.0f && nw_interp_t(&a, &b, 13.0) == 0.5f && nw_interp_t(&a, &b, 20.0) == 1.0f);
    CHECK(nw_interp_t(NULL, &b, 12.0) == 1.0f);
    const NwPacket *out = nw_interp_blend(&a, &b, 0.5f);
    CHECK(out != &b && out->ncmds == b.ncmds);
    CHECK(fabsf(out->cmds[1].local[12] - 6.0f) < 1e-4f && fabsf(out->cmds[2].local[12] - 16.0f) < 1e-4f);
    CHECK(b.cmds[1].local[12] == 11.0f);
    NwInterpStats st = nw_interp_stats();
    CHECK(st.paired == 2 && st.unpaired == 0);
    CHECK(nw_interp_blend(&a, &b, 1.0f) == &b);

    /* A draw on the back buffer before its clear: drawn once. */
    NwCmd first = b.cmds[0];
    b.cmds[0] = b.cmds[1];
    b.cmds[1] = first;
    CHECK(!nw_interp_redraws(&b));
    SDL_free(a.cmds);
    SDL_free(b.cmds);
}

/* Cuts, gaps and jumps are drawn as the newer packet. */
static void test_snaps(void)
{
    NwPacket a = { 0 }, b = { 0 };
    frame(&a, 0, 0.0f);
    nw_interp_snap();
    frame(&b, 0, 1.0f);
    a.seq = 3;
    a.edge = 20;
    b.seq = 4;
    b.edge = 21;
    CHECK(b.snap && !a.snap && nw_interp_t(&a, &b, 21.0) == 1.0f);
    b.snap = false;
    CHECK(nw_interp_t(&a, &b, 21.0) == 0.0f);
    b.edge = 20 + NW_INTERP_GAP + 1;
    CHECK(nw_interp_t(&a, &b, (double)b.edge) == 1.0f);

    float m[16], turned[16];
    yaw(m, 0.0f, 1.0f, 0, 0, 0);
    yaw(turned, 1.7f, 1.0f, 0, 0, 0);
    CHECK(!nw_interp_jump(m, m) && nw_interp_jump(m, turned));
    yaw(turned, 0.5f, 1.0f, NW_INTERP_JUMP + 1.0f, 0, 0);
    CHECK(nw_interp_jump(m, turned));
    /* The first draw jumps away: drawn as b, the others blended. */
    b.cmds[1].local[12] += 100.0f;
    nw_interp_blend(&a, &b, 0.5f);
    NwInterpStats st = nw_interp_stats();
    CHECK(st.snapped == 1 && st.paired == 3);
    SDL_free(a.cmds);
    SDL_free(b.cmds);
}

int main(void)
{
    test_mtx();
    test_keys();
    test_snaps();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("interp: ok\n");
    return 0;
}
