/* All off (zero) is the retail game every check, test and recording runs. */
#ifndef NUWORLD_GAMEPLAY_GAMEPLAY_H
#define NUWORLD_GAMEPLAY_GAMEPLAY_H

#include <stdbool.h>

#define NW_CRASH_MOVEMENT(SW, FLAG, VAL) \
    SW(jump_fall, "Crash 4 jump arc", true, "Crash 4 jump: quick launch, soft apex, fall speeds up") \
    VAL(jump_float, "Apex float %", 65, 0, 200, "hang time at the apex") \
    VAL(jump_weight, "Fall weight %", 100, 100, 200, "how fast the fall speeds up") \
    VAL(jump_height, "Jump height %", 110, 100, 130, "jump height, % of PS2") \
    SW(jump_distance, "Predictable jump distance", false, "any stick tilt in the air is full speed") \
    VAL(air_accel, "Air acceleration %", 200, 100, 400, "% of PS2") \
    SW(air_momentum, "Air momentum", false, "keeps some speed in the air with the stick released") \
    VAL(air_drift, "Momentum kept %", 75, 0, 100, "share of the PS2 air slowdown skipped") \
    SW(dash_jump, "Crash Dash jump momentum", false, "jump out of a Crash Dash keeps its speed") \
    VAL(dash_kept, "Dash speed kept %", 100, 0, 100, "share of dash speed kept until landing") \
    SW(run_speed, "Faster run", false, "faster run and Crash Dash, quicker acceleration") \
    VAL(run_top, "Top speed %", 120, 100, 150, "% of PS2") \
    VAL(run_accel, "Acceleration %", 150, 100, 300, "% of PS2") \
    SW(grip, "Grip", true, "less sliding on stops and turns, except on ice") \
    VAL(grip_turn, "Turn grip %", 175, 100, 500, "% of PS2") \
    VAL(grip_brake, "Braking %", 180, 100, 400, "% of PS2") \
    SW(long_spin, "Longer spin", false, "longer spin attack") \
    VAL(spin_ms, "Spin length ms", 400, 320, 600, "PS2: 320") \
    SW(spin_fruit, "Spin collects fruit", true, "a spin collects wumpa fruit instead of knocking it away") \
    SW(slide, "Crash slide", true, "faster slide when out of running") \
    VAL(slide_speed, "Slide speed %", 150, 100, 300, "% of PS2") \
    VAL(slide_ms, "Slide length ms", 350, 300, 600, "PS2: 467") \
    FLAG(slide_keep, "Keep speed after it", false, "slide speed carries on instead of stopping") \
    FLAG(slide_cancel, "Let go to stand up", false, "releasing crouch ends the slide") \
    VAL(slide_min_ms, "Shortest slide ms", 300, 0, 400, "minimum length when crouch is released") \
    VAL(slide_jump, "Slide jump speed kept %", 50, 0, 100, "share of slide speed kept when jumping out of it") \
    VAL(slide_steer, "Slide steering deg/s", 210, 0, 720, "turn speed toward the stick while sliding; PS2: 0") \
    SW(slide_spin, "Crash slide spin", true, "a spin out of a slide keeps its speed and leans forward") \
    VAL(slide_spin_kept, "Spin speed %", 110, 50, 150, "% of the slide's speed, held while the spin lasts") \
    VAL(slide_spin_late_ms, "Late spin ms", 10, 0, 300, "how long after the slide a spin still counts") \
    VAL(slide_spin_steer, "Steering %", 2, 0, 100, "turn speed, % of his normal turning") \
    VAL(slide_spin_jump, "Jump speed kept %", 0, 0, 100, "share of its speed kept when jumping out of it") \
    VAL(slide_spin_lean, "Lean deg", 35, 0, 45, "forward tilt while spinning") \
    VAL(slide_spin_bank, "Bank deg", 10, 0, 45, "sideways tilt into a turn while steering") \
    SW(edge_grip, "Edge grip", true, "no sliding off beveled platform edges") \
    VAL(edge_reach, "Edge reach %", 55, 45, 90, "how far past an edge he can stand, % of his radius; PS2: 45") \
    SW(line_2d, "Hold 2D line", false, "stays on the entry line in side-on sections") \
    VAL(line_pull, "Line pull %", 100, 0, 100, "pull back to the line") \
    SW(hazard_hitboxes, "Fair hazard hitboxes", false, "deadly hitboxes match their models") \
    VAL(hazard_enemy, "Enemy reach %", 80, 50, 100, "% of PS2") \
    VAL(hazard_fire, "Fire and steam reach %", 50, 25, 100, "share of his radius that counts against fire and steam") \
    SW(air_jump, "Double jump at any height", true, "double jump at any fall speed") \
    FLAG(air_jump_ledge, "Off ledges and bars", true, "also after walking off a ledge or dropping off bars") \
    FLAG(air_jump_free, "Before it is earned", false, "also before the power is earned") \
    VAL(air_jump_fall, "Max fall speed", 12, 5, 12, "max fall speed, units/s; PS2: 5, 12: any")

