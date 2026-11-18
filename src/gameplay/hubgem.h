#ifndef NUWORLD_GAMEPLAY_HUBGEM_H
#define NUWORLD_GAMEPLAY_HUBGEM_H

#include <stdbool.h>
#include <stddef.h>

struct cammtx_s;
struct obj_s;

typedef struct NwHubGemData {
    char file[128];
    char object[64];
    int buffer_kb;
    int level, flag, seen_bit;
    int place_level;
    float pos[3];
    int yaw;
    float scale, radius, height;
    float settle, floor_range, clear;
    float cam_pos[3], cam_look[3];
    float blend_in, appear, hold, blend_out;
    int debris, sfx, volume;
    float effect_height;
    bool hide_hud;
    char text[64];
    float text_range, text_y, text_drop, text_slide, text_scale;
    int text_colour;
    float wall_open[4];
    float wall_add[32];
    int wall_points;
} NwHubGemData;

bool nw_hubgem_parse(NwHubGemData *d, const char *text, size_t size);
const NwHubGemData *nw_hubgem_data(void);
bool nw_hubgem_owned(const NwHubGemData *d);
bool nw_hubgem_seen(const NwHubGemData *d);
bool nw_hubgem_due(const NwHubGemData *d);
bool nw_hubgem_idle(const NwHubGemData *d);
void nw_hubgem_aim(const float pos[3], const float look[3], int *xrot, int *yrot);

void nw_hubgem_level(void);
bool nw_hubgem_tick(void);
bool nw_hubgem_camera(struct cammtx_s *cam);
void nw_hubgem_collide(struct obj_s *obj);
void nw_hubgem_walls(void);
void nw_hubgem_draw(void);
void nw_hubgem_message(void);
void nw_hubgem_close(void);

#endif
