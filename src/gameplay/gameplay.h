/* NuWorld's changes to how the game plays or looks, each behind a [game] setting; all off (zero) is the retail game that
   every check, test and recording runs, and only the windowed boot fills the profile from settings.toml. */
#ifndef NUWORLD_GAMEPLAY_GAMEPLAY_H
#define NUWORLD_GAMEPLAY_GAMEPLAY_H

#include <stdbool.h>

/* Crash's options ([game.movement.crash]) in panel order: SW(field, label, help) a switch, off is retail;
   FLAG(field, label, default, help) and VAL(field, label, default, lo, hi, help) tune the switch above them. */
#define NW_CRASH_MOVEMENT(SW, FLAG, VAL) \
    SW(jump_fall, "Crash 4 jump arc", "a Crash 4 style jump: quick launch, soft top, a fall that builds up") \
    VAL(jump_float, "Apex float %", 100, 0, 200, "how long the jump floats at the top") \
    VAL(jump_weight, "Fall weight %", 100, 100, 200, "how hard the fall builds up") \
    VAL(jump_height, "Jump height %", 100, 100, 130, "percent of the PS2's jump height") \
    SW(jump_distance, "Predictable jump distance", "in a jump any stick tilt is a full run") \
    VAL(air_accel, "Air acceleration %", 200, 100, 400, "percent of the PS2's") \
    SW(air_momentum, "Air momentum", "mid-air with the stick let go he keeps some momentum") \
    VAL(air_drift, "Momentum kept %", 75, 0, 100, "how much of the PS2's slow-down he skips") \
    SW(dash_jump, "Crash Dash jump momentum", "a jump out of a Crash Dash keeps its speed") \
    VAL(dash_kept, "Dash speed kept %", 100, 0, 100, "share of the dash's extra speed kept to the ground") \
    SW(run_speed, "Faster run", "a faster run and Crash Dash, quicker to speed") \
    VAL(run_top, "Top speed %", 120, 100, 150, "percent of the PS2's") \
    VAL(run_accel, "Acceleration %", 150, 100, 300, "percent of the PS2's") \
    SW(grip, "Grip", "less slippery stops and turns; ice stays slippery") \
    VAL(grip_turn, "Turn grip %", 300, 100, 500, "percent of the PS2's") \
    VAL(grip_brake, "Braking %", 200, 100, 400, "percent of the PS2's") \
    SW(long_spin, "Longer spin", "the spin attack lasts longer") \
    VAL(spin_ms, "Spin length ms", 400, 320, 600, "the PS2's is 320") \
    SW(slide, "Crash 2 slide", "a faster slide he comes out of running") \
    VAL(slide_speed, "Slide speed %", 115, 100, 150, "percent of the PS2's") \
    VAL(slide_ms, "Slide length ms", 400, 300, 600, "the PS2's is 467") \
    FLAG(slide_keep, "Keep speed after it", true, "the slide's speed carries on instead of stopping dead") \
    FLAG(slide_cancel, "Let go to stand up", true, "letting go of crouch ends the slide") \
    VAL(slide_min_ms, "Shortest slide ms", 150, 0, 400, "how long it lasts at least when let go") \
    SW(edge_grip, "Edge grip", "a beveled platform edge holds him instead of sliding him off") \
    VAL(edge_reach, "Edge reach %", 75, 45, 90, "how far past an edge he stands, percent of his radius; the PS2's 45") \
    SW(line_2d, "Hold 2D line", "in side-on sections he stays on the line he came in on") \
    VAL(line_pull, "Line pull %", 100, 0, 100, "how hard he is pulled back to the line") \
    SW(hazard_hitboxes, "Fair hazard hitboxes", "what can kill him reaches about as far as it is drawn") \
    VAL(hazard_enemy, "Enemy reach %", 80, 50, 100, "percent of the PS2's") \
    VAL(hazard_fire, "Fire and steam reach %", 50, 25, 100, "share of his radius that counts against them") \
    SW(air_jump, "Double jump at any height", "the double jump at any fall speed") \
    FLAG(air_jump_ledge, "Off ledges and bars", true, "also after walking off a ledge or dropping off the bars") \
    FLAG(air_jump_free, "Before it is earned", false, "also before the power is earned") \
    VAL(air_jump_fall, "Max fall speed", 12, 5, 12, "units a second it still works at; 5 is the PS2's, 12 any")

