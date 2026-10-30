/* game/game's DrawMenu (port/units.toml wrap): the build ID on the title and pause menus. */
#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "game/game.retail.h"
#include "nu_types.h"
#include "platform/buildid.h"

extern s32 editor_active, GameMode;

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);

void DrawMenu(struct cursor_s *cursor, s32 paused)
{
    static char id[64]; /* upper case: Text3D draws lower case as models and pad buttons */

    DrawMenu__retail(cursor, paused);
    if (editor_active || cursor->wait || GameMode == 1) {
        return;
    }
    /* 0: title, 3 and 4: pause */
    if (cursor->menu == 0 || ((cursor->menu == 3 || cursor->menu == 4) && paused >= 25)) {
        if (!id[0]) {
            SDL_snprintf(id, sizeof(id), "NUWORLD %s", nw_build_id);
            for (char *c = id; *c; c++) {
                *c = (char)SDL_toupper((unsigned char)*c);
            }
        }
        Text3D(id, -0.95f, 0.9f, 1.0f, 0.3f, 0.3f, 0.3f, 2, 2);
    }
}
