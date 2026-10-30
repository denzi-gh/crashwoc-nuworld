#include "gameplay/gameplay.h"

#include <stdio.h>

#include "gameplay/monkeybars.h"
#include "gameplay/title.h"
#include "port/game.h"

NwProfile nw_profile;

void nw_profile_apply(const NwProfile *p)
{
    nw_profile = *p;
    nw_monkey_bars_apply();
}

void nw_deviation_off(const char *sw)
{
    char message[128];
    snprintf(message, sizeof message, "gameplay: %s ran with its switch off", sw);
    nw_game_fatal(message);
}

void nw_gameplay_swap(void)
{
    nw_title_poll();
}