#define NW_CRASH_BOOL(f, ...) bool f;
#define NW_CRASH_INT(f, ...) int f;
typedef struct NwCrashMovement {
    NW_CRASH_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwCrashMovement;

#define NW_COCO_MOVEMENT(SW, FLAG, VAL) \
    SW(jump_fall, "Crash 4 jump arc", false,"Crash 4 jump: quick launch, soft apex, fall speeds up") \
    VAL(jump_float, "Apex float %", 100, 0, 200, "hang time at the apex") \
    VAL(jump_weight, "Fall weight %", 100, 100, 200, "how fast the fall speeds up") \
    VAL(jump_height, "Jump height %", 100, 100, 130, "jump height, % of PS2") \
    SW(jump_distance, "Predictable jump distance", true, "any stick tilt in the air is full speed") \
    VAL(air_accel, "Air acceleration %", 115, 100, 400, "% of PS2") \
    SW(air_momentum, "Air momentum", false, "keeps some speed in the air with the stick released") \
    VAL(air_drift, "Momentum kept %", 100, 0, 100, "share of the PS2 air slowdown skipped") \
    SW(dash_jump, "Dash jump momentum", true, "jump out of a dash keeps its speed") \
    VAL(dash_kept, "Dash speed kept %", 100, 0, 100, "share of dash speed kept until landing") \
    SW(run_speed, "Faster run", true, "faster run and dash, quicker acceleration") \
    VAL(run_top, "Top speed % of Crash's", 110, 100, 150, "% of Crash's PS2 run and dash; Coco's PS2: 83 and 94") \
    VAL(run_accel, "Acceleration %", 150, 100, 300, "% of PS2") \
    SW(grip, "Grip", true, "less sliding on stops and turns, except on ice") \
    VAL(grip_turn, "Turn grip %", 300, 100, 500, "% of PS2") \
    VAL(grip_brake, "Braking %", 200, 100, 400, "% of PS2") \
    SW(spin_fruit, "Spin collects fruit", true, "a spin collects wumpa fruit instead of knocking it away")

typedef struct NwCocoMovement {
    NW_COCO_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwCocoMovement;

#define NW_MECH_MOVEMENT(SW, FLAG, VAL) \
    SW(stand_jump, "Running jump from a standstill", true, "jumps at once, steers like a running jump") \
    SW(shot_stop, "Shorter stop after a shot", true, "steering or jumping ends the post-shot stop early") \
    VAL(shot_stop_ms, "Stop ms", 25, 0, 600, "minimum stop after a shot") \
    FLAG(shot_tap, "Quick tap fires", true, "short press fires; PS2 drops it") \
    SW(aim_walk, "Walk while aiming", true, "left stick walks while the right stick or d-pad aims") \
    VAL(aim_walk_speed, "Walk speed %", 150, 50, 400, "% of walk speed; run is 444") \
    SW(aim_fast, "Faster aiming", true, "aim follows the stick faster") \
    VAL(aim_speed, "Aim speed %", 200, 100, 500, "% of PS2") \
    SW(aim_assist, "Aim assist", false, "aim bends toward the nearest enemy ahead") \
    VAL(assist_cone, "Assist cone deg", 20, 5, 45, "max angle off the aim for the pull") \
    VAL(assist_pull, "Assist pull %", 50, 0, 100, "pull strength") \
    SW(hitbox, "Fitting hitbox", true, "deadly hitboxes match their models") \
    VAL(hitbox_size, "Hitbox %", 70, 50, 100, "% of PS2 radius")

typedef struct NwMechMovement {
    NW_MECH_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwMechMovement;

#define NW_SUB_MOVEMENT(SW, FLAG, VAL) \
    SW(turning, "Free turning", true, "turns toward any side push, also mid-turn; no stop facing the screen") \
    VAL(turn_speed, "Turn speed %", 200, 100, 400, "% of PS2") \
    VAL(pitch_speed, "Climb and dive turn %", 250, 100, 400, "pitch speed, % of PS2") \
    SW(speed, "Faster sub", true, "faster sub, quicker acceleration and braking") \
    VAL(speed_top, "Top speed %", 110, 100, 200, "% of PS2") \
    VAL(speed_accel, "Acceleration %", 250, 100, 400, "% of PS2") \
    VAL(speed_brake, "Braking %", 200, 100, 400, "% of PS2") \
    SW(hitbox, "Fitting hitbox", true, "fish, mine and shot hitboxes match their models") \
    VAL(hitbox_size, "Hitbox %", 90, 50, 100, "% of PS2; walls, crates and fruit unchanged") \
    SW(weapons, "Weapon tuning", true, "torpedo and bomb reload and speed as set below") \
    VAL(torpedo_rate, "Torpedo rate % (2/s)", 135, 100, 400, "fire rate, % of PS2 (2/s)") \
    VAL(torpedo_speed, "Torpedo speed %", 120, 100, 300, "% of PS2; same flight time, so longer range") \
    VAL(bomb_rate, "Bomb rate % (1/s)", 250, 100, 400, "drop rate, % of PS2 (1/s)") \
    VAL(bomb_speed, "Bomb speed %", 100, 100, 300, "sink speed, % of PS2") \
    SW(pass_fruit, "Shots pass fruit", true, "torpedoes and bombs pass through wumpa fruit")

typedef struct NwSubMovement {
    NW_SUB_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwSubMovement;

#define NW_JEEP_MOVEMENT(SW, FLAG, VAL) \
    SW(steering, "Sharper steering", true, "quicker turn-in") \
    VAL(steer_response, "Steering response %", 120, 100, 400, "how fast it turns toward the stick") \
    VAL(steer_angle, "Steering angle %", 115, 50, 150, "max turn off the track, % of PS2") \
    SW(grip, "Grip", true, "less drift at speed in Smokey and the Bandicoot") \
    VAL(grip_speed, "Grip at speed %", 200, 100, 1000, "% of PS2") \
    SW(speed, "Faster jeep", true, "higher top speed, reached sooner") \
    VAL(speed_top, "Top speed %", 100, 100, 150, "% of PS2") \
    VAL(speed_accel, "Acceleration %", 100, 100, 300, "Smokey and the Bandicoot run-up, % of PS2")

typedef struct NwJeepMovement {
    NW_JEEP_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwJeepMovement;

#define NW_ATLAS_MOVEMENT(SW, FLAG, VAL) \
    SW(steer, "Sharper steering", true, "tighter turns that keep speed") \
    VAL(steer_turn, "Turn sharpness %", 250, 100, 400, "sideways push, % of PS2") \
    SW(brake, "Faster braking", true, "stick against the roll stops sooner; acceleration unchanged") \
    VAL(brake_force, "Braking %", 250, 100, 400, "push against the roll, % of PS2") \
    VAL(coast_brake, "Let-go braking %", 0, 0, 100, "slowdown with the stick released, % of PS2 push") \
    SW(edge_guard, "Edge guard", false, "stops at pit edges when rolling slowly") \
    VAL(guard_reach, "Look-ahead ms", 300, 100, 600, "pit look-ahead, as time at current speed") \
    VAL(guard_speed, "Up to speed", 6, 2, 13, "max speed it still stops from, units/s; top 11, boost 13") \
    VAL(guard_brake, "Edge braking %", 300, 100, 500, "% of PS2 push") \
    FLAG(guard_push, "Push to roll in", true, "pushing toward the pit rolls in, as on PS2")

typedef struct NwAtlasMovement {
    NW_ATLAS_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwAtlasMovement;

#define NW_FIREFLY_MOVEMENT(SW, FLAG, VAL) \
    SW(lock_on, "Clear lock-on", true, "marker shows the X target, lock progress, lock, loss and reload") \
    VAL(lock_ms, "Lock time ms", 1000, 300, 1300, "X hold time to lock; PS2: 1300") \
    FLAG(lock_tap, "Quick tap fires", true, "releasing X early fires once locked; PS2 drops it") \
    SW(steering, "Sharper steering", true, "quicker bank, level out, climb and dive") \
    VAL(steer_response, "Steering response %", 200, 100, 400, "bank in and out speed") \
    VAL(steer_turn, "Turn rate %", 125, 100, 200, "turn rate while banked, % of PS2") \
    VAL(pitch_response, "Climb and dive response %", 250, 100, 400, "pitch speed")

typedef struct NwFireflyMovement {
    NW_FIREFLY_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwFireflyMovement;

#define NW_PLANE_MOVEMENT(SW, FLAG, VAL) \
    SW(steering, "Sharper steering", true, "quicker pitch and bank") \
    VAL(deadzone, "Stick deadzone %", 5, 5, 30, "stick deadzone; PS2: 25") \
    VAL(pitch_response, "Pitch response %", 200, 100, 500, "pitch speed, % of PS2") \
    VAL(bank_response, "Bank response %", 230, 100, 400, "bank speed, % of PS2") \
    FLAG(push_dive, "Push to dive", true, "stick forward dives, as on PS2; off: forward climbs") \
    SW(aim, "Shots on the reticle", true, "shots fly to the reticle in Tornado Alley") \
    VAL(aim_assist, "Assist cone deg", 3, 0, 10, "shots pull to enemies this close to the reticle; 0: off")

typedef struct NwPlaneMovement {
    NW_PLANE_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwPlaneMovement;

#define NW_SPACE_MOVEMENT(SW, FLAG, VAL) \
    SW(steering, "Sharper steering", true, "nose follows the stick sooner, lighter push steers") \
    VAL(pitch_speed, "Climb and dive response %", 250, 100, 500, "nose speed, % of PS2") \
    VAL(bank_speed, "Bank response %", 150, 100, 400, "bank speed, % of PS2") \
    VAL(turn_rate, "Turn rate %", 125, 100, 200, "turn rate at full bank, % of PS2 (120 deg/s)") \
    VAL(deadzone, "Stick deadzone %", 10, 0, 25, "stick deadzone; PS2: 25") \
    SW(roll, "Barrel roll keeps speed", true, "barrel roll keeps speed") \
    VAL(roll_kept, "Speed kept %", 100, 0, 100, "speed kept in a roll; PS2 slows to 1/8") \
    SW(aim, "True aim", true, "reticle on the shot path") \
    VAL(aim_range, "Reticle distance", 60, 20, 88, "reticle distance on the path; shots reach 88") \
    FLAG(aim_sweep, "Shots cannot skip targets", true, "shots hit anything passed between frames") \
    SW(aim_assist, "Aim assist", true, "shots bend toward the nearest target ahead") \
    VAL(assist_cone, "Assist cone deg", 6, 2, 20, "max angle off the shots for the pull") \
    VAL(assist_pull, "Assist pull %", 75, 0, 100, "bend strength")

typedef struct NwSpaceMovement {
    NW_SPACE_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwSpaceMovement;

#define NW_TT_MOVEMENT(SW, FLAG, VAL) \
    SW(no_masks, "Start without masks", true, "starting a time trial takes away the Aku Aku masks carried in") \
    SW(no_fruit_clock, "Bazooka cannot start the clock", true, "flying fruit no longer starts a time trial")

typedef struct NwTimeTrialMovement {
    NW_TT_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwTimeTrialMovement;

typedef struct NwMovement {
    int monkey_bar_speed;
    NwCrashMovement crash;
    NwCocoMovement coco;
    NwMechMovement mech;
    NwSubMovement submarine;
    NwJeepMovement jeep;
    NwAtlasMovement atlasphere;
    NwFireflyMovement firefly;
    NwPlaneMovement plane;
    NwSpaceMovement spacefighter;
    NwTimeTrialMovement time_trial;
} NwMovement;

#define NW_CRASH_NONE(...)
#define NW_CRASH_DEFAULT(f, label, def, ...) .f = def,
#define NW_MOVEMENT_RETAIL \
    { .monkey_bar_speed = 100, .crash = { NW_CRASH_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .coco = { NW_COCO_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .mech = { NW_MECH_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .submarine = { NW_SUB_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .jeep = { NW_JEEP_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .atlasphere = { NW_ATLAS_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .firefly = { NW_FIREFLY_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .plane = { NW_PLANE_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .spacefighter = { NW_SPACE_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .time_trial = { NW_TT_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) } }
#define NW_MOVEMENT_NHANCED \
    { .monkey_bar_speed = 335, .crash = { NW_CRASH_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .coco = { NW_COCO_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .mech = { NW_MECH_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .submarine = { NW_SUB_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .jeep = { NW_JEEP_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .atlasphere = { NW_ATLAS_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .firefly = { NW_FIREFLY_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .plane = { NW_PLANE_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .spacefighter = { NW_SPACE_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .time_trial = { NW_TT_MOVEMENT(NW_CRASH_DEFAULT, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) } }

typedef enum NwMovementProfile {
    NW_MOVEMENT_PROFILE_RETAIL,
    NW_MOVEMENT_PROFILE_NHANCED,
    NW_MOVEMENT_PROFILE_CUSTOM,
} NwMovementProfile;

NwMovementProfile nw_movement_profile(const NwMovement *m);

typedef struct NwProfile {
    bool title_menu;
    NwMovement movement;
    bool air_indicator;
    bool boss_health;
    bool wombat;        /* a new game named WOMBAT starts with everything open, as on the GameCube and Xbox */
    bool hub_minigame;
    bool long_time_names; /* up to 8 letters, not 3 (port/names.h) */
    bool relic_spin;
    int camera_stability; /* % of a jump the camera ignores; 0 = retail */
    bool hub_gem;
    bool first_boss_cut;
    bool vehicle_hints;
    bool subtitles, title_subtitles;
    bool bug_fixes;
    bool hitboxes;      /* not saved */
    bool boss_pics;     /* not saved */
} NwProfile;

extern NwProfile nw_profile;

/* The windowed boot, before the game thread starts. */
void nw_profile_apply(const NwProfile *p);
/* Applied by the game thread at its next swap. */
void nw_profile_request(const NwProfile *p);

/* Reached with its switch off (every retail run), the game stops. */
#define NW_DEVIATION(sw) (nw_profile.sw ? (void)0 : nw_deviation_off(#sw))
_Noreturn void nw_deviation_off(const char *sw);

/* The game thread at each swap, before the port's level poll (src/port/ps2video.c). */
void nw_gameplay_swap(void);

#endif
