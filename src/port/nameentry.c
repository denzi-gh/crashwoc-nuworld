/* NEW GAME's name typed on the keyboard (port/nameentry.h). */
#include "port/nameentry.h"

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "nu3d/nugscn.h"
#include "nu_types.h"
#include "nups2/ps2ctl.h"

#define NW_NAMEENTRY_MENU 2      /* game/game's NEW GAME name entry */
#define NW_NAMEENTRY_LENGTH 8    /* its InputNewLetter count: Game.name's letters */
#define NW_NAMEENTRY_QUEUE 32
#define NW_NAMEENTRY_CROSS 0x40u /* ProcMenu's buttons */
#define NW_NAMEENTRY_TRIANGLE 0x10u
#define NW_NAMEENTRY_SFX 0x36    /* the sound of a letter put in */

/* Declared by no header. */
extern struct game_s Game;
extern struct objtab_s ObjTab[];
extern s32 i_nameinput, cutmovie;
extern f32 GAMENAMEY;
char *MakeEditText(char *txt);
void GameSfx(s32 sfx, void *p);
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
s32 DrawPanel3DObject(s32 object, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, u16 xrot, u16 yrot,
                      u16 zrot, struct nugscn_s *scn, struct nuspecial_s *obj, s32 rot);

static struct {
    SDL_SpinLock lock;
    SDL_AtomicInt wanted, keyboard;
    char queue[NW_NAMEENTRY_QUEUE];
    int head, count;
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

static void nw_nameentry_want(bool want)
{
    if (want != nw_nameentry_wanted()) {
        SDL_LockSpinlock(&nw_nameentry.lock);
        nw_nameentry.head = nw_nameentry.count = 0; /* nothing typed before carries over */
        SDL_UnlockSpinlock(&nw_nameentry.lock);
        SDL_SetAtomicInt(&nw_nameentry.wanted, want);
    }
}

/* Retail's InputNewLetter for a typed letter ('_' is a space in the name), and its '<'. */
static void nw_nameentry_letter(int c)
{
    if (c == NW_NAMEENTRY_BACK) {
        if (i_nameinput > 0) {
            Game.name[--i_nameinput] = '_';
        }
        return;
    }
    int at = i_nameinput >= NW_NAMEENTRY_LENGTH - 1 ? NW_NAMEENTRY_LENGTH - 1 : i_nameinput;
    Game.name[at] = c == ' ' ? '_' : (char)c;
    if (i_nameinput < NW_NAMEENTRY_LENGTH) {
        i_nameinput++;
    }
}

uint32_t nw_nameentry_proc(struct cursor_s *cursor, struct nupad_s *pad)
{
    bool open = cursor->menu == NW_NAMEENTRY_MENU && (u32)Game.language < 6;
    nw_nameentry_want(open);
    /* retail takes no button on a menu's first frame or while it waits (ProcMenu's item_frame) */
    if (!open || !pad || cursor->wait || cursor->item_frame == 0) {
        return 0;
    }
    uint32_t press = 0;
    bool typed = false;
    int c;
    while (!press && (c = nw_nameentry_next()) >= 0) {
        if (c == NW_NAMEENTRY_DONE) {
            cursor->y = cursor->y_max - 1; /* DONE: retail's CROSS there cleans the name and starts the game */
            press = NW_NAMEENTRY_CROSS;
        } else if (c == NW_NAMEENTRY_QUIT) {
            press = NW_NAMEENTRY_TRIANGLE;
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

void nw_nameentry_done(struct nupad_s *pad, uint32_t pressed)
{
    if (pad) {
        pad->paddata_db &= ~pressed;
    }
}

bool nw_nameentry_draw(struct cursor_s *cursor)
{
    if (cursor->menu != NW_NAMEENTRY_MENU || (u32)Game.language >= 6 || !nw_nameentry_wanted() ||
        !nw_nameentry_keyboard()) {
        return false;
    }
    /* retail's menu 2 without DrawNameInputTable's letters: the name and its panel */
    if (cutmovie == -1) {
        DrawPanel3DObject(0x81, 0.0f, 0.5f, 1.0f, 0.2f, 0.125f, 0.06f, 0xC000, 0, 0, ObjTab[129].obj.scene,
                          ObjTab[129].obj.special, 0);
    }
    Text3D(MakeEditText(Game.name), 0.0f, GAMENAMEY, 1.0f, 1.0f, 1.0f, 1.0f, 0, 4);
    return true;
}
