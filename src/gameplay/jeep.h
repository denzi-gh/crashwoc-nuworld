/* The jeep: steering, grip, speed. Both jeep levels drive the off-roader (0x99); MovePlayerJeep is unused. */
#ifndef NUWORLD_GAMEPLAY_JEEP_H
#define NUWORLD_GAMEPLAY_JEEP_H

#include <stdbool.h>
#include <stdint.h>

void nw_jeep_apply(void);
/* SeekRot, before retail: true for the off-roader's steering, *out the heading. */
bool nw_jeep_steer(uint16_t a, uint16_t b, int32_t shift, uint16_t *out);

#endif