#define NW_CRASH_BOOL(f, ...) bool f;
#define NW_CRASH_INT(f, ...) int f;
typedef struct NwCrashMovement {
    NW_CRASH_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwCrashMovement;

/* Coco's options ([game.movement.coco]), the same scheme, under Crash's names. */
#define NW_COCO_MOVEMENT(SW, FLAG, VAL) \
    SW(jump_fall, "Crash 4 jump arc", "a Crash 4 style jump: quick launch, soft top, a fall that builds up") \
    VAL(jump_float, "Apex float %", 100, 0, 200, "how long the jump floats at the top") \
    VAL(jump_weight, "Fall weight %", 100, 100, 200, "how hard the fall builds up") \
    VAL(jump_height, "Jump height %", 100, 100, 130, "percent of the PS2's jump height") \
    SW(jump_distance, "Predictable jump distance", "in a jump any stick tilt is a full run") \
    VAL(air_accel, "Air acceleration %", 200, 100, 400, "percent of the PS2's") \
    SW(air_momentum, "Air momentum", "mid-air with the stick let go she keeps some momentum") \
    VAL(air_drift, "Momentum kept %", 75, 0, 100, "how much of the PS2's slow-down she skips") \
    SW(dash_jump, "Dash jump momentum", "a jump out of a dash keeps its speed") \
    VAL(dash_kept, "Dash speed kept %", 100, 0, 100, "share of the dash's extra speed kept to the ground") \
    SW(run_speed, "Faster run", "a faster run and dash, quicker to speed") \
    VAL(run_top, "Top speed % of Crash's", 120, 100, 150, "percent of Crash's PS2 run and dash; hers are 83 and 94") \
    VAL(run_accel, "Acceleration %", 150, 100, 300, "percent of the PS2's") \
    SW(grip, "Grip", "less slippery stops and turns; ice stays slippery") \
    VAL(grip_turn, "Turn grip %", 300, 100, 500, "percent of the PS2's") \
    VAL(grip_brake, "Braking %", 200, 100, 400, "percent of the PS2's")

typedef struct NwCocoMovement {
    NW_COCO_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwCocoMovement;

/* The mech's options ([game.movement.mech]), the same scheme. */
#define NW_MECH_MOVEMENT(SW, FLAG, VAL) \
    SW(stand_jump, "Running jump from a standstill", "it jumps off at once and steers like a running jump") \
    SW(shot_stop, "Shorter stop after a shot", "steering or jumping ends the stop after a shot early") \
    VAL(shot_stop_ms, "Stop ms", 150, 0, 600, "the least it stands after a shot") \
    FLAG(shot_tap, "Quick tap fires", true, "a short press still fires; the PS2's drops it") \
    SW(aim_walk, "Walk while aiming", "while aiming the right stick or the d-pad aims and the left stick walks") \
    VAL(aim_walk_speed, "Walk speed %", 150, 50, 400, "percent of its walk; its run is 444") \
    SW(aim_fast, "Faster aiming", "the aim follows the stick sooner") \
    VAL(aim_speed, "Aim speed %", 250, 100, 500, "percent of the PS2's") \
    SW(aim_assist, "Aim assist", "the aim bends toward the nearest enemy in front") \
    VAL(assist_cone, "Assist cone deg", 20, 5, 45, "how far off the aim an enemy still pulls it") \
    VAL(assist_pull, "Assist pull %", 50, 0, 100, "how hard it pulls") \
    SW(hitbox, "Fitting hitbox", "what can kill it reaches about as far as it is drawn") \
    VAL(hitbox_size, "Hitbox %", 70, 50, 100, "percent of the PS2's radius")

typedef struct NwMechMovement {
    NW_MECH_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwMechMovement;

/* The submarine's options ([game.movement.submarine]), the same scheme. */
#define NW_SUB_MOVEMENT(SW, FLAG, VAL) \
    SW(turning, "Free turning", "any push to the side turns it that way, also mid-turn; no stop facing the screen") \
    VAL(turn_speed, "Turn speed %", 200, 100, 400, "percent of the PS2's") \
    VAL(pitch_speed, "Climb and dive turn %", 200, 100, 400, "how fast it noses up and down, percent of the PS2's") \
    SW(speed, "Faster sub", "a faster sub, quicker to speed and to stop") \
    VAL(speed_top, "Top speed %", 125, 100, 200, "percent of the PS2's") \
    VAL(speed_accel, "Acceleration %", 200, 100, 400, "percent of the PS2's") \
    VAL(speed_brake, "Braking %", 200, 100, 400, "percent of the PS2's") \
    SW(hitbox, "Fitting hitbox", "fish, mines and shots reach about as far as it is drawn") \
    VAL(hitbox_size, "Hitbox %", 75, 50, 100, "percent of the PS2's; walls, crates and fruit keep the PS2's") \
    SW(weapons, "Weapon tuning", "the torpedo's and the bomb's reload and speed as set below") \
    VAL(torpedo_rate, "Torpedo rate % (2/s)", 100, 100, 400, "how often it fires, percent of the PS2's 2 a second") \
    VAL(torpedo_speed, "Torpedo speed %", 100, 100, 300, "percent of the PS2's; it flies as long, so farther") \
    VAL(bomb_rate, "Bomb rate % (1/s)", 100, 100, 400, "how often it drops one, percent of the PS2's 1 a second") \
    VAL(bomb_speed, "Bomb speed %", 100, 100, 300, "how fast it sinks, percent of the PS2's")

typedef struct NwSubMovement {
    NW_SUB_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwSubMovement;

/* The jeep's options ([game.movement.jeep]), the same scheme. */
#define NW_JEEP_MOVEMENT(SW, FLAG, VAL) \
    SW(steering, "Sharper steering", "it turns in quicker, the same at 60 Hz as at 50") \
    VAL(steer_response, "Steering response %", 200, 100, 400, "how fast it turns where the stick points") \
    VAL(steer_angle, "Steering angle %", 100, 50, 150, "how far it turns off the track, percent of the PS2's") \
    SW(grip, "Grip", "less drift at speed in Smokey and the Bandicoot") \
    VAL(grip_speed, "Grip at speed %", 300, 100, 1000, "percent of the PS2's") \
    SW(speed, "Faster jeep", "a higher top speed, reached sooner") \
    VAL(speed_top, "Top speed %", 110, 100, 150, "percent of the PS2's") \
    VAL(speed_accel, "Acceleration %", 150, 100, 300, "Smokey and the Bandicoot's run-up, percent of the PS2's")

typedef struct NwJeepMovement {
    NW_JEEP_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwJeepMovement;

/* The atlasphere's options ([game.movement.atlasphere]), the same scheme. None touches acceleration or coasting. */
#define NW_ATLAS_MOVEMENT(SW, FLAG, VAL) \
    SW(steer, "Sharper steering", "the ball turns tighter toward the stick and keeps its speed through the turn") \
    VAL(steer_turn, "Turn sharpness %", 200, 100, 400, "percent of the PS2's push across the roll") \
    SW(brake, "Faster braking", "the stick against the roll stops the ball sooner; speeding up stays the PS2's") \
    VAL(brake_force, "Braking %", 200, 100, 400, "percent of the PS2's push against the roll") \
    VAL(coast_brake, "Let-go braking %", 0, 0, 100, "with the stick let go it slows too, percent of the PS2's push") \
    SW(edge_guard, "Edge guard", "rolling slowly at a pit, the ball stops at its edge") \
    VAL(guard_reach, "Look-ahead ms", 300, 100, 600, "how far ahead it looks for a pit, as time at its speed") \
    VAL(guard_speed, "Up to speed", 6, 2, 13, "units a second it still stops from; its top is 11, a boost 13") \
    VAL(guard_brake, "Edge braking %", 300, 100, 500, "percent of the PS2's push") \
    FLAG(guard_push, "Push to roll in", true, "pushing the stick at the pit rolls in as on the PS2")

typedef struct NwAtlasMovement {
    NW_ATLAS_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwAtlasMovement;

/* The firefly's options ([game.movement.firefly]) in That Sinking Feeling, the same scheme. */
#define NW_FIREFLY_MOVEMENT(SW, FLAG, VAL) \
    SW(lock_on, "Clear lock-on", "a marker shows what X locks, the lock building up, locked, lost and the reload") \
    VAL(lock_ms, "Lock time ms", 1000, 300, 1300, "how long X is held before it is locked; the PS2's is 1300") \
    FLAG(lock_tap, "Quick tap fires", true, "letting go before it is locked fires once it is; the PS2's drops it") \
    SW(steering, "Sharper steering", "it banks, levels out, climbs and dives quicker, the same at 60 Hz as at 50") \
    VAL(steer_response, "Steering response %", 200, 100, 400, "how fast it banks into and out of a turn") \
    VAL(steer_turn, "Turn rate %", 125, 100, 200, "how fast a bank turns it, percent of the PS2's") \
    VAL(pitch_response, "Climb and dive response %", 200, 100, 400, "how fast it noses up and down")

typedef struct NwFireflyMovement {
    NW_FIREFLY_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwFireflyMovement;

/* The plane's options ([game.movement.plane]), the same scheme: Tornado Alley and Atmospheric Pressure. */
#define NW_PLANE_MOVEMENT(SW, FLAG, VAL) \
    SW(steering, "Sharper steering", "it noses up and down and banks sooner, the same at 60 Hz as at 50") \
    VAL(deadzone, "Stick deadzone %", 10, 5, 30, "how far the stick counts as centred; the PS2's 25") \
    VAL(pitch_response, "Pitch response %", 250, 100, 500, "how fast it noses up and down, percent of the PS2's") \
    VAL(bank_response, "Bank response %", 150, 100, 400, "how fast it banks into a turn, percent of the PS2's") \
    FLAG(push_dive, "Push to dive", true, "the stick forward noses down as on the PS2; off, forward climbs") \
    SW(aim, "Shots on the reticle", "in Tornado Alley the shots fly where the reticle points") \
    VAL(aim_assist, "Assist cone deg", 3, 0, 10, "an enemy this close to the reticle draws the shots; 0 none")

typedef struct NwPlaneMovement {
    NW_PLANE_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwPlaneMovement;

/* The space fighter's options ([game.movement.spacefighter]), the same scheme. */
#define NW_SPACE_MOVEMENT(SW, FLAG, VAL) \
    SW(steering, "Sharper steering", "the nose follows the stick sooner and a lighter push steers") \
    VAL(pitch_speed, "Climb and dive response %", 250, 100, 500, "how fast the nose moves, percent of the PS2's") \
    VAL(bank_speed, "Bank response %", 150, 100, 400, "how fast it banks into a turn, percent of the PS2's") \
    VAL(turn_rate, "Turn rate %", 125, 100, 200, "its turn at a full bank, percent of the PS2's 120 degrees a second") \
    VAL(deadzone, "Stick deadzone %", 10, 0, 25, "how far the stick moves before it steers; the PS2's is 25") \
    SW(roll, "Barrel roll keeps speed", "a barrel roll does not slow it down") \
    VAL(roll_kept, "Speed kept %", 100, 0, 100, "share of its speed a roll keeps; the PS2's slows to an eighth") \
    SW(aim, "True aim", "the reticle sits on the shots' path") \
    VAL(aim_range, "Reticle distance", 60, 20, 88, "how far ahead on the path; the shots reach 88") \
    FLAG(aim_sweep, "Shots cannot skip targets", true, "a shot hits what it passes through between two frames") \
    SW(aim_assist, "Aim assist", "the shots bend toward the nearest target in front") \
    VAL(assist_cone, "Assist cone deg", 6, 2, 20, "how far off the shots a target still pulls them") \
    VAL(assist_pull, "Assist pull %", 75, 0, 100, "how far they bend toward it")

typedef struct NwSpaceMovement {
    NW_SPACE_MOVEMENT(NW_CRASH_BOOL, NW_CRASH_BOOL, NW_CRASH_INT)
} NwSpaceMovement;

/* N. Hanced Movement ([game.movement]), speeds in percent of retail's; 0, the zeroed profile's, is retail's too. */
typedef struct NwMovement {
    int monkey_bar_speed; /* Crash on the monkey bars */
    NwCrashMovement crash;
    NwCocoMovement coco;
    NwMechMovement mech;
    NwSubMovement submarine;
    NwJeepMovement jeep;
    NwAtlasMovement atlasphere;
    NwFireflyMovement firefly;
    NwPlaneMovement plane;
    NwSpaceMovement spacefighter;
} NwMovement;

#define NW_CRASH_NONE(...)
#define NW_CRASH_ON(f, ...) .f = true,
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
      .spacefighter = { NW_SPACE_MOVEMENT(NW_CRASH_NONE, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) } }
#define NW_MOVEMENT_NHANCED \
    { .monkey_bar_speed = 350, .crash = { NW_CRASH_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .coco = { NW_COCO_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .mech = { NW_MECH_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .submarine = { NW_SUB_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .jeep = { NW_JEEP_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .atlasphere = { NW_ATLAS_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .firefly = { NW_FIREFLY_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .plane = { NW_PLANE_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) }, \
      .spacefighter = { NW_SPACE_MOVEMENT(NW_CRASH_ON, NW_CRASH_DEFAULT, NW_CRASH_DEFAULT) } }

/* Every value at retail's is the retail profile, every one at N. Hanced's the N. Hanced profile, anything else Custom. */
typedef enum NwMovementProfile {
    NW_MOVEMENT_PROFILE_RETAIL,
    NW_MOVEMENT_PROFILE_NHANCED,
    NW_MOVEMENT_PROFILE_CUSTOM,
} NwMovementProfile;

NwMovementProfile nw_movement_profile(const NwMovement *m);

typedef struct NwProfile {
    bool unused_title; /* the unused title menu (TITLES' own level) instead of the logos and the title cut */
    NwMovement movement;
    bool air_indicator; /* a ring on the floor under Crash while he is in the air */
    bool boss_health;   /* phase lines on the boss bar */
    bool hitboxes;      /* the debug menu's view of every collision shape, not saved */
} NwProfile;

extern NwProfile nw_profile;

/* The windowed boot, before the game thread starts. */
void nw_profile_apply(const NwProfile *p);
/* Asks the game thread for p's movement, air indicator and boss health at its next swap */
void nw_profile_request(const NwProfile *p);

/* Marks where a feature changes the game; reached with its NwProfile switch off (every retail run), the game stops. */
#define NW_DEVIATION(sw) (nw_profile.sw ? (void)0 : nw_deviation_off(#sw))
_Noreturn void nw_deviation_off(const char *sw);

/* The game thread at each swap, before the port's level poll (src/port/ps2video.c). */
void nw_gameplay_swap(void);

#endif
