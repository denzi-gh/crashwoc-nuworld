#include "hle/eefloat.h"
#include "nu3d/nuanim.h"
#include "nucore/nuerror.h"
#include "port/nw_newlib.h"

#define nuanim_srcfile "..\\nu2crash.ps2\\nu3d\\nuanim.c"

#define NUANIMKEYTYPE_NONE 0
#define NUANIMKEYTYPE_HERMITE 1
#define NUANIMKEYTYPE_SMALL 2
#define NUANIMKEYTYPE_STEP 3
#define NUANIMKEYTYPE_BOOLEAN 4

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

float NuAnimCurve2CalcVal(union nuanimcurve2_u *curve, struct nuanimtime_s *atime, int keytype)
{
    if (keytype == NUANIMKEYTYPE_NONE) {
        return curve->constant;
    }
    union nuanimmask_u *mask = &curve->curvedata->mask[atime->chunk];
    if (keytype == NUANIMKEYTYPE_BOOLEAN) {
        int bit = (int)floor(atime->time_offset) - 1;
        return mask->bits && (1 << bit) != 0; /* sic: any key */
    }
    int nkey = nw_anim_nkey(mask, atime);
    struct nuanimcurvedata2_s *curvedata = curve->curvedata;
    int ix = curvedata->key_ixs[atime->chunk];
    switch (keytype) {
    case NUANIMKEYTYPE_HERMITE: {
        struct nuanimkey2_s *key = &curvedata->keys[ix + nkey - 1];
        struct nuanimkey2_s *next = key + 1;
        double value = nw_ee_in(key->value);
        double dt = nw_ee_sub(nw_ee_in(next->time), nw_ee_in(key->time));
        double u = nw_ee_mul(nw_ee_sub(nw_ee_in(atime->time), nw_ee_in(key->time)), nw_ee_in(key->dtime));
        double b = nw_ee_mul(nw_ee_in(key->slope), dt);
        double c = nw_ee_mul(nw_ee_in(next->slope), dt);
        double a = nw_ee_sub(value, nw_ee_in(next->value));
        double val = nw_ee_add(nw_ee_add(nw_ee_add(a, a), b), c);
        val = nw_ee_sub(nw_ee_sub(nw_ee_add(nw_ee_mul(u, val), nw_ee_mul(a, -3.0)), nw_ee_add(b, b)), c);
        val = nw_ee_add(nw_ee_mul(u, nw_ee_add(nw_ee_mul(u, val), b)), value);
        return nw_ee_out(val);
    }
    case NUANIMKEYTYPE_SMALL:
        NuErrorProlog(nuanim_srcfile, 0x50C)("NuAnimCurve2CalcVal: not supporting NUANIMKEYTYPE_SMALL yet");
        break;
    case NUANIMKEYTYPE_STEP:
        return ((struct nuanimstepkey_s *)curvedata->keys)[ix + nkey - 1].value;
    }
    return 0.0f;
}
