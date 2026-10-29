#include "port/hz.h"

#include "nu_port.h"

int nw_hz = 50;

int nw_hz_set(int hz)
{
    nw_hz = hz == 60 ? 60 : 50;
    return nw_hz;
}

unsigned nw_hz_line_ns(void)
{
    return nw_hz == 60 ? 63556u : 64000u;
}
