/* --cut: a cut movie (the intro, the outros) started at one of its shots instead of its first, for testing a shot
   without sitting through the ones before it. */
#ifndef NUWORLD_PORT_CUTSTART_H
#define NUWORLD_PORT_CUTSTART_H

/* The shot the next cut movie that has it starts at: a .cut file's name with or without ".cut" ("sunny", "storm.cut"),
   matched without case. Once only: the movie after it starts at its first shot again. */
void nw_cut_start(const char *name);
/* game/cut's StartCutMovie, at that shot if the movie has it. Called by gameplay/cut.c's wrap. */
void nw_cut_start_movie(void);

#endif
