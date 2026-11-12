/* The save menus, the Xbox version's way (port/savemenu.h). game/loadsave is replaced (port/units.toml): its
   DrawGameSlot and DrawGameSlots are here, retail's slot with the time and date its game was saved, the date in the
   order of the disc's region (NTSC: month first). */
#include "port/savemenu.h"

#include <stdint.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "nu3d/nugscn.h"
#include "nu_types.h"
#include "nups2/ps2ctl.h"
#include "port/menu.h"
#include "port/names.h"
#include "port/saves.h"
#include "shim/shim.h"
#include "vfs/manifest.h"
#include "vfs/vfs.h"

#define NW_SAVEMENU_ARROW_X 0.85f /* the page arrows, beside the grid */
#define NW_SAVEMENU_DX 0.7f       /* retail's SLOTPANELDX, SLOTPANELDY, SLOTPANELSX and SLOTPANELSZ */
#define NW_SAVEMENU_DY 0.65f
#define NW_SAVEMENU_SX 0.12f
#define NW_SAVEMENU_SZ 0.08f

/* Declared by no header. */
extern struct game_s Game;
extern struct game_s SaveSlot[2][2];
extern struct cursor_s Cursor;
extern struct gtimer_s GlobalTimer;
extern struct objtab_s ObjTab[];
extern s8 CLetter[];
extern char tbuf[128];
extern char *tEMPTY[6];
extern s32 stick_bits_db;
extern s32 fadeval, Paused;

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
void CalculateGamePercentage(struct game_s *game);
void AddSpacesIntoText(char *text, u32 flags);
s32 DrawPanel3DObject(s32 object, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, u16 xrot, u16 yrot,
                      u16 zrot, struct nugscn_s *scn, struct nuspecial_s *obj, s32 rot);

static struct {
    bool deck;
    int menu; /* the menu as last seen: a save grid is entered when it changes to one */
} nw_savemenu = { .menu = -1 };

void nw_savemenu_platform(bool deck)
{
    nw_savemenu.deck = deck;
}

/* When g's game was saved: a slot's from its file, the game in play's now (the load/save menu, a save's confirm). */
static void nw_savemenu_when(const struct game_s *g, NwSaveTime *t)
{
    uintptr_t at = (uintptr_t)g - (uintptr_t)&SaveSlot[0][0];
    SDL_zerop(t);
    if (at < sizeof(SaveSlot)) {
        *t = *nw_saves_time((int)(at / sizeof(struct game_s)));
    } else if (g == &Game) {
        SDL_Time now = 0;
        SDL_GetCurrentTime(&now);
        nw_save_time_from(now, t);
    }
}

static bool nw_savemenu_ntsc(void)
{
    const NwVfs *vfs = nw_shim_file_vfs();
    return vfs && vfs->manifest && vfs->manifest->vmode && SDL_strcmp(vfs->manifest->vmode, "NTSC") == 0;
}

static void nw_savemenu_stat(const char *fmt, int value, int letter, f32 x, f32 y)
{
    SDL_snprintf(tbuf, sizeof(tbuf), fmt, value, letter);
    if (Game.language == 'c') {
        AddSpacesIntoText(tbuf, 1);
    }
    Text3D(tbuf, x, y, 1.0f, 0.6f, 0.6f, 0.6f, 0, 0);
}

