/* gen/nw_ps2types.h: every named bitfield sets exactly its PS2 bit range; the layouts are _Static_asserts there. */
#include <stdio.h>
#include <stdlib.h>

#include "nw_ps2types.h"

typedef struct NwPs2Bitfield {
    const char *path;
    size_t size;
    void (*set)(void *p, int v);
    unsigned bit, bits;
} NwPs2Bitfield;

#include "nw_ps2types_test.inc"

int main(void)
{
    int failures = 0, count = 0;
    for (const NwPs2Bitfield *bf = nw_ps2_bitfields; bf->path; bf++, count++) {
        unsigned char *bytes = calloc(1, bf->size);
        if (!bytes) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        bf->set(bytes, -1);
        for (unsigned i = 0; i < bf->size * 8; i++) {
            int want = i >= bf->bit && i < bf->bit + bf->bits;
            int got = (bytes[i / 8] >> (i % 8)) & 1;
            if (got != want) {
                fprintf(stderr, "%s: bit %u is %d, the PS2 range is [%u, %u)\n", bf->path, i, got, bf->bit,
                        bf->bit + bf->bits);
                failures++;
                break;
            }
        }
        free(bytes);
    }
    printf("%d bitfields, %d wrong\n", count, failures);
    return failures != 0;
}
