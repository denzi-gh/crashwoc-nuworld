/* Game-thread shadow casters: borrow this frame's mesh records and already evaluated joint palettes. */
#ifndef NUWORLD_PORT_SHADOWS_H
#define NUWORLD_PORT_SHADOWS_H

#include <stdbool.h>

struct NwCmd;
struct NUHGOBJ_s;
struct numtx_s;

void nw_shadow_reset(void);
void nw_shadow_release(void);
/* A character's first model is the caster; its reflected model must not replace it. */
void nw_shadow_character_begin(void);
void nw_shadow_character_end(void);
bool nw_shadow_model_begin(const struct NUHGOBJ_s *obj, const struct numtx_s *world);
void nw_shadow_model_end(bool captured);
void nw_shadow_record(struct NwCmd *cmd);

#endif
