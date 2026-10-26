/* gamelib/saveload, which the port replaces (port/units.toml): the memory card (port 0, slot 0) is the saves folder,
   always there, formatted and with room. The async calls keep retail's statuses, errors and frame steps, which
   game/loadsave's UpdateSaveSlots and the menus read: a request runs on the next saveloadASCallEachFrame, and the
   statuses after it are each seen for one frame. */
#include "port/saveload.h"

#include <stddef.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "app.h"
#include "nw_convert.h"

#define NW_SAVELOAD_FREE_KB 8000 /* an empty 8 MB card */
#define NW_SAVELOAD_PROBE 5      /* idle frames between retail's card checks */

/* The record layout the codec writes. */
#define NW_SAVELOAD_AT(type, field, at) _Static_assert(offsetof(struct type, field) == (at), #type "." #field)
#define NW_SAVELOAD_COUNT(type, field) (sizeof(((struct type *)0)->field) / sizeof(((struct type *)0)->field[0]))
_Static_assert(sizeof(struct game_s) == NW_SAVE_GAME_SIZE, "game_s");
NW_SAVELOAD_AT(game_s, name, NW_SAVE_NAME);
_Static_assert(sizeof(((struct game_s *)0)->name) == NW_SAVE_NAME_SIZE, "game_s.name");
NW_SAVELOAD_AT(game_s, vibration, NW_SAVE_VIBRATION);
NW_SAVELOAD_AT(game_s, surround, NW_SAVE_SURROUND);
NW_SAVELOAD_AT(game_s, sfx_volume, NW_SAVE_SFX_VOLUME);
NW_SAVELOAD_AT(game_s, music_volume, NW_SAVE_MUSIC_VOLUME);
NW_SAVELOAD_AT(game_s, screen_x, NW_SAVE_SCREEN_X);
NW_SAVELOAD_AT(game_s, screen_y, NW_SAVE_SCREEN_Y);
NW_SAVELOAD_AT(game_s, language, NW_SAVE_LANGUAGE);
NW_SAVELOAD_AT(game_s, hub, NW_SAVE_HUB);
_Static_assert(NW_SAVELOAD_COUNT(game_s, hub) == NW_SAVE_HUBS, "game_s.hub");
NW_SAVELOAD_AT(game_s, level, NW_SAVE_LEVEL);
_Static_assert(NW_SAVELOAD_COUNT(game_s, level) == NW_SAVE_LEVELS, "game_s.level");
NW_SAVELOAD_AT(game_s, lives, NW_SAVE_LIVES);
NW_SAVELOAD_AT(game_s, wumpas, NW_SAVE_WUMPAS);
NW_SAVELOAD_AT(game_s, mask, NW_SAVE_MASK);
NW_SAVELOAD_AT(game_s, percent, NW_SAVE_PERCENT);
NW_SAVELOAD_AT(game_s, crystals, NW_SAVE_CRYSTALS);
NW_SAVELOAD_AT(game_s, relics, NW_SAVE_RELICS);
NW_SAVELOAD_AT(game_s, crate_gems, NW_SAVE_CRATE_GEMS);
NW_SAVELOAD_AT(game_s, bonus_gems, NW_SAVE_BONUS_GEMS);
NW_SAVELOAD_AT(game_s, gems, NW_SAVE_GEMS);
NW_SAVELOAD_AT(game_s, gembits, NW_SAVE_GEMBITS);
NW_SAVELOAD_AT(game_s, powerbits, NW_SAVE_POWERBITS);
NW_SAVELOAD_AT(game_s, empty, NW_SAVE_EMPTY);
NW_SAVELOAD_AT(game_s, cutbits, NW_SAVE_CUTBITS);
_Static_assert(sizeof(struct hub_s) == NW_SAVE_HUB_SIZE, "hub_s");
NW_SAVELOAD_AT(hub_s, flags, NW_SAVE_HUB_FLAGS);
NW_SAVELOAD_AT(hub_s, crystals, NW_SAVE_HUB_CRYSTALS);
NW_SAVELOAD_AT(hub_s, pad1, NW_SAVE_HUB_PAD);
_Static_assert(sizeof(struct level_s) == NW_SAVE_LEVEL_SIZE, "level_s");
NW_SAVELOAD_AT(level_s, flags, NW_SAVE_LEVEL_FLAGS);
NW_SAVELOAD_AT(level_s, pad1, NW_SAVE_LEVEL_PAD);
NW_SAVELOAD_AT(level_s, time, NW_SAVE_LEVEL_TIME);
_Static_assert(NW_SAVELOAD_COUNT(level_s, time) == NW_SAVE_TIMES, "level_s.time");
_Static_assert(sizeof(struct time_s) == NW_SAVE_TIME_SIZE, "time_s");
NW_SAVELOAD_AT(time_s, name, NW_SAVE_TIME_NAME);
_Static_assert(sizeof(((struct time_s *)0)->name) == NW_SAVE_TIME_NAME_SIZE, "time_s.name");
NW_SAVELOAD_AT(time_s, itime, NW_SAVE_TIME_ITIME);

