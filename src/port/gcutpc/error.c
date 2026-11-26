/* The reason a CSP could not be read. */
#include <stdarg.h>
#include <stdio.h>

#include "port/gcutpc/internal.h"

bool NuGCutPCFail(NUGCUTPCERROR_s *error, const char *format, ...)
{
    if (error) {
        va_list args;
        va_start(args, format);
        vsnprintf(error->text, sizeof(error->text), format, args);
        va_end(args);
    }
    return false;
}
