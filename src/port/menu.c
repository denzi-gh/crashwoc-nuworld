/* game/game's DrawMenu (port/units.toml wrap): the build ID on the title and pause menus, and the title menu's
   MODS (SOON) teaser. */
#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "game/game.retail.h"
#include "nu_types.h"
#include "platform/buildid.h"
#include "port/font3d.h"

extern s32 editor_active, GameMode, pause_dir, LANGUAGEOPTION;
extern struct game_s Game;
extern char tbuf[128];
extern char *tNEWGAME[6], *tLOADGAME[6], *tLANGUAGE[6], *LanguageName[6];

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
void DrawMenuEntry(struct cursor_s *cursor, char *txt, f32 *x, f32 *y, s32 *i);

#define NW_MENUDY -0.120000005f /* game/game's static MENUDY */

static char *nw_menu_mods[6] = {
    "MODS (SOON)", "MODS (BIENTOT)", "MODS (BALD)", "MOD (PRESTO)", "MODS (PRONTO)", "MODS (BINNENKORT)",
};

/* Retail's menu 0 with a grey MODS (SOON) line under LOAD GAME, one row more to centre */
static void nw_menu_title(struct cursor_s *cursor, s32 paused)
{
    f32 x = 0.0f;
    if (pause_dir != 0) {
        f32 size = (25 - paused) * 0.08f;
        x += pause_dir == 1 ? -size : size;
    }
    f32 y = -0.75f - (cursor->y_max - cursor->y_min + 1) * NW_MENUDY * 0.5f;
    s32 i = 0;
    DrawMenuEntry(cursor, tNEWGAME[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, tLOADGAME[Game.language], &x, &y, &i);
    Text3D(nw_menu_mods[Game.language], x, y, 1.0f, 0.6f, 0.6f, 0.6f, 0, NW_FONT3D_GREY);
    y += NW_MENUDY;
    if (LANGUAGEOPTION) {
        SDL_snprintf(tbuf, sizeof(tbuf), "%s: %s", tLANGUAGE[Game.language], LanguageName[Game.language]);
        DrawMenuEntry(cursor, tbuf, &x, &y, &i);
    }
}

void DrawMenu(struct cursor_s *cursor, s32 paused)
{
    static char id[64]; /* upper case: Text3D draws lower case as models and pad buttons */

    if (editor_active || cursor->wait || GameMode == 1) {
        DrawMenu__retail(cursor, paused);
        return;
    }
    if (cursor->menu == 0 && (u32)Game.language < 6) {
        nw_menu_title(cursor, paused);
    } else {
        DrawMenu__retail(cursor, paused);
    }
    /* 0: title, 3 and 4: pause */
    if (cursor->menu == 0 || ((cursor->menu == 3 || cursor->menu == 4) && paused >= 25)) {
        if (!id[0]) {
            SDL_snprintf(id, sizeof(id), "NUWORLD %s", nw_build_name);
            for (char *c = id; *c; c++) {
                *c = (char)SDL_toupper((unsigned char)*c);
            }
        }
        Text3D(id, -0.95f, 0.9f, 1.0f, 0.3f, 0.3f, 0.3f, 2, 2);
    }
}
