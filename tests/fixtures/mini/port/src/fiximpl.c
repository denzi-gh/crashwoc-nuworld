#include "nucore/fixcore.h"
#include "nucore/fixcore.retail.h"

s32 FixLoad(const char *name)
{
    (void)name;
    return 42;
}

s32 FixLocal(s32 v)
{
    return FixLocal__retail(v) * 10;
}

void FixAsmB(int x)
{
    (void)x;
}

s32 fixowned = 7;
