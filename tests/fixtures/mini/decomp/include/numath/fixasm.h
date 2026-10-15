#ifndef NUMATH_FIXASM_H
#define NUMATH_FIXASM_H

#include "nu_types.h"

struct fixvec_s {
    f32 x, y, z;
};

void FixAsmA(struct fixvec_s *v);

#endif
