#include "port/options.h"

#include "game/gametypes.h"

extern struct game_s Game;

int nw_options_volume(bool music)
{
    return music ? Game.music_volume : Game.sfx_volume;
}

void nw_options_set_volume(bool music, int volume)
{
    u8 v = (u8)(volume < 0 ? 0 : volume > 100 ? 100 : volume);
    if (music) {
        Game.music_volume = v;
    } else {
        Game.sfx_volume = v;
    }
}
