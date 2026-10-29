/* src/port/hz.c: the rate taken, the H-line length, and the 60 Hz table written only where the PAL value is:
   floats and ints of 1, 2 and 4 bytes, a value that is not PAL's and an address with no host. */
#include <stdio.h>
#include <string.h>

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

static unsigned char ram[32];

static void *host(uint32_t addr)
{
    return addr >= 0x1000 && addr < 0x1000 + sizeof(ram) ? &ram[addr - 0x1000] : NULL;
}

int main(void)
{
    CHECK(nw_hz == 50 && nw_hz_line_ns() == 64000u);
    CHECK(nw_hz_set(60) == 60 && nw_hz == 60 && nw_hz_line_ns() == 63556u);
    CHECK(NW_HZ(0.02f, 0.016666668f) == 0.016666668f && NW_HZ(6, 5) == 5);
    CHECK(nw_hz_set(55) == 50 && NW_HZ(6, 5) == 6);

    float gravity = -0.0072f;
    memcpy(&ram[0], &gravity, 4);
    ram[4] = 10;
    ram[6] = 0xDC, ram[7] = 0x05;                       /* 1500 */
    ram[8] = 0x5D, ram[9] = ram[10] = ram[11] = 0xFF;   /* -163 */
    ram[12] = 7;
    const NwHzPatch patches[] = {
        { "GRAVITY", 0x1000, 0, 0, 0, -0.0072f, -0.005f },
        { "frames", 0x1004, 1, 10, 12, 0.0f, 0.0f },
        { "idle", 0x1006, 2, 1500, 1800, 0.0f, 0.0f },
        { "turn", 0x1008, 4, 0xFFFFFF5Du, 0xFFFFFF78u, 0.0f, 0.0f },
        { "not retail", 0x100C, 1, 5, 6, 0.0f, 0.0f },
        { "no host", 0x2000, 1, 5, 6, 0.0f, 0.0f },
    };
    CHECK(nw_hz_apply(patches, sizeof patches / sizeof patches[0], host) == 2);
    memcpy(&gravity, &ram[0], 4);
    CHECK(gravity == -0.005f && ram[4] == 12 && ram[6] == 0x08 && ram[7] == 0x07);
    CHECK(ram[8] == 0x78 && ram[11] == 0xFF && ram[12] == 7);
    /* A second pass finds the NTSC values and leaves them. */
    CHECK(nw_hz_apply(patches, sizeof patches / sizeof patches[0], host) == 6 && ram[4] == 12);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
