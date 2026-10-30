/* The libc, newlib and libgcc names the decomp calls that the host libc lacks or spells otherwise
   (nw_strlwr and nw_divdi3 through port/nw_prelude.h). */
#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "shim/shim.h"

/* The decomp's newlib srand (decomp src/sdk/newlib/libc/stdlib/rand.c, renamed by port/nw_newlib.h). */
void nw_srand(unsigned int seed);

/* newlib's reent block (rand's seed, the decomp libm's errno as first member); retail's impure_data holds an SDK code
   pointer (_cleanup_r), so the port owns it (port/data.toml [owned]). */
static _Alignas(16) unsigned char nw_reent[1024];
void *_impure_ptr[1] = { nw_reent };

int *__errno(void)
{
    return (int *)_impure_ptr[0];
}

void nw_newlib_init(void)
{
    memset(nw_reent, 0, sizeof nw_reent);
    nw_srand(1); /* retail starts from _REENT_INIT's seed 1 */
}

char *nw_strlwr(char *s)
{
    for (char *p = s; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    return s;
}

long long nw_divdi3(long long u, long long v)
{
    return u / v;
}

/* libgcc fp-bit's float -> double, which vehsupp.c calls by name: the double's bits. */
uint64_t fptodp(float x)
{
    double d = x;
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    return bits;
}
