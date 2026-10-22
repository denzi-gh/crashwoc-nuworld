/* `data_harness <elf>` loads the fixture's data image from a synthetic ELF (tests/test_gen_dataimage.py) and
   converts it back; `data_harness <elf> bad` expects the load to fail and prints why; `data_harness <elf> trip`
   expects the way back to list one difference in the image and one in the C data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/image.h"
#include "nucore/fixtypes.h"

extern char D_00100B00[];
extern char *fixnames[];
extern struct fixnode_s fixroot;
extern s32 D_00100A04[];
extern FixPair fixpairs[2];
extern s32 D_00100C08[];
extern s32 fixtent;
extern char vu_load_fixa_mpg[];
extern char D_00100680[];
extern void *fixmpgs[2];
extern fixcb_t fixcbs[2];
extern nuintptr fixaddrs[2];
s32 FixAdd(s32 a, s32 b);
s32 FixMissing(s32 a, f32 b);
s32 FixDataUse(void);

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "data_harness: check failed: %s\n", #cond); \
            return 1; \
        } \
    } while (0)

int main(int argc, char *argv[])
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        fprintf(stderr, "data_harness: no ELF\n");
        return 2;
    }
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *elf = malloc(size);
    size_t got = fread(elf, 1, size, f);
    fclose(f);
    CHECK(got == size);

    NwImageStats st;
    char err[512];
    bool ok = nw_image_load(&nw_image_tables, elf, size, &st, err, sizeof(err));
    free(elf);
    if (argc > 2 && strcmp(argv[2], "bad") == 0) {
        CHECK(!ok);
        printf("%s\n", err);
        return 0;
    }
    if (!ok) {
        fprintf(stderr, "data_harness: %s\n", err);
        return 1;
    }

    CHECK(strcmp(D_00100B00, "hello") == 0);                               /* mirror */
    CHECK(fixnames[0] == D_00100B00 && strcmp(fixnames[1], "world") == 0); /* pointers into the mirror */
    CHECK(fixroot.i == 0x1234 && fixroot.named.a == 5);                    /* converted */
    CHECK((void *)D_00100A04 == (void *)&fixroot.i);                       /* alias into a converted root */
    CHECK((void (*)(void))fixroot.cb == (void (*)(void))FixAdd);         /* code pointer */
    CHECK(fixroot.next && fixroot.next->i == 77);                          /* region */
    CHECK(fixroot.next->next && fixroot.next->next->i == 88);               /* a symbol where it ends, not its end */
    CHECK(fixroot.next->next->next == &fixroot);                            /* cycle */
    CHECK(fixpairs[1].x == 3.5f && ((uintptr_t)fixpairs & 15) == 0);       /* copied, aligned */
    CHECK(D_00100C08[0] == 3 && nw_image_host(0x00100c08u) == (void *)D_00100C08); /* alias into C data */
    CHECK(fixtent == 0);
    const NwVuProg *a = nw_vu_prog(vu_load_fixa_mpg), *b = nw_vu_prog(D_00100680); /* microcode tokens */
    CHECK(a && strcmp(a->name, "vu_load_fixa_mpg") == 0 && a->addr == 0x00100600u && a->size == 0x80);
    CHECK(b && strcmp(b->name, "vu_load_fixb_mpg") == 0 && b->addr == 0x00100680u); /* a D_ name of it */
    CHECK(fixmpgs[0] == (void *)D_00100680 && fixmpgs[1] == (void *)vu_load_fixa_mpg);
    CHECK((void (*)(void))fixcbs[0] == (void (*)(void))FixMissing && nw_vu_prog((const void *)fixcbs[1]) == b);
    CHECK(fixaddrs[0] == (nuintptr)FixAdd && fixaddrs[1] == (nuintptr)vu_load_fixa_mpg);
    CHECK(nw_image_host(0x00100680u) == (void *)b && !nw_image_host(0x00100684u) && !nw_vu_prog(fixmpgs));
    CHECK(FixDataUse() == 'h' + 'h' + 0x1234 + 0x1234 + 3 + 3 + 7 + 1 + 0 + 4);
    CHECK(st.roots[NW_ROOT_MIRROR] == 1 && st.roots[NW_ROOT_COPY] == 1 && st.roots[NW_ROOT_CONVERT] == 5 &&
          st.roots[NW_ROOT_ALIAS] == 2 && st.roots[NW_ROOT_VU] == 2);
    CHECK(st.objects == 3 && st.regions == 2);
    CHECK(st.code_pointers == 3 && st.stub_pointers == 1 && st.vu_tokens == 4); /* cb, fixcbs[0], fixaddrs[0] */
    nw_image_report(&st, stdout);

    uint32_t ps2;
    CHECK(nw_image_ps2(fixroot.next->next, &ps2) && ps2 == 0x00100d28u); /* the way back */
    CHECK(nw_image_ps2(&fixpairs[1], &ps2) && ps2 == 0x00100a50u);
    CHECK(nw_image_ps2(&D_00100B00[6], &ps2) && ps2 == 0x00100b06u);
    CHECK(nw_image_ps2(a, &ps2) && ps2 == 0x00100600u);
    NwImageTrip trip;
    CHECK(nw_image_roundtrip(&trip, stdout, err, sizeof(err)));
    nw_image_trip_report(&trip, stdout);
    if (argc > 2) {
        CHECK(trip.differ[0] == 1 && trip.differ[1] == 1);
    } else {
        CHECK(trip.objects[0] == 8 && trip.differ[0] == 0); /* 5 converted roots, fixpairs, 2 regions */
        CHECK(trip.objects[1] == 3 && trip.differ[1] == 0 && trip.untyped == 0 && trip.strings == 1);
    }
    printf("ok\n");
    return 0;
}
