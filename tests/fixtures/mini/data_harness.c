/* `data_harness <elf>` loads the fixture's data image from a synthetic ELF (tests/test_gen_dataimage.py);
   `data_harness <elf> bad` expects the load to fail and prints why. */
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
s32 FixAdd(s32 a, s32 b);
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
    if (argc > 2) {
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
    CHECK(fixroot.next && fixroot.next->i == 77 && fixroot.next->next == &fixroot); /* region, cycle */
    CHECK(fixpairs[1].x == 3.5f && ((uintptr_t)fixpairs & 15) == 0);       /* copied, aligned */
    CHECK(D_00100C08[0] == 3 && nw_image_host(0x00100c08u) == (void *)D_00100C08); /* alias into C data */
    CHECK(fixtent == 0);
    CHECK(FixDataUse() == 'h' + 'h' + 0x1234 + 0x1234 + 3 + 3 + 7 + 1 + 0);
    CHECK(st.roots[NW_ROOT_MIRROR] == 1 && st.roots[NW_ROOT_COPY] == 1 && st.roots[NW_ROOT_CONVERT] == 2 &&
          st.roots[NW_ROOT_ALIAS] == 2);
    CHECK(st.objects == 2 && st.regions == 1 && st.code_pointers == 1);
    nw_image_report(&st, stdout);
    printf("ok\n");
    return 0;
}
