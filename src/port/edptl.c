/* Wrap of gamelib/edptl: a particle file's sound numbers in PAL's numbering. */
#include <SDL3/SDL.h>

#include "gamelib/debris.h"
#include "gamelib/edptl.retail.h"
#include "port/disc.h"

void FileLoadSingleEffectType(struct debinfo_s *dt, int version, signed char list)
{
    FileLoadSingleEffectType__retail(dt, version, list);
    for (size_t i = 0; i < SDL_arraysize(dt->sounds); i++) {
        dt->sounds[i].id = nw_disc_sfx(dt->sounds[i].id);
    }
}
