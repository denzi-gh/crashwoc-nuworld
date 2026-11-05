/* Game > Start Minigame: the loading screen's wumpa minigame, cut from every retail build (MiniGame* in game/main are
   empty), rebuilt on retail's leftover mg_* tuning. */
#ifndef NUWORLD_GAMEPLAY_MINIGAME_H
#define NUWORLD_GAMEPLAY_MINIGAME_H

/* UI thread: reloads the current level, or another Crash or Coco level with fruit, with the minigame on its loading
   screen. */
void nw_minigame_start(void);
/* The current save's best score (save/savecodec.h NW_SAVE_MINIGAME_BEST); the game saves it with the rest. */
int nw_minigame_best(void);

#endif
