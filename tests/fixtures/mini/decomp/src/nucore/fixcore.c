#include "nucore/fixcore.h"

static s32 FixHelper(s32 v)
{
    return v * 2;
}

s32 FixAdd(s32 a, s32 b)
{
    return a + b;
}

s32 FixNear(s32 a)
{
    return FixHelper(a) + FixMissing(a, 1.0f);
}

s32 FixLoad(const char *name)
{
    return name[0];
}