/* Roots of the data image the decomp reads. */
extern int saveload_status;
extern int saveload_error;
extern int saveload_cardtype;
extern int saveload_freespace;
extern int saveload_cardformatted;
extern int saveload_cardchanged;

/* Declared by no header. */
int NuFileSize(char *name);
int NuFileLoadBuffer(char *filename, void *mem, int buffsize);
extern struct ldata_s LData[];

static struct {
    char *dir; /* NULL: no card */
    NwSaveRegistry reg;
    bool reg_ready;
    int probe; /* idle frames since the last card check */
    int slot;  /* the pending request's */
    void *data;
    int size;
} nw_saveload;

void nw_saveload_init(const char *dir)
{
    SDL_free(nw_saveload.dir);
    nw_saveload.dir = dir ? SDL_strdup(dir) : NULL;
    nw_saveload.probe = 0;
}

/* Level keys from LData's paths, made on first use. */
static const NwSaveRegistry *nw_saveload_registry(void)
{
    if (!nw_saveload.reg_ready) {
        const char *paths[NW_SAVE_LEVELS];
        for (int i = 0; i < NW_SAVE_LEVELS; i++) {
            paths[i] = LData[i].path;
        }
        nw_saveload.reg_ready = nw_save_registry_init(&nw_saveload.reg, paths, NW_SAVE_LEVELS, NW_SAVE_HUBS);
    }
    return nw_saveload.reg_ready ? &nw_saveload.reg : NULL;
}

NwSaveResult nw_saveload_import(const char *dir, const char *file)
{
    const NwSaveRegistry *reg = nw_saveload_registry();
    return reg ? nw_save_import(dir, 0, file, reg) : NW_SAVE_FAILED;
}

/* What the game passes: whole records only. */
static bool nw_saveload_records(int size, size_t *count)
{
    *count = size > 0 && size % NW_SAVE_GAME_SIZE == 0 ? (size_t)(size / NW_SAVE_GAME_SIZE) : 0;
    return (*count >= 1 && *count <= NW_SAVE_RECORDS) ||
           SDL_SetError("%d bytes are not 1 to %d save records", size, NW_SAVE_RECORDS);
}

/* Card slot slot's save into data: 1 loaded, 0 none, -1 failed. */
static int nw_saveload_read(int slot, void *data, int size)
{
    const NwSaveRegistry *reg = nw_saveload.dir ? nw_saveload_registry() : NULL;
    size_t count = 0;
    if (!nw_saveload.dir) {
        return 0;
    }
    NwSaveResult r = reg && nw_saveload_records(size, &count) ? nw_save_load(nw_saveload.dir, slot, reg, data, count)
                                                              : NW_SAVE_FAILED;
    if (r == NW_SAVE_IMPORTED) {
        fprintf(stderr, NW_APP_NAME ": saves: imported %s\n", SDL_GetError());
    } else if (r == NW_SAVE_BACKUP) {
        fprintf(stderr, NW_APP_NAME ": saves: loaded the backup: %s\n", SDL_GetError());
    } else if (r == NW_SAVE_FAILED) {
        fprintf(stderr, NW_APP_NAME ": saves: cannot load: %s\n", SDL_GetError());
    }
    return r == NW_SAVE_NONE ? 0 : r == NW_SAVE_FAILED ? -1 : 1;
}

static bool nw_saveload_write(int slot, const void *data, int size)
{
    const NwSaveRegistry *reg = nw_saveload.dir ? nw_saveload_registry() : NULL;
    size_t count = 0;
    bool ok = reg && nw_saveload_records(size, &count) && nw_save_store(nw_saveload.dir, slot, reg, data, count);
    if (!ok && nw_saveload.dir) {
        fprintf(stderr, NW_APP_NAME ": saves: cannot save: %s\n", SDL_GetError());
    }
    return ok;
}

/* 1 deleted, 0 none, -1 failed. */
static int nw_saveload_delete(int slot)
{
    NwSaveResult r = nw_saveload.dir ? nw_save_remove(nw_saveload.dir, slot) : NW_SAVE_NONE;
    if (r == NW_SAVE_FAILED) {
        fprintf(stderr, NW_APP_NAME ": saves: cannot delete: %s\n", SDL_GetError());
    }
    return r == NW_SAVE_OK ? 1 : r == NW_SAVE_NONE ? 0 : -1;
}

