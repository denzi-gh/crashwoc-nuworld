/* [video] gems: the materials of the gems, crystals and relics, and what each of their layers is. */
#ifndef NUWORLD_GFX_GEM_H
#define NUWORLD_GFX_GEM_H

#include <stdint.h>

typedef enum NwGemKind {
    NW_GEM_NONE,
    NW_GEM_CRYSTAL,
    NW_GEM_CLEAR,
    NW_GEM_COLOUR,
    NW_GEM_RELIC,
} NwGemKind;

/* body: the stone, drawn by the gem programs; metal: a relic's body, the same; hide: a layer the body replaces;
   keep: drawn as the PS2. */
typedef enum NwGemRole {
    NW_GEM_KEEP,
    NW_GEM_BODY,
    NW_GEM_METAL,
    NW_GEM_HIDE,
} NwGemRole;

typedef struct NwGemInfo {
    uint8_t kind, role;
    float tint[3]; /* the material's diffuse */
} NwGemInfo;

/* Game thread. A level's materials go with it: reset before its own are added. */
void nw_gem_reset(void);
void nw_gem_add(const void *mtl, NwGemKind kind, NwGemRole role, const float tint[3]);
NwGemInfo nw_gem_find(const void *mtl);

#endif
