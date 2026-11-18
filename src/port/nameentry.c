/* The names typed in game/game's name entries (port/nameentry.h). */
#include "port/nameentry.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "nu3d/nugscn.h"
#include "nu_types.h"
#include "nups2/ps2ctl.h"
#include "port/names.h"

#define NW_NAMEENTRY_GAME 2      /* game/game's NEW GAME name entry */
#define NW_NAMEENTRY_TIME 15     /* and a time trial record's */
#define NW_NAMEENTRY_LETTERS 4   /* rows of NameInputTable; DONE is the next one */
#define NW_NAMEENTRY_QUEUE 32
#define NW_NAMEENTRY_CROSS 0x40u /* ProcMenu's buttons */
#define NW_NAMEENTRY_TRIANGLE 0x10u
#define NW_NAMEENTRY_SFX 0x36    /* the sound of a letter put in */

/* Declared by no header. */
extern struct game_s Game;
extern struct objtab_s ObjTab[];
extern char NameInputTable[4][7];
extern s32 i_nameinput, cutmovie, Level, newleveltime_slot;
extern f32 GAMENAMEY;
char *MakeEditText(char *txt);
void GameSfx(s32 sfx, void *p);
void DrawNameInputTable(struct cursor_s *cursor, f32 x0, f32 y0);
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
s32 DrawPanel3DObject(s32 object, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, u16 xrot, u16 yrot,
                      u16 zrot, struct nugscn_s *scn, struct nuspecial_s *obj, s32 rot);

static struct {
    SDL_SpinLock lock;
    SDL_AtomicInt wanted, keyboard;
    char queue[NW_NAMEENTRY_QUEUE];
    int head, count;
    /* the entry open (game thread): its menu, the longer name typed into (letters, '_' for the rest), its longest,
       and the game's own name that takes its first letters */
    int menu;
    char *name;
    int max;
    char *own;
    int own_width;
} nw_nameentry;

bool nw_nameentry_wanted(void)
{
    return SDL_GetAtomicInt(&nw_nameentry.wanted) != 0;
}

void nw_nameentry_set_keyboard(bool keyboard)
{
    SDL_SetAtomicInt(&nw_nameentry.keyboard, keyboard);
}

bool nw_nameentry_keyboard(void)
{
    return SDL_GetAtomicInt(&nw_nameentry.keyboard) != 0;
}

void nw_nameentry_type(int c)
{
    if (c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
    }
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == NW_NAMEENTRY_BACK ||
          c == NW_NAMEENTRY_DONE || c == NW_NAMEENTRY_QUIT) ||
        !nw_nameentry_wanted()) {
        return;
    }
    SDL_LockSpinlock(&nw_nameentry.lock);
    if (nw_nameentry.count < NW_NAMEENTRY_QUEUE) {
        nw_nameentry.queue[(nw_nameentry.head + nw_nameentry.count++) % NW_NAMEENTRY_QUEUE] = (char)c;
    }
    SDL_UnlockSpinlock(&nw_nameentry.lock);
}

/* The next typed key, -1 for none. */
static int nw_nameentry_next(void)
{
    int c = -1;
    SDL_LockSpinlock(&nw_nameentry.lock);
    if (nw_nameentry.count > 0) {
        c = (unsigned char)nw_nameentry.queue[nw_nameentry.head];
        nw_nameentry.head = (nw_nameentry.head + 1) % NW_NAMEENTRY_QUEUE;
        nw_nameentry.count--;
    }
    SDL_UnlockSpinlock(&nw_nameentry.lock);
    return c;
}

/* The entry of menu, 0 for none: the name it fills, as NewMenu and NewLevelTime left it ('_' everywhere). */
static void nw_nameentry_open(int menu)
{
    nw_nameentry.menu = menu;
    if (menu == NW_NAMEENTRY_GAME) {
        nw_nameentry.name = nw_names_game()->game;
        nw_nameentry.max = NW_NAMES_GAME;
        nw_nameentry.own = Game.name;
        nw_nameentry.own_width = NW_NAMES_OWN_GAME;
    } else if (menu == NW_NAMEENTRY_TIME) {
        nw_nameentry.name = nw_names_game()->time[Level][newleveltime_slot];
        nw_nameentry.max = nw_names_time_max();
        nw_nameentry.own = Game.level[Level].time[newleveltime_slot].name;
        nw_nameentry.own_width = NW_NAMES_OWN_TIME;
    } else {
        nw_nameentry.name = NULL;
        return;
    }
    SDL_memset(nw_nameentry.name, '_', (size_t)nw_nameentry.max);
    nw_nameentry.name[nw_nameentry.max] = 0;
}

/* The entry closed: its longer name as the game cleans its own (CleanLetters), the spaces after it gone. */
static void nw_nameentry_close(void)
{
    char *name = nw_nameentry.name;
    if (name) {
        for (char *c = name; *c; c++) {
            *c = *c == '_' ? ' ' : *c;
        }
        size_t n = SDL_strlen(name);
        while (n && name[n - 1] == ' ') {
            name[--n] = 0;
        }
    }
    nw_nameentry.menu = 0;
    nw_nameentry.name = NULL;
}

static void nw_nameentry_want(bool want)
{
    if (want != nw_nameentry_wanted()) {
        SDL_LockSpinlock(&nw_nameentry.lock);
        nw_nameentry.head = nw_nameentry.count = 0; /* nothing typed before carries over */
        SDL_UnlockSpinlock(&nw_nameentry.lock);
        SDL_SetAtomicInt(&nw_nameentry.wanted, want);
    }
}

