#include "nu_types.h"
#include "nucore/fixtypes.h"

extern char D_00100B00[];          /* "hello" "world": plain, unsized (mirror) */
extern char *fixnames[];           /* two pointers into it (converted) */
extern struct fixnode_s fixroot;   /* a record with pointers (converted) */
extern s32 D_00100A04[];           /* fixroot's union (an alias into a converted root) */
extern FixPair fixpairs[2];        /* 16 bytes at an 8-aligned address (copied) */
extern s32 D_00100C08[];           /* fixstatic[2] (an alias into C data) */
extern s32 fixowned;               /* the port's (port/data.toml) */
extern s32 fixhidden;              /* only in FixDataHidden, which the port overrides */

static s32 fixstatic[4] = { 1, 2, 3, 4 };
s32 fixtent;

s32 FixDataUse(void)
{
    return D_00100B00[0] + fixnames[0][0] + fixroot.i + D_00100A04[0] + (s32)fixpairs[1].x + D_00100C08[0]
           + fixowned + fixstatic[0] + fixtent;
}

s32 FixDataHidden(void)
{
    return fixhidden;
}
