/* src/port/fptrap.c: with NUWORLD_FP_TRAPS set a NaN on the game thread stops the program with where it was made
   (ctest fp-traps expects that line); without it the game thread goes on (ctest fp-traps-off). */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "port/game.h"

static volatile float zero;

static int nan_main(int argc, char **argv)
{
    (void)argc, (void)argv;
    volatile float nan = zero / zero;
    (void)nan;
    return 0;
}

int main(void)
{
    if (!nw_game_init(NW_CLOCK_REALTIME, 50) || !nw_game_start(nan_main, 0, NULL)) {
        fprintf(stderr, "no game thread: %s\n", SDL_GetError());
        return 1;
    }
    while (nw_game_state() == NW_GAME_RUNNING) {
        SDL_Delay(10);
    }
    printf("fptrap: the game thread returned\n");
    return nw_game_state() == NW_GAME_RETURNED ? 0 : 1;
}
