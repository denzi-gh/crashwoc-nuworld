#include "nu_types.h"

struct deadthing_s {
    s32 v;
};

s32 FixDeadA(struct deadthing_s *d)
{
    return d->v;
}
