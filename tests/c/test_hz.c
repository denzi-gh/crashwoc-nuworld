/* src/port/hz.c: the rate taken, the H-line length and the seams' values at either rate. */
#include <stdio.h>

#include "nu_port.h"
#include "port/hz.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

int main(void)
{
    CHECK(nw_hz == 50 && nw_hz_line_ns() == 64000u);
    CHECK(nw_hz_set(60) == 60 && nw_hz == 60 && nw_hz_line_ns() == 63556u);
    CHECK(NW_HZ(0.02f, 0.016666668f) == 0.016666668f && NW_HZ(6, 5) == 5);
    CHECK(nw_hz_set(55) == 50 && NW_HZ(6, 5) == 6);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