/* Retail's, with the longer name (port/names.h) and a fourth line: "HH:MM DD.MM.YY" (NTSC: "HH:MM MM.DD.YY"). */
void DrawGameSlot(struct game_s *game, f32 x, f32 y, s32 colour)
{
    if (game->empty == 0) {
        char *name = nw_names_of(game); /* the longer name when it stands, narrower to fit the slot */
        f32 size = nw_names_scale(0.6f, name, NW_NAMES_OWN_GAME);
        Text3D(name, x, y + 0.15f, 1.0f, size, size, size, 0, colour);
        CalculateGamePercentage(game);
        nw_savemenu_stat("%i%%", game->percent, 0, x - 0.15f, y);
        nw_savemenu_stat("%i%c", game->crystals, CLetter[0x75], x + 0.15f, y);
        nw_savemenu_stat("%i%c", game->gems, CLetter[0x77], x - 0.125f, y - 0.15f);
        nw_savemenu_stat("%i%c", game->relics, ObjTab[3].font3d_letter, x + 0.125f, y - 0.15f);
        NwSaveTime t;
        nw_savemenu_when(game, &t);
        if (t.year) {
            bool ntsc = nw_savemenu_ntsc();
            SDL_snprintf(tbuf, sizeof(tbuf), "%02d:%02d %02d.%02d.%02d", t.hour, t.minute, ntsc ? t.month : t.day,
                         ntsc ? t.day : t.month, t.year % 100);
            Text3D(tbuf, x, y - 0.3f, 1.0f, 0.5f, 0.5f, 0.5f, 0, 0);
        }
    } else {
        Text3D(tEMPTY[Game.language], x, y, 1.0f, 0.6f, 0.6f, 0.6f, 0, colour);
    }
    DrawPanel3DObject(0x81, x, y, 1.01f, NW_SAVEMENU_SX, 0.125f, NW_SAVEMENU_SZ, 0xC000, 0, 0, ObjTab[129].obj.scene,
                      ObjTab[129].obj.special, 0);
}

/* Retail's: the page's 4 slots, the cursor's blinking. */
void DrawGameSlots(void)
{
    f32 y = NW_SAVEMENU_DY * 0.5f;
    for (int row = 0; row < 2; row++, y -= NW_SAVEMENU_DY) {
        f32 x = -NW_SAVEMENU_DX * 0.5f;
        for (int column = 0; column < 2; column++, x += NW_SAVEMENU_DX) {
            s32 colour = 0;
            if (Cursor.y == row && Cursor.x == column) {
                colour = (GlobalTimer.frame % 10 < 5) ? 4 : 3;
            }
            DrawGameSlot(&SaveSlot[row][column], x, y, colour);
        }
    }
}

static bool nw_savemenu_grid(int menu)
{
    return menu == 20 || menu == 23 || menu == 26;
}

bool nw_savemenu_open(void)
{
    return (Cursor.menu >= 19 && Cursor.menu <= 28) || Cursor.menu == NW_MENU_SAVE_PREVIOUS;
}

bool nw_savemenu_grid_open(void)
{
    return nw_savemenu_grid(Cursor.menu);
}

/* A save grid opened (from the load/save menu, or the title's LOAD GAME): the page of the save written last, the
   cursor on it; back from a confirm, as retail left it. */
static void nw_savemenu_enter(struct cursor_s *cursor)
{
    int menu = cursor->menu, from = nw_savemenu.menu;
    nw_savemenu.menu = menu;
    if (menu == from || !nw_savemenu_grid(menu)) {
        return;
    }
    if (from == 21 || from == 24 || from == 27) {
        nw_saves_show(nw_saves_page());
        return;
    }
    nw_saves_autos(menu != 23 && nw_saves_has_autos());
    int latest = nw_saves_latest();
    if (latest >= 0 && latest / NW_SAVES_PAGE != nw_saves_page()) {
        nw_saves_show(latest / NW_SAVES_PAGE);
    }
    cursor->x = latest >= 0 ? (signed char)(latest % 2) : 0;
    cursor->y = latest >= 0 ? (signed char)(latest % NW_SAVES_PAGE / 2) : 0;
    cursor->remember[menu].x = cursor->x;
    cursor->remember[menu].y = cursor->y;
}

