#include "hle/eefloat.h"
#include "nu3d/nuanim.h"

/* nuanim.c's curve evaluators in the EE's float: the exporter writes a 1/0 dtime (two keys on one frame) and
   FLT_MAX pads, which the EE reads as finite and saturates. BitCountTable is popcount. */

static int nw_anim_nkey(const union nuanimmask_u *mask, const struct nuanimtime_s *atime)
{
    int nkey = 0;
    for (unsigned int i = 0; i < atime->time_byte; i++) {
        nkey += __builtin_popcount(mask->bytes[i]);
    }
    return nkey + __builtin_popcount(mask->bytes[atime->time_byte] & atime->time_mask);
}

float NuAnimCurveCalcVal2(struct nuanimcurve_s *curve, struct nuanimtime_s *atime)
{
    struct nuanimkey_s *next = &curve->animkeys[nw_anim_nkey(&curve->mask, atime)];
    struct nuanimkey_s *key = next - 1;
    if (curve->flags & 1) {
        return key->d;
    }
    double d = nw_ee_in(key->d);
    double dt = nw_ee_sub(nw_ee_in(next->time), nw_ee_in(key->time));
    double u = nw_ee_mul(nw_ee_sub(nw_ee_in(atime->time), nw_ee_in(key->time)), nw_ee_in(key->dtime));
    double b = nw_ee_mul(nw_ee_in(key->c), dt);
    double c = nw_ee_mul(nw_ee_in(next->c), dt);
    double a = nw_ee_sub(d, nw_ee_in(next->d));
    double val = nw_ee_add(nw_ee_add(nw_ee_add(a, a), b), c);
    val = nw_ee_sub(nw_ee_sub(nw_ee_add(nw_ee_mul(u, val), nw_ee_mul(a, -3.0)), nw_ee_add(b, b)), c);
    val = nw_ee_add(nw_ee_mul(u, nw_ee_add(nw_ee_mul(u, val), b)), d);
    return nw_ee_out(val);
}