void nw_nameentry_finish(void)
{
    nw_nameentry_close();
    nw_nameentry_want(false);
}

void nw_nameentry_resume_game(const char *name, int position)
{
    nw_nameentry_open(NW_NAMEENTRY_GAME);
    SDL_strlcpy(nw_nameentry.name, name, NW_NAMES_GAME + 1);
    i_nameinput = position;
    nw_nameentry_want(true);
}

/* Retail's InputNewLetter on the longer name ('_' is a space), with its '<'; the game's own name takes the first
   letters. Returns whether the name is full (retail's cursor goes to DONE then). */
static bool nw_nameentry_letter(int c)
{
    bool full = false;
    if (c == NW_NAMEENTRY_BACK || c == '<') {
        if (i_nameinput > 0) {
            nw_nameentry.name[--i_nameinput] = '_';
        }
    } else {
        int last = nw_nameentry.max - 1;
        full = i_nameinput >= last;
        nw_nameentry.name[full ? last : i_nameinput] = c == ' ' ? '_' : (char)c;
        if (i_nameinput < nw_nameentry.max) {
            i_nameinput++;
        }
    }
    SDL_memcpy(nw_nameentry.own, nw_nameentry.name, (size_t)nw_nameentry.own_width);
    nw_nameentry.own[nw_nameentry.own_width] = 0;
    return full;
}

/* The entry's DONE row: menu 2 has CANCEL after it. */
static int nw_nameentry_done_row(const struct cursor_s *cursor)
{
    return nw_nameentry.menu == NW_NAMEENTRY_GAME ? cursor->y_max - 1 : cursor->y_max;
}

uint32_t nw_nameentry_proc(struct cursor_s *cursor, struct nupad_s *pad, uint32_t *taken)
{
    int menu = (u32)Game.language < 6 && (cursor->menu == NW_NAMEENTRY_GAME || cursor->menu == NW_NAMEENTRY_TIME)
                   ? cursor->menu
                   : 0;
    *taken = 0;
    if (menu != nw_nameentry.menu) {
        nw_nameentry_close();
        nw_nameentry_open(menu);
    }
    nw_nameentry_want(menu != 0);
    /* retail takes no button on a menu's first frame, while it waits or while a button is locked */
    if (!menu || !pad || cursor->wait || cursor->item_frame == 0 || cursor->button_lock > 1) {
        return 0;
    }
    uint32_t press = 0;
    bool typed = false;
    if ((pad->paddata_db & NW_NAMEENTRY_CROSS) && cursor->y < NW_NAMEENTRY_LETTERS) {
        /* the table's letter, into the longer name instead of retail's 8 or 3 */
        if (nw_nameentry_letter((unsigned char)NameInputTable[cursor->y][cursor->x])) {
            cursor->y = (signed char)nw_nameentry_done_row(cursor);
        }
        *taken = NW_NAMEENTRY_CROSS;
        pad->paddata_db &= ~NW_NAMEENTRY_CROSS;
        typed = true;
    }
    int c;
    while (!press && (c = nw_nameentry_next()) >= 0) {
        if (c == NW_NAMEENTRY_DONE) {
            cursor->y = (signed char)nw_nameentry_done_row(cursor); /* retail's CROSS there finishes the name */
            press = NW_NAMEENTRY_CROSS;
        } else if (c == NW_NAMEENTRY_QUIT) {
            press = menu == NW_NAMEENTRY_GAME ? NW_NAMEENTRY_TRIANGLE : 0; /* a record's name has no way back */
        } else {
            nw_nameentry_letter(c);
            typed = true;
        }
    }
    if (typed) {
        GameSfx(NW_NAMEENTRY_SFX, NULL);
    }
    press &= ~pad->paddata_db; /* only what the pad does not press itself */
    pad->paddata_db |= press;
    return press;
}

void nw_nameentry_done(struct nupad_s *pad, uint32_t pressed, uint32_t taken)
{
    if (pad) {
        pad->paddata_db = (pad->paddata_db & ~pressed) | taken;
    }
}

const char *nw_nameentry_name(void)
{
    return nw_nameentry.name;
}

bool nw_nameentry_draw(struct cursor_s *cursor)
{
    if ((u32)Game.language >= 6 || cursor->menu != nw_nameentry.menu || !nw_nameentry.name) {
        return false;
    }
    bool keyboard = nw_nameentry_wanted() && nw_nameentry_keyboard();
    if (cursor->menu == NW_NAMEENTRY_TIME) {
        return keyboard; /* retail draws the table alone; the name is in the times panel (NW_TIME_NAME) */
    }
    /* retail's menu 2 with the longer name, the letters without the keyboard */
    if (!keyboard) {
        DrawNameInputTable(cursor, 0.0f, -0.5f);
    } else if (cutmovie == -1) {
        DrawPanel3DObject(0x81, 0.0f, 0.5f, 1.0f, 0.2f, 0.125f, 0.06f, 0xC000, 0, 0, ObjTab[129].obj.scene,
                          ObjTab[129].obj.special, 0);
    }
    f32 size = nw_names_scale(1.0f, nw_nameentry.name, NW_NAMES_OWN_GAME + 2);
    Text3D(MakeEditText(nw_nameentry.name), 0.0f, GAMENAMEY, 1.0f, size, size, size, 0, 4);
    return true;
}
