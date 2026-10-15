#include "nu_types.h"

enum fixmode_e { FIX_A, FIX_B };

struct fixobj_s {
    s32 hp;
};

extern void FixOther(struct fixobj_s *obj, enum fixmode_e mode, f32 t);

void FixGameTick(struct fixobj_s *obj)
{
    FixOther(obj, FIX_B, 0.5f);
}
