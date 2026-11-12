#include "gfx/shadow.h"

#include <math.h>
#include <string.h>

bool nw_shadow_normal(float out[3], const float plane[16])
{
    float n[3] = { plane[4], plane[5], plane[6] };
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (isfinite(len) && len < 1e-6f) {
        n[0] = plane[9] * plane[2] - plane[10] * plane[1];
        n[1] = plane[10] * plane[0] - plane[8] * plane[2];
        n[2] = plane[8] * plane[1] - plane[9] * plane[0];
        len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    }
    if (!isfinite(len) || len < 1e-6f) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        out[k] = n[k] / len;
    }
    return true;
}

bool nw_shadow_projection(float out[16], const float plane[16], const float light[3])
{
    float n[3];
    if (!nw_shadow_normal(n, plane)) {
        return false;
    }
    float l[3], ll = 0.0f, dot = 0.0f, height = 0.0f;
    for (int k = 0; k < 3; k++) {
        if (!isfinite(plane[12 + k])) {
            return false;
        }
        l[k] = light ? light[k] : n[k];
        ll += l[k] * l[k];
        dot += n[k] * l[k];
        height += n[k] * plane[12 + k];
    }
    /* At most a roughly 2.7:1 horizontal stretch. Avoid projections toward a light below the receiver. */
    if (!isfinite(ll) || !isfinite(dot) || dot < sqrtf(ll) * 0.35f || ll < 1e-12f) {
        memcpy(l, n, sizeof(l));
        dot = 1.0f;
    }
    memset(out, 0, 16 * sizeof(*out));
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out[r * 4 + c] = (r == c ? 1.0f : 0.0f) - n[r] * l[c] / dot;
        }
    }
    for (int c = 0; c < 3; c++) {
        out[12 + c] = height * l[c] / dot;
    }
    out[15] = 1.0f;
    return true;
}
