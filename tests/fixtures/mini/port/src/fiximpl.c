#include "nucore/fixcore.h"

s32 FixLoad(const char *name)
{
    (void)name;
    return 42;
}

s32 FixLocal(s32 v)
{
    return v * 10;
}

void FixAsmB(int x)
{
    (void)x;
}