/* sceMcGetInfo's answer. */
static void nw_saveload_info(void)
{
    bool card = nw_saveload.dir != NULL;
    saveload_cardtype = card ? 2 : -1;
    saveload_freespace = card ? NW_SAVELOAD_FREE_KB : -1;
    saveload_cardformatted = card ? 1 : -1;
}

void saveloadASCallEachFrame(void)
{
    int r;
    switch (saveload_status) {
    case 0: /* the first card check */
        nw_saveload_info();
        saveload_status = 3;
        break;
    case 1:
        if (++nw_saveload.probe >= NW_SAVELOAD_PROBE) {
            nw_saveload.probe = 0;
            saveload_status = 2;
        }
        break;
    case 2: /* a card check done: the same card */
    case 3:
        nw_saveload_info();
        saveload_error = 0;
        saveload_cardchanged = 0;
        saveload_status = 1;
        break;
    case 4: /* a format: nothing to erase */
        nw_saveload_info();
        saveload_error = 0;
        saveload_status = 3;
        break;
    case 5:
        r = nw_saveload_read(nw_saveload.slot, nw_saveload.data, nw_saveload.size);
        saveload_error = r > 0 ? 0 : r == 0 ? 5 : 2;
        saveload_status = 1;
        break;
    case 9:
        if (nw_saveload_write(nw_saveload.slot, nw_saveload.data, nw_saveload.size)) {
            nw_saveload_info();
            saveload_error = 0;
            saveload_status = 3;
        } else {
            saveload_error = 3;
            saveload_status = 1;
        }
        break;
    case 0x19:
        if (nw_saveload_delete(nw_saveload.slot) > 0) {
            nw_saveload_info();
            saveload_error = 0;
            saveload_status = 3;
        } else {
            saveload_error = 4;
            saveload_status = 1;
        }
        break;
    }
}

void saveloadASFormat(void)
{
    if (saveload_status == 1) {
        saveload_status = 4;
    }
}

static void nw_saveload_request(int slot, void *data, int size, int status)
{
    if (slot < NW_SAVE_CARD_SLOTS) {
        nw_saveload.slot = slot;
        nw_saveload.data = data;
        nw_saveload.size = size;
        saveload_status = status;
    }
}

void saveloadASLoad(int slot, void *data, int size)
{
    nw_saveload_request(slot, data, size, 5);
}

void saveloadASSave(int slot, void *data, int size)
{
    nw_saveload_request(slot, data, size, 9);
}

void saveloadASDelete(int slot)
{
    nw_saveload_request(slot, NULL, 0, 0x19);
}

int saveloadCheckCardPresent(void)
{
    return nw_saveload.dir != NULL;
}

/* Retail's directory count: the folder's marker file and one file per saved slot, 0 without a save. */
int saveloadGetDirectory(void)
{
    int n = 0;
    if (!nw_saveload.dir) {
        return -1;
    }
    for (int slot = 0; slot < NW_SAVE_CARD_SLOTS; slot++) {
        n += nw_save_exists(nw_saveload.dir, slot);
    }
    return n ? n + 1 : 0;
}

int saveloadSaveSlot(int slot, void *buf, int size)
{
    return slot < NW_SAVE_CARD_SLOTS && nw_saveload_write(slot, buf, size);
}

int saveloadDeleteSlot(int slot)
{
    return slot < NW_SAVE_CARD_SLOTS && nw_saveload_delete(slot) > 0;
}

int saveloadLoadIcon(char *name, void *buf)
{
    int size = NuFileSize(name);
    NuFileLoadBuffer(name, buf, size);
    return size;
}

int saveloadCheckCardType(void)
{
    return nw_saveload.dir != NULL;
}

int saveloadCheckCardFormatted(void)
{
    return nw_saveload.dir != NULL;
}

int saveloadCheckCardFreeSpace(int size)
{
    if (!nw_saveload.dir) {
        return 0;
    }
    return size <= 0 ? NW_SAVELOAD_FREE_KB : NW_SAVELOAD_FREE_KB >= size;
}

int saveloadFormatCard(void)
{
    return nw_saveload.dir != NULL;
}

int saveloadUnFormatCard(void)
{
    return 0;
}

int saveloadCheckSlotsUsed(void)
{
    int n = saveloadGetDirectory();
    return n > 0 ? n - 1 : 0;
}

int saveloadLoadSlot(int slot, void *buf, int size)
{
    return slot < NW_SAVE_CARD_SLOTS && nw_saveload_read(slot, buf, size) > 0;
}
