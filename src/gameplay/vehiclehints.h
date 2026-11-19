#ifndef NUWORLD_GAMEPLAY_VEHICLEHINTS_H
#define NUWORLD_GAMEPLAY_VEHICLEHINTS_H

#define NW_VEHICLE_HINT_LINES 3
#define NW_VEHICLE_HINT_TEXT 48
#define NW_VEHICLE_HINT_SWIM -2

int nw_vehicle_hints_lines(int ride, char lines[NW_VEHICLE_HINT_LINES][NW_VEHICLE_HINT_TEXT]);
void nw_vehicle_hints_draw(void);
void nw_vehicle_hints_reset(void);

#endif
