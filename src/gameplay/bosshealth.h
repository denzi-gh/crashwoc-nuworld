/* [game] boss_health: lines on the boss bar between the boss's phases. */
#ifndef NUWORLD_GAMEPLAY_BOSSHEALTH_H
#define NUWORLD_GAMEPLAY_BOSSHEALTH_H

/* The lines that split a bar of maxhealth into its phases; 0 for a bar without phases (Air Crunch's 100). */
int nw_boss_health_lines(int maxhealth);
/* BossBar, after retail drew it. */
void nw_boss_health_bar(float x, float y, float z, float xs, float ys, int health, int maxhealth);
/* NuMtlRelease (a level load) freed the lines' material. */
void nw_boss_health_release(void);

#endif
