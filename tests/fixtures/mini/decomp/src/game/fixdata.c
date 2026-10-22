#include "nu_types.h"
#include "nucore/fixtypes.h"

extern char D_00100B00[];          /* "hello" "world": plain, unsized (mirror) */
extern char *fixnames[];           /* two pointers into it (converted) */
extern struct fixnode_s fixroot;   /* a record with pointers (converted) */
extern s32 D_00100A04[];           /* fixroot's union (an alias into a converted root) */
extern FixPair fixpairs[2];        /* 16 bytes at an 8-aligned address (copied) */
extern s32 D_00100C08[];           /* fixstatic[2] (an alias into C data) */
extern char vu_load_fixa_mpg[];    /* a VU1 microcode package (a token) */
extern char D_00100680[];          /* the next package, by its address */
extern void *fixmpgs[2];           /* pointers to both packages (converted) */
extern fixcb_t fixcbs[2];          /* a stub and a package as function pointers (converted) */
extern nuintptr fixaddrs[2];       /* a function and a package as addresses (converted) */
extern s32 fixowned;               /* the port's (port/data.toml) */
extern s32 fixhidden;              /* only in FixDataHidden, which the port overrides */

static s32 fixstatic[4] = { 1, 2, 3, 4 };
char *fixgreet = "world"; /* a string literal: the round trip matches it by its text */
s32 fixtent;

s32 FixDataUse(void)
{
    return D_00100B00[0] + fixnames[0][0] + fixroot.i + D_00100A04[0] + (s32)fixpairs[1].x + D_00100C08[0]
           + fixowned + fixstatic[0] + fixtent + (fixmpgs[1] == vu_load_fixa_mpg) + (fixmpgs[0] == D_00100680)
           + (fixcbs[0] != 0) + (fixaddrs[0] != 0);
}

s32 FixDataHidden(void)
{
    return fixhidden;
}
