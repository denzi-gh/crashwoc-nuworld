#include "gameplay/gameplay.h"

#include <stdio.h>

#include "gameplay/title.h"
#include "port/game.h"

NwProfile nw_profile;
static bool nw_profile_applied;

void nw_profile_apply(const NwProfile *p)
{
    nw_profile = *p;
    nw_profile_applied = true;
}

void nw_deviation(const char *id)
{
    if (!nw_profile_applied) {
        char message[128];
        snprintf(message, sizeof message, "gameplay: %s ran in a retail run", id);
        nw_game_fatal(message);
    }
}

void nw_gameplay_swap(void)
{
    nw_title_poll();
}