void nw_savemenu_proc(struct cursor_s *cursor, struct nupad_s *pad, int old_x)
{
    int menu = cursor->menu;
    bool entered = menu != nw_savemenu.menu;
    nw_savemenu_enter(cursor);
    if (entered || !nw_savemenu_grid(menu) || !pad || cursor->wait ||
        cursor->button_lock || fadeval > 0 || (Paused > 0 && Paused < 25)) {
        return;
    }
    /* Shoulder buttons switch banks in LOAD/DELETE only; SAVE always stays on manual destinations. */
    if (menu != 23 && (pad->paddata_db & 0xCu) && (nw_saves_automatic() || nw_saves_has_autos())) {
        nw_saves_autos(!nw_saves_automatic());
        cursor->x = cursor->y = 0;
        cursor->remember[menu].x = cursor->remember[menu].y = 0;
        return;
    }
    if (cursor->x == old_x || cursor->y >= cursor->y_max) return;
    /* retail's buttons this frame (ProcMenu): 0x2000 right, 0x8000 left; a step past the edge wraps the column */
    s32 bits = (s32)pad->paddata_db;
    if ((bits & 0xF000) == 0) {
        bits |= stick_bits_db;
    }
    int page = nw_saves_page();
    if ((bits & 0x2000) && old_x == 1 && cursor->x == 0) {
        if (page + 1 < nw_saves_pages()) {
            nw_saves_show(page + 1);
        } else {
            if (menu != 23 && !nw_saves_automatic() && nw_saves_has_autos()) {
                nw_saves_autos(true);
            } else {
                cursor->x = 1;
            }
        }
    } else if ((bits & 0x8000) && old_x == 0 && cursor->x == 1) {
        if (page > 0) {
            nw_saves_show(page - 1);
        } else {
            if (menu != 23 && nw_saves_automatic()) {
                nw_saves_autos(false);
                nw_saves_show(nw_saves_pages() - 1);
            } else {
                cursor->x = 0;
            }
        }
    }
    cursor->remember[menu].x = cursor->x;
}

void nw_savemenu_draw(struct cursor_s *cursor)
{
    int menu = cursor->menu;
    if (menu < 19 || menu > 28 || (u32)Game.language >= 6) {
        return;
    }
    if (menu != 22 && menu != 25 && menu != 28) { /* the result screens have none, as the Xbox's */
        Text3D(nw_savemenu.deck ? "STEAM DECK" : "PC", 0.0f, 0.63f, 1.0f, 0.45f, 0.45f, 0.45f, 0, 0);
    }
    if (nw_savemenu_grid(menu)) {
        static char *kind[2][6] = {
            { "MANUAL SAVES", "SAUVEGARDES MANUELLES", "MANUELLE SPEICHERSTAENDE", "SALVATAGGI MANUALI", "GUARDADOS MANUALES", "HANDMATIGE SAVES" },
            { "AUTOSAVES", "SAUVEGARDES AUTO", "AUTOMATISCHE SPEICHERSTAENDE", "AUTOSALVATAGGI", "GUARDADOS AUTOMATICOS", "AUTOMATISCHE SAVES" },
        };
        Text3D(kind[nw_saves_automatic()][Game.language], 0, 0.54f, 1, 0.32f, 0.32f, 0.32f, 0, 0);
        if (menu != 23 && (nw_saves_automatic() || nw_saves_has_autos())) {
            Text3D("q / r", 0, -0.83f, 1, 0.32f, 0.32f, 0.32f, 0, 0);
        }
    }
    if (nw_savemenu_grid(menu) && (menu == 23 || nw_saves_any())) { /* the slots are drawn: their pages */
        int page = nw_saves_page();
        if (page > 0 || (menu != 23 && nw_saves_automatic())) {
            Text3D("<", -NW_SAVEMENU_ARROW_X, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0);
        }
        if (page + 1 < nw_saves_pages() || (menu != 23 && !nw_saves_automatic() && nw_saves_has_autos())) {
            Text3D(">", NW_SAVEMENU_ARROW_X, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0);
        }
    }
}

void nw_savemenu_prepare(struct cursor_s *cursor)
{
    nw_savemenu_enter(cursor);
}
