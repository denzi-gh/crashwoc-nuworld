/* Crunch stands on the warp room's top floor (NwProfile.hub_minigame); X next to him starts the loading minigame. */
#ifndef NUWORLD_GAMEPLAY_HUBCRUNCH_H
#define NUWORLD_GAMEPLAY_HUBCRUNCH_H

struct creature_s;
struct nupad_s;

/* MovePlayer, before retail: loads and animates him, and takes the X that starts the minigame. */
struct nupad_s *nw_crunch_pad(struct creature_s *c, struct nupad_s *pad);
/* MovePlayer, after retail: keeps Crash out of him. */
void nw_crunch_push(struct creature_s *c);
/* DrawCreatures' main pass for Character[0]. */
void nw_crunch_draw(void);
/* DrawPanel's end: the prompt. */
void nw_crunch_prompt(void);
/* CloseCreatures: the level's textures and materials go next. */
void nw_crunch_close(void);

#endif
