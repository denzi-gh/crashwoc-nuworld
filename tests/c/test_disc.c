/* src/port/disc.c: the disc's region and a US level file's sound numbers in PAL's numbering. */
#include <stdio.h>
#include <string.h>

#include "data/image.h"
#include "game/sfx.h"
#include "port/disc.h"
#include "shim/shim.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

extern GameSound *edSfxLevelTab;
extern int edSfxGlobalCount;

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

typedef struct {
    const char *name;
    GameSound *host;
} Find;

static void find(const NwImageObj *o, void *user)
{
    Find *f = user;
    if (strcmp(o->name, f->name) == 0) {
        f->host = (GameSound *)o->host;
    }
}

/* A PAL sound table of the image, by name. */
static GameSound *table(const char *name)
{
    Find f = { name, NULL };
    nw_image_each(find, &f);
    if (!f.host) {
        fprintf(stderr, "no %s in the data image\n", name);
        failures++;
    }
    return f.host;
}

int main(void)
{
    unsigned char elf[52] = { 0x7f, 'E', 'L', 'F', 1, 1, 1 };
    elf[18] = 8;  /* EM_MIPS */
    elf[28] = 52; /* e_phoff */
    elf[42] = 32; /* e_phentsize */
    NwImageStats stats;
    char err[512];
    if (!nw_image_load_disc(&nw_image_tables, "USA v1.00", elf, sizeof(elf), &stats, err, sizeof(err))) {
        fprintf(stderr, "data image of USA v1.00: %s\n", err);
        return 1;
    }
    NwVfs vfs = { .manifest = &nw_disc_manifest_usa100 };
    nw_shim_file_init(&vfs);
    CHECK(nw_disc_ntsc());
    edSfxGlobalCount = 197;

    /* Global sounds keep their number; none follows the US table's 177. */
    edSfxLevelTab = table("SfxTabLIZARDS");
    CHECK(nw_disc_sfx(-1) == -1 && nw_disc_sfx(0) == 0 && nw_disc_sfx(121) == 121 && nw_disc_sfx(176) == 176);
    /* A level's sounds follow PAL's global table; past the level's rows nothing plays. */
    CHECK(nw_disc_sfx(177) == 197 && nw_disc_sfx(186) == 206 && nw_disc_sfx(187) == 207 && nw_disc_sfx(188) == -1);
    edSfxLevelTab = table("SfxTabTSUNAMI");
    CHECK(nw_disc_sfx(179) == 199); /* TOONARMY.PTL's BoatRock: BoatCreak */
    edSfxLevelTab = table("SfxTabVOID");
    CHECK(nw_disc_sfx(183) == -1);  /* DROID.PTL's PINK_GAS, past the US table on the US disc too */
    /* PAL's extra voices sit inside these two tables. */
    edSfxLevelTab = table("SfxTabOIDS");
    CHECK(nw_disc_sfx(179) == 199 && nw_disc_sfx(180) == 205 && nw_disc_sfx(193) == 218 && nw_disc_sfx(194) == -1);
    edSfxLevelTab = table("SfxTabFRONTEND");
    CHECK(nw_disc_sfx(188) == 208 && nw_disc_sfx(189) == 269 && nw_disc_sfx(196) == 276 && nw_disc_sfx(197) == -1);
    edSfxLevelTab = NULL;
    CHECK(nw_disc_sfx(121) == 121 && nw_disc_sfx(177) == -1);

    /* PAL's own files are PAL's numbering. */
    vfs.manifest = &nw_disc_manifest;
    edSfxLevelTab = table("SfxTabVOID");
    CHECK(!nw_disc_ntsc() && nw_disc_sfx(183) == 183 && nw_disc_sfx(-1) == -1);
    nw_shim_file_init(NULL);
    CHECK(nw_disc_sfx(183) == 183);
    nw_image_free();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
