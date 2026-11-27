#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "frontend/settings.h"
#include "platform/paths.h"

static int failures;
static char root[1100];

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed; last error: %s\n", __FILE__, __LINE__, #cond, SDL_GetError()); \
            failures++; \
        } \
    } while (0)

static bool write_text(const char *text)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", root, NW_PATHS_SETTINGS);
    return SDL_SaveFile(path, text, strlen(text));
}

static bool read_text(const char *text, NwSettings *s)
{
    nw_settings_defaults(s);
    return write_text(text) && nw_settings_load(s, root);
}

static bool exists(const char *name)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", root, name);
    return SDL_GetPathInfo(path, NULL);
}

static char *file_text(void)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s", root, NW_PATHS_SETTINGS);
    return SDL_LoadFile(path, NULL);
}

static void movement_key(const char *table, const char *key, NwSettings *s, const char *value)
{
    char text[128], *file = NULL;
    SDL_snprintf(text, sizeof(text), "[game.movement.%s]\n%s = %s\n", table, key, value);
    CHECK(read_text(text, s) && nw_settings_save(s, root) && (file = file_text()));
    SDL_snprintf(text, sizeof(text), "\n[game.movement.%s]\n", table);
    CHECK(file && SDL_strstr(file, text));
    SDL_snprintf(text, sizeof(text), "# game.movement.%s.%s: ", table, key);
    CHECK(file && SDL_strstr(file, text));
    SDL_free(file);
    nw_settings_defaults(s);
    CHECK(nw_settings_load(s, root));
}

static void movement_switch(const char *table, const char *key, size_t offset, bool def)
{
    NwSettings s;
    bool *field = (bool *)((char *)&s + offset);
    char text[128];
    nw_settings_defaults(&s);
    CHECK(*field == def);
    SDL_snprintf(text, sizeof(text), "[game.movement]\n%s = %s\n", key, def ? "false" : "true");
    CHECK(read_text(text, &s) && *field == def);
    movement_key(table, key, &s, def ? "false" : "true");
    CHECK(*field == !def);
}

static void movement_value(const char *table, const char *key, size_t offset, int def, int lo, int hi)
{
    NwSettings s;
    int *field = (int *)((char *)&s + offset);
    char value[16];
    nw_settings_defaults(&s);
    CHECK(*field == def);
    SDL_snprintf(value, sizeof(value), "%d", hi + 100);
    movement_key(table, key, &s, value);
    CHECK(*field == hi);
    SDL_snprintf(value, sizeof(value), "%d", lo - 100);
    movement_key(table, key, &s, value);
    CHECK(*field == lo);
}

#define CRASH_SWITCH(f, label, on, ...) movement_switch("crash", #f, offsetof(NwSettings, movement.crash.f), on);
#define CRASH_FLAG(f, label, def, ...) movement_switch("crash", #f, offsetof(NwSettings, movement.crash.f), def);
#define CRASH_VALUE(f, label, def, lo, hi, ...) \
    movement_value("crash", #f, offsetof(NwSettings, movement.crash.f), def, lo, hi);
#define COCO_SWITCH(f, label, on, ...) movement_switch("coco", #f, offsetof(NwSettings, movement.coco.f), on);
#define COCO_FLAG(f, label, def, ...) movement_switch("coco", #f, offsetof(NwSettings, movement.coco.f), def);
#define COCO_VALUE(f, label, def, lo, hi, ...) \
    movement_value("coco", #f, offsetof(NwSettings, movement.coco.f), def, lo, hi);
#define MECH_SWITCH(f, label, on, ...) movement_switch("mech", #f, offsetof(NwSettings, movement.mech.f), on);
#define MECH_FLAG(f, label, def, ...) movement_switch("mech", #f, offsetof(NwSettings, movement.mech.f), def);
#define MECH_VALUE(f, label, def, lo, hi, ...) \
    movement_value("mech", #f, offsetof(NwSettings, movement.mech.f), def, lo, hi);
#define SUB_SWITCH(f, label, on, ...) movement_switch("submarine", #f, offsetof(NwSettings, movement.submarine.f), on);
#define SUB_VALUE(f, label, def, lo, hi, ...) \
    movement_value("submarine", #f, offsetof(NwSettings, movement.submarine.f), def, lo, hi);
#define JEEP_SWITCH(f, label, on, ...) movement_switch("jeep", #f, offsetof(NwSettings, movement.jeep.f), on);
#define JEEP_FLAG(f, label, def, ...) movement_switch("jeep", #f, offsetof(NwSettings, movement.jeep.f), def);
#define JEEP_VALUE(f, label, def, lo, hi, ...) \
    movement_value("jeep", #f, offsetof(NwSettings, movement.jeep.f), def, lo, hi);
#define ATLAS_SWITCH(f, label, on, ...) \
    movement_switch("atlasphere", #f, offsetof(NwSettings, movement.atlasphere.f), on);
#define ATLAS_FLAG(f, label, def, ...) \
    movement_switch("atlasphere", #f, offsetof(NwSettings, movement.atlasphere.f), def);
#define ATLAS_VALUE(f, label, def, lo, hi, ...) \
    movement_value("atlasphere", #f, offsetof(NwSettings, movement.atlasphere.f), def, lo, hi);
#define FIREFLY_SWITCH(f, label, on, ...) movement_switch("firefly", #f, offsetof(NwSettings, movement.firefly.f), on);
#define FIREFLY_FLAG(f, label, def, ...) movement_switch("firefly", #f, offsetof(NwSettings, movement.firefly.f), def);
#define FIREFLY_VALUE(f, label, def, lo, hi, ...) \
    movement_value("firefly", #f, offsetof(NwSettings, movement.firefly.f), def, lo, hi);
#define PLANE_SWITCH(f, label, on, ...) movement_switch("plane", #f, offsetof(NwSettings, movement.plane.f), on);
#define PLANE_FLAG(f, label, def, ...) movement_switch("plane", #f, offsetof(NwSettings, movement.plane.f), def);
#define PLANE_VALUE(f, label, def, lo, hi, ...) \
    movement_value("plane", #f, offsetof(NwSettings, movement.plane.f), def, lo, hi);
#define SPACE_SWITCH(f, label, on, ...) \
    movement_switch("spacefighter", #f, offsetof(NwSettings, movement.spacefighter.f), on);
#define SPACE_FLAG(f, label, def, ...) \
    movement_switch("spacefighter", #f, offsetof(NwSettings, movement.spacefighter.f), def);
#define SPACE_VALUE(f, label, def, lo, hi, ...) \
    movement_value("spacefighter", #f, offsetof(NwSettings, movement.spacefighter.f), def, lo, hi);
#define TT_SWITCH(f, label, on, ...) \
    movement_switch("time_trial", #f, offsetof(NwSettings, movement.time_trial.f), on);

static int read_speed(const char *text)
{
    NwSettings s;
    return read_text(text, &s) ? s.hardware_speed : -1;
}

/* master_volume * 1000 + voice_volume; -1 when it does not load. */
static int read_volumes(const char *text)
{
    NwSettings s;
    return read_text(text, &s) ? s.master_volume * 1000 + s.voice_volume : -1;
}

static void profiles(void)
{
    const NwMovement nhanced = NW_MOVEMENT_NHANCED, retail = NW_MOVEMENT_RETAIL;
    NwMovement m = retail;
    char name[NW_PROFILE_NAME];
    size_t size = 0;
    char *text = nw_settings_profile_text(&nhanced, NW_PARTS_ALL, "Mine", &size);
    CHECK(text && SDL_strstr(text, "name = \"Mine\"") && SDL_strstr(text, "[game.movement.spacefighter]"));
    CHECK(text && nw_settings_profile_read(text, size, &m, name, sizeof(name)) == NW_PARTS_ALL);
    CHECK(SDL_strcmp(name, "Mine") == 0 && m.monkey_bar_speed == nhanced.monkey_bar_speed && m.crash.jump_fall &&
          m.crash.slide_ms == nhanced.crash.slide_ms && m.coco.grip && m.spacefighter.aim_assist);
    SDL_free(text);

    NwMovement coco = nhanced;
    coco.coco.run_top = 130;
    text = nw_settings_profile_text(&coco, 1u << NW_PART_COCO, "Coco", &size);
    CHECK(text && !SDL_strstr(text, "[game.movement.crash]") && !SDL_strstr(text, "monkey_bar_speed"));
    m = retail;
    CHECK(text && nw_settings_profile_read(text, size, &m, name, sizeof(name)) == 1u << NW_PART_COCO);
    CHECK(m.coco.run_top == 130 && m.coco.run_speed && !m.crash.jump_fall && m.monkey_bar_speed == 100);
    SDL_free(text);

    const char *bars = "[game.movement]\nmonkey_bar_speed = 200\n";
    m = retail;
    CHECK(nw_settings_profile_read(bars, SDL_strlen(bars), &m, name, sizeof(name)) == 1u << NW_PART_CRASH);
    CHECK(m.monkey_bar_speed == 200 && m.crash.jump_fall && !name[0]);

    const char *one = "[profile]\nname = \" a/b:c?. \"\n[game.movement.crash]\njump_height = 999\ngrip = false\n";
    m = retail;
    CHECK(nw_settings_profile_read(one, SDL_strlen(one), &m, name, sizeof(name)) == 1u << NW_PART_CRASH);
    CHECK(m.crash.jump_height == 130 && !m.crash.grip && m.crash.slide && SDL_strcmp(name, "abc") == 0 &&
          m.monkey_bar_speed == nhanced.monkey_bar_speed && !m.jeep.steering);

    const char *none = "[game]\nwombat = true\n", *bad = "not toml [";
    m = retail;
    CHECK(nw_settings_profile_read(none, SDL_strlen(none), &m, name, sizeof(name)) == 0);
    CHECK(nw_settings_profile_read(bad, SDL_strlen(bad), &m, name, sizeof(name)) == 0 && m.monkey_bar_speed == 100);

    m = retail;
    nw_settings_profile_apply(&m, &nhanced, (1u << NW_PART_CRASH) | (1u << NW_PART_JEEP));
    CHECK(m.monkey_bar_speed == nhanced.monkey_bar_speed && m.crash.jump_fall && m.jeep.steering && !m.coco.run_speed);

    char messy[] = "  Fast\tRun..  ";
    nw_settings_profile_name(messy);
    CHECK(SDL_strcmp(messy, "FastRun") == 0);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_settings <scratch dir>\n");
        return 2;
    }
    if (!nw_paths_remove_tree(argv[1]) || !SDL_CreateDirectory(argv[1])) {
        fprintf(stderr, "%s: %s\n", argv[1], SDL_GetError());
        return 1;
    }
    SDL_snprintf(root, sizeof(root), "%s/", argv[1]);

    NwSettings s;
    nw_settings_defaults(&s);
    CHECK(!s.hardware_speed);
    s.hardware_speed = true;
    CHECK(nw_settings_load(&s, root) && s.hardware_speed);
    CHECK(nw_settings_save(&s, root));
    s.hardware_speed = false;
    CHECK(nw_settings_load(&s, root) && s.hardware_speed);

    CHECK(read_speed("# settings\n\n  [ game ]  \r\n\thardware_speed=true # on\r\n") == 1);
    CHECK(read_speed("hardware_speed = true\n") == 0);
    CHECK(read_speed("[video]\nhardware_speed = true\n[game]\nspeed = true\n") == 0);
    CHECK(read_speed("[game]\nhardware_speed = \"true\"\n") == 0);
    CHECK(read_speed("") == 0);

    CHECK(read_speed("[game]\nhardware_speed = true\nhardware_speed = false\n") == -1);
    CHECK(exists(NW_PATHS_SETTINGS ".corrupt") && !exists(NW_PATHS_SETTINGS));
    CHECK(read_speed("[game]\nhardware_speed = yes\n") == -1);
    CHECK(read_speed("[game]\nhardware_speed = true\n[game\nhardware_speed = false\n") == -1);
    nw_settings_defaults(&s);
    s.hardware_speed = true;
    CHECK(write_text("[game]\nhardware_speed = false\n[game]\n") && !nw_settings_load(&s, root) && s.hardware_speed);
    s.hardware_speed = true;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.hardware_speed);

    nw_settings_defaults(&s);
    CHECK(s.master_volume == 100 && s.voice_volume == 100);
    s.master_volume = 40;
    s.voice_volume = 7;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.master_volume == 40 && s.voice_volume == 7);
    CHECK(read_volumes("[audio]\nmaster_volume = 55\nvoice_volume=0\n") == 55000);
    CHECK(read_volumes("[audio]\nmaster_volume = 250\nvoice_volume = -3\n") == 100000);
    CHECK(read_volumes("[audio]\nmaster_volume = \"5\"\nvoice_volume = 0x10\n") == 100016);
    CHECK(read_volumes("[game]\nmaster_volume = 5\n") == 100100);

    nw_settings_defaults(&s);
    CHECK(!s.retail_menu);
    CHECK(read_text("[game]\nretail_menu = true\n", &s) && s.retail_menu);
    nw_settings_defaults(&s);
    CHECK(s.air_indicator);
    CHECK(read_text("[game]\nair_indicator = false\n", &s) && !s.air_indicator);
    CHECK(nw_settings_save(&s, root));
    char *air = file_text();
    CHECK(air && SDL_strstr(air, "\nair_indicator = false\n") && SDL_strstr(air, "# game.air_indicator: "));
    SDL_free(air);
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && !s.air_indicator);
    nw_settings_defaults(&s);
    CHECK(s.boss_health);
    CHECK(read_text("[game]\nboss_health = false\n", &s) && !s.boss_health);
    CHECK(nw_settings_save(&s, root));
    char *boss = file_text();
    CHECK(boss && SDL_strstr(boss, "\nboss_health = false\n") && SDL_strstr(boss, "# game.boss_health: "));
    SDL_free(boss);
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && !s.boss_health);
    nw_settings_defaults(&s);
    CHECK(s.wombat);
    CHECK(read_text("[game]\nwombat = false\n", &s) && !s.wombat);
    nw_settings_defaults(&s);
    CHECK(s.hub_minigame);
    CHECK(read_text("[game]\nhub_minigame = false\n", &s) && !s.hub_minigame);
    CHECK(s.first_boss_cut);
    CHECK(read_text("[game]\nfirst_boss_cut = false\n", &s) && !s.first_boss_cut);
    CHECK(s.vehicle_hints);
    CHECK(read_text("[game]\nvehicle_hints = false\n", &s) && !s.vehicle_hints);
    CHECK(s.bug_fixes);
    CHECK(read_text("[game]\nbug_fixes = false\n", &s) && !s.bug_fixes);
    CHECK(!s.long_time_names);
    CHECK(read_text("[game]\nlong_time_names = true\n", &s) && s.long_time_names);
    nw_settings_defaults(&s);
    CHECK(s.relic_spin);
    CHECK(read_text("[game]\nrelic_spin = false\n", &s) && !s.relic_spin);
    nw_settings_defaults(&s);
    CHECK(s.discord);
    CHECK(read_text("[discord]\nenabled = false\n", &s) && !s.discord);
    CHECK(nw_settings_save(&s, root));
    char *discord = file_text();
    CHECK(discord && SDL_strstr(discord, "\n[discord]\nenabled = false\n") && SDL_strstr(discord, "# discord.enabled: "));
    SDL_free(discord);
    nw_settings_defaults(&s);
    CHECK(s.updates);
    CHECK(read_text("[updates]\ncheck = false\n", &s) && !s.updates);
    nw_settings_defaults(&s);
    CHECK(SDL_strcmp(s.log, "info") == 0 && !s.gl_debug && s.minidump);
    CHECK(read_text("[log]\nlevel = \"info,gfx=debug\"\ngl_debug = true\nminidump = false\n", &s) &&
          SDL_strcmp(s.log, "info,gfx=debug") == 0 && s.gl_debug && !s.minidump);
    CHECK(nw_settings_save(&s, root));
    char *log = file_text();
    CHECK(log && SDL_strstr(log, "\n[log]\nlevel = \"info,gfx=debug\"\ngl_debug = true\nminidump = false\n") &&
          SDL_strstr(log, "# log.level: "));
    SDL_free(log);
    nw_settings_defaults(&s);
    CHECK(s.camera_stability == 50);
    CHECK(read_text("[game]\ncamera_stability = 40\n", &s) && s.camera_stability == 40);
    CHECK(read_text("version = 2\n[game]\ncamera_stability = 300\n", &s) && s.camera_stability == 100);
    nw_settings_defaults(&s);
    CHECK(s.draw_distance == 400);
    CHECK(read_text("[video]\ndraw_distance = 250\n", &s) && s.draw_distance == 250);
    CHECK(read_text("[video]\ndraw_distance = 50\n", &s) && s.draw_distance == 100);
    CHECK(read_text("[video]\ndraw_distance = 900\n", &s) && s.draw_distance == NW_DRAW_DISTANCE_MAX);
    nw_settings_defaults(&s);
    CHECK(s.fog == 40 && !s.blur && !s.haze);
    CHECK(read_text("version = 2\n[video]\nfog = 100\nblur = true\nhaze = true\n", &s) && s.fog == 100 && s.blur &&
          s.haze);
    CHECK(read_text("[video]\nfog = -5\n", &s) && s.fog == 0);
    CHECK(read_text("version = 2\n[video]\nfog = 250\n", &s) && s.fog == 100);
    CHECK(read_text("[game]\ncamera_stability = 100\n[video]\nfog = 100\nblur = true\nhaze = true\n", &s) &&
          s.camera_stability == 50 && s.fog == 40 && !s.blur && !s.haze);
    CHECK(read_text("[game]\ncamera_stability = 0\n[video]\nfog = 70\n", &s) && s.camera_stability == 0 &&
          s.fog == 70);
    s.camera_stability = 100;
    s.fog = 100;
    s.blur = true;
    CHECK(nw_settings_save(&s, root));
    char *versioned = file_text();
    CHECK(versioned && SDL_strstr(versioned, "\nversion = 2\n") && SDL_strstr(versioned, "# version: "));
    SDL_free(versioned);
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.camera_stability == 100 && s.fog == 100 && s.blur);
    nw_settings_defaults(&s);
    int kept = -1;
    nw_settings_level_fog(&s, 2, &kept);
    CHECK(s.fog == 40 && kept == -1);
    nw_settings_level_fog(&s, NW_SETTINGS_SKYLESS_LEVEL, &kept);
    CHECK(s.fog == 100 && kept == 40);
    s.fog = 60;
    nw_settings_level_fog(&s, NW_SETTINGS_SKYLESS_LEVEL, &kept);
    CHECK(s.fog == 60 && kept == 40);
    nw_settings_level_fog(&s, -1, &kept);
    CHECK(s.fog == 40 && kept == -1);
    nw_settings_defaults(&s);
    CHECK(s.movement.monkey_bar_speed == ((NwMovement)NW_MOVEMENT_NHANCED).monkey_bar_speed);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 150\n", &s) && s.movement.monkey_bar_speed == 150);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 900\n", &s) && s.movement.monkey_bar_speed == 500);
    CHECK(read_text("[game]\nmonkey_bar_speed = 150\n", &s) && s.movement.monkey_bar_speed == 335);
    s.movement.monkey_bar_speed = 100;
    CHECK(nw_settings_save(&s, root));
    char *movement = file_text();
    CHECK(movement && SDL_strstr(movement, "\n[game.movement]\nmonkey_bar_speed = 100\n") &&
          SDL_strstr(movement, "# game.movement.monkey_bar_speed: "));
    SDL_free(movement);
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.movement.monkey_bar_speed == 100);
    NW_CRASH_MOVEMENT(CRASH_SWITCH, CRASH_FLAG, CRASH_VALUE)
    NW_MECH_MOVEMENT(MECH_SWITCH, MECH_FLAG, MECH_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.crash.run_speed &&
          !s.movement.crash.air_jump && s.movement.crash.air_jump_ledge && s.movement.crash.run_top == 120);
    CHECK(!s.movement.mech.stand_jump && !s.movement.mech.hitbox && s.movement.mech.shot_tap &&
          s.movement.mech.shot_stop_ms == 25);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.crash.grip && s.movement.crash.air_jump && s.movement.mech.stand_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.mech]\naim_fast = true\n", &s) &&
          !s.movement.crash.air_jump && s.movement.mech.aim_fast && s.movement.mech.stand_jump);
    NW_COCO_MOVEMENT(COCO_SWITCH, COCO_FLAG, COCO_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.coco.run_speed &&
          !s.movement.coco.grip && s.movement.coco.run_top == 110);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.coco.grip && s.movement.coco.run_speed && !s.movement.coco.jump_fall);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.coco]\ngrip = false\n", &s) &&
          !s.movement.coco.grip && s.movement.coco.run_speed && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.coco.grip);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.crash.jump_fall &&
          !s.movement.crash.run_speed);
    NW_SUB_MOVEMENT(SUB_SWITCH, SUB_SWITCH, SUB_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.submarine.turning &&
          !s.movement.submarine.hitbox && s.movement.submarine.speed_top == 110);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.submarine.speed);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.submarine]\nhitbox = true\n", &s) &&
          s.movement.submarine.hitbox && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.submarine.turning);
    NW_JEEP_MOVEMENT(JEEP_SWITCH, JEEP_FLAG, JEEP_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.jeep.steering &&
          !s.movement.jeep.speed && s.movement.jeep.steer_response == 120);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.jeep.steering);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.jeep]\nspeed = false\n", &s) &&
          !s.movement.jeep.speed && s.movement.jeep.steering && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.jeep.steering);
    NW_ATLAS_MOVEMENT(ATLAS_SWITCH, ATLAS_FLAG, ATLAS_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.atlasphere.brake &&
          !s.movement.atlasphere.edge_guard && s.movement.atlasphere.guard_push &&
          s.movement.atlasphere.brake_force == 250);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.atlasphere.brake && !s.movement.atlasphere.edge_guard);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.atlasphere]\nedge_guard = true\n", &s) &&
          s.movement.atlasphere.brake && s.movement.atlasphere.edge_guard && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && !s.movement.atlasphere.edge_guard &&
          s.movement.atlasphere.brake && s.movement.atlasphere.steer_turn == 250);
    NW_FIREFLY_MOVEMENT(FIREFLY_SWITCH, FIREFLY_FLAG, FIREFLY_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.firefly.lock_on &&
          !s.movement.firefly.steering && s.movement.firefly.lock_tap && s.movement.firefly.lock_ms == 1000);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.firefly.lock_on && s.movement.firefly.steering);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.firefly]\nsteering = false\n", &s) &&
          s.movement.firefly.lock_on && !s.movement.firefly.steering && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.firefly.steering);
    NW_PLANE_MOVEMENT(PLANE_SWITCH, PLANE_FLAG, PLANE_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.plane.steering &&
          !s.movement.plane.aim && s.movement.plane.push_dive && s.movement.plane.pitch_response == 200);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.plane.steering && s.movement.plane.aim);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.plane]\naim = false\n", &s) &&
          s.movement.plane.steering && !s.movement.plane.aim && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.plane.steering);
    NW_SPACE_MOVEMENT(SPACE_SWITCH, SPACE_FLAG, SPACE_VALUE)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.spacefighter.steering &&
          !s.movement.spacefighter.aim_assist && s.movement.spacefighter.aim_sweep &&
          s.movement.spacefighter.pitch_speed == 250);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.spacefighter.steering && s.movement.spacefighter.aim);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.spacefighter]\nroll = false\n", &s) &&
          !s.movement.spacefighter.roll && s.movement.spacefighter.aim && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.spacefighter.aim_assist);
    NW_TT_MOVEMENT(TT_SWITCH, TT_SWITCH, TT_SWITCH)
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n", &s) && !s.movement.time_trial.no_masks &&
          !s.movement.time_trial.no_fruit_clock);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.crash]\ngrip = true\n", &s) &&
          s.movement.time_trial.no_masks && s.movement.time_trial.no_fruit_clock);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 100\n[game.movement.time_trial]\nno_masks = false\n", &s) &&
          !s.movement.time_trial.no_masks && s.movement.time_trial.no_fruit_clock && !s.movement.crash.air_jump);
    CHECK(read_text("[game.movement]\nmonkey_bar_speed = 350\n", &s) && s.movement.time_trial.no_masks);

    CHECK(read_speed("[game]\nhz = 50\npal_60hz = false\nhardware_speed = true\n") == 1);

    float a = -1.0f;
    char name[16];
    CHECK(nw_settings_aspect("auto", &a) && a == 0.0f);
    CHECK(nw_settings_aspect("\"16:9\"", &a) && SDL_fabsf(a - 16.0f / 9.0f) < 1e-6f);
    CHECK(nw_settings_aspect("2.39", &a) && SDL_fabsf(a - 2.39f) < 1e-6f);
    CHECK(nw_settings_aspect("3440:1440", &a) && SDL_fabsf(a - 3440.0f / 1440.0f) < 1e-6f);
    a = 1.5f;
    CHECK(!nw_settings_aspect("wide", &a) && !nw_settings_aspect("16:", &a) && !nw_settings_aspect("0:9", &a) &&
          !nw_settings_aspect("-2", &a) && !nw_settings_aspect("\"16:9", &a) && !nw_settings_aspect("", &a) &&
          a == 1.5f);
    CHECK(SDL_strcmp(nw_settings_aspect_name(0.0f, name, sizeof(name)), "auto") == 0);
    CHECK(SDL_strcmp(nw_settings_aspect_name(21.0f / 9.0f, name, sizeof(name)), "21:9") == 0);
    CHECK(SDL_strcmp(nw_settings_aspect_name(2.39f, name, sizeof(name)), "2.39") == 0);
    nw_settings_defaults(&s);
    CHECK(s.aspect == 0.0f);
    s.aspect = 32.0f / 9.0f;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && SDL_fabsf(s.aspect - 32.0f / 9.0f) < 1e-6f);
    CHECK(read_text("[video]\naspect = \"16:10\" # quoted\n", &s) && SDL_fabsf(s.aspect - 1.6f) < 1e-6f);
    CHECK(read_text("[video]\naspect = \"square\"\n", &s) && s.aspect == 0.0f);
    CHECK(s.hud == 16.0f / 9.0f);
    CHECK(nw_settings_hud("edges", &a) && a == 0.0f && nw_settings_hud("\"4:3\"", &a) &&
          SDL_fabsf(a - 4.0f / 3.0f) < 1e-6f);
    CHECK(!nw_settings_hud("auto", &a) && !nw_settings_hud("left", &a) && SDL_fabsf(a - 4.0f / 3.0f) < 1e-6f);
    CHECK(SDL_strcmp(nw_settings_hud_name(0.0f, name, sizeof(name)), "edges") == 0);
    s.hud = 0.0f;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.hud == 0.0f);

    CHECK(read_text("[video]\nwindow = \"borderless\"\n", &s) && s.window == NW_WINDOW_BORDERLESS);
    CHECK(read_text("[video]\nwindow = \"maximised\"\n", &s) && s.window == NW_WINDOW_WINDOWED);
    s.window = NW_WINDOW_FULLSCREEN;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.window == NW_WINDOW_FULLSCREEN);

    CHECK(read_text("[video]\nvsync = \"adaptive\"\n", &s) && s.vsync == NW_VSYNC_ADAPTIVE);
    CHECK(read_text("[video]\nvsync = true\n", &s) && s.vsync == NW_VSYNC_ON);

    CHECK(read_text("[video]\nscale = 3\n", &s) && s.scale == 3);
    CHECK(read_text("[video]\nscale = 99\n", &s) && s.scale == NW_BACKEND_SCALE_MAX);
    CHECK(read_text("[video]\nscale = \"auto\"\n", &s) && s.scale == 0);
    s.scale = 2;
    CHECK(read_text("[video]\nscale = \"big\"\n", &s) && s.scale == 0);
    s.scale = 0;
    CHECK(nw_settings_save(&s, root));
    char *saved = file_text();
    CHECK(saved && SDL_strstr(saved, "scale = \"auto\"\n"));
    SDL_free(saved);
    s.scale = 4;
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && s.scale == 4);

    CHECK(read_text("[video]\nfps = 120\n", &s) && s.fps == 120);
    CHECK(read_text("[video]\nfps = \"refresh\"\n", &s) && s.fps == 0);
    CHECK(read_text("[video]\nfps = 5000\n", &s) && s.fps == NW_FPS_MAX);

    CHECK(read_text("[video]\nmsaa = 4\n", &s) && s.msaa == 4);
    CHECK(read_text("[video]\nmsaa = -2\n", &s) && s.msaa == 0);

    CHECK(read_text("[video]\nfilter = \"bilinear\"\n", &s) && s.filter == NW_FILTER_BILINEAR);
    CHECK(read_text("[video]\n", &s) && SDL_strcmp(s.gpu, "auto") == 0);
    CHECK(read_text("[video]\ngpu = \"Intel(R) UHD Graphics 620\"\n", &s) &&
          SDL_strcmp(s.gpu, "Intel(R) UHD Graphics 620") == 0);
    CHECK(read_text("[video]\ngpu = \"\"\n", &s) && SDL_strcmp(s.gpu, "auto") == 0);
    SDL_zero(s.gpu);
    SDL_strlcpy(s.gpu, "NVIDIA GeForce RTX 4060 Laptop GPU", sizeof(s.gpu));
    CHECK(nw_settings_save(&s, root));
    nw_settings_defaults(&s);
    CHECK(nw_settings_load(&s, root) && SDL_strcmp(s.gpu, "NVIDIA GeForce RTX 4060 Laptop GPU") == 0);
    nw_settings_defaults(&s);
    CHECK(s.gems);
    CHECK(read_text("[video]\ngems = false\n", &s) && !s.gems);

    NwResolution res = { 1, 1 };
    CHECK(nw_settings_resolution("auto", &res) && res.w == 0 && res.h == 0);
    CHECK(nw_settings_resolution("2560x1440", &res) && res.w == 2560 && res.h == 1440);
    CHECK(!nw_settings_resolution("1920", &res) && !nw_settings_resolution("1920x", &res) &&
          !nw_settings_resolution("x1080", &res) && !nw_settings_resolution("0x1080", &res) &&
          !nw_settings_resolution("1920x1080p", &res) && !nw_settings_resolution("99999x1080", &res) &&
          res.w == 2560 && res.h == 1440);
    CHECK(SDL_strcmp(nw_settings_resolution_name((NwResolution){ 0, 0 }, name, sizeof(name)), "auto") == 0);
    CHECK(SDL_strcmp(nw_settings_resolution_name(res, name, sizeof(name)), "2560x1440") == 0);
    CHECK(read_text("[video]\nresolution = \"1280x800\"\n", &s) && s.resolution.w == 1280 &&
          s.resolution.h == 800);
    CHECK(read_text("[video]\nresolution = \"big\"\n", &s) && s.resolution.w == 0);

    CHECK(read_text("[pad]\ncross = \"east\"\ncircle = [\"south\", \"jump\", \"righttrigger\", \"south\"]\n"
                    "l2 = []\nr2 = 7\n",
                    &s));
    CHECK(s.controls.pad[0][0] == 1 + SDL_GAMEPAD_BUTTON_EAST && s.controls.pad[0][1] == 0);
    CHECK(s.controls.pad[1][0] == 1 + SDL_GAMEPAD_BUTTON_SOUTH &&
          s.controls.pad[1][1] == NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER && s.controls.pad[1][2] == 0);
    CHECK(s.controls.pad[6][0] == 0 && s.controls.pad[7][0] == NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    CHECK(s.controls.pad[3][0] == 1 + SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(nw_settings_save(&s, root));
    saved = file_text();
    CHECK(saved && SDL_strstr(saved, "[pad]\ncross = [\"east\"]\ncircle = [\"south\", \"righttrigger\"]\n") &&
          SDL_strstr(saved, "l2 = []\n") && SDL_strstr(saved, "# pad.<button>: "));
    SDL_free(saved);

    CHECK(s.controls.left_deadzone == 0 && s.controls.right_deadzone == 0 && s.controls.trigger_press == 25);
    CHECK(read_text("[input]\nleft_deadzone = 15\nright_deadzone = 95\ntrigger_press = 0\n", &s) &&
          s.controls.left_deadzone == 15 && s.controls.right_deadzone == 90 && s.controls.trigger_press == 1);
    CHECK(s.controls.prompts == NW_PROMPTS_AUTO);
    CHECK(read_text("[input]\nprompts = \"nintendo\"\n", &s) && s.controls.prompts == NW_PROMPTS_NINTENDO);
    CHECK(nw_settings_save(&s, root));
    saved = file_text();
    CHECK(saved && SDL_strstr(saved, "prompts = \"nintendo\"\n"));
    SDL_free(saved);
    CHECK(read_text("[input]\nprompts = \"gamecube\"\n", &s) && s.controls.prompts == NW_PROMPTS_AUTO);

    CHECK(s.controls.keys[2][0] == SDL_SCANCODE_E && s.controls.keys[2][1] == SDL_SCANCODE_J);
    CHECK(read_text("[keyboard]\nsquare = [\"Left Shift\", \"no such key\"]\nright_stick_up = \"I\"\nstart = []\n",
                    &s));
    CHECK(s.controls.keys[2][0] == SDL_SCANCODE_LSHIFT && s.controls.keys[2][1] == 0);
    CHECK(s.controls.keys[NW_CONTROL_STICKS + 4][0] == SDL_SCANCODE_I && s.controls.keys[11][0] == 0);
    CHECK(s.controls.keys[NW_CONTROL_STICKS][0] == SDL_SCANCODE_W);
    CHECK(nw_settings_save(&s, root));
    saved = file_text();
    CHECK(saved && SDL_strstr(saved, "[keyboard]\nsquare = [\"Left Shift\"]\n") &&
          SDL_strstr(saved, "cross = [\"Space\"]\ncircle = [\"C\", \"K\"]\n") &&
          SDL_strstr(saved, "right_stick_up = [\"I\"]\n") && SDL_strstr(saved, "right_stick_down = []\n"));
    SDL_free(saved);
    NwSettings u;
    nw_settings_defaults(&u);
    CHECK(!nw_settings_equal(&s, &u) && nw_settings_load(&u, root) && nw_settings_equal(&s, &u));

    NwSettings t;
    nw_settings_defaults(&t);
    s = t;
    s.resolution.h = 1;
    CHECK(nw_settings_equal(&t, &t) && !nw_settings_equal(&s, &t));
    CHECK(read_text("[video]\nfov = 90\naspect = \"4:3\"\n\n[mods.example]\nenabled = true\n", &s));
    s.aspect = 0.0f;
    CHECK(nw_settings_save(&s, root) && !exists(NW_PATHS_SETTINGS ".tmp"));
    char *text = file_text();
    CHECK(text && SDL_strncmp(text, "# ", 2) == 0 && SDL_strstr(text, "# video.aspect: ") &&
          SDL_strstr(text, "[video]\nfov = 90\naspect = \"auto\"\n") &&
          SDL_strstr(text, "[mods.example]\nenabled = true\n") && SDL_strstr(text, "[audio]\n"));
    SDL_free(text);

    NwSettings h;
    nw_settings_defaults(&h);
    CHECK(h.hub_gem && nw_settings_save(&h, root));
    text = file_text();
    CHECK(text && !SDL_strstr(text, "hub_gem"));
    SDL_free(text);
    h.hub_gem = false;
    CHECK(nw_settings_save(&h, root));
    text = file_text();
    CHECK(text && SDL_strstr(text, "hub_gem = false\n") && !SDL_strstr(text, "game.hub_gem"));
    SDL_free(text);
    NwSettings back;
    nw_settings_defaults(&back);
    CHECK(nw_settings_load(&back, root) && !back.hub_gem && nw_settings_equal(&h, &back));
    h.hub_gem = true;
    CHECK(nw_settings_save(&h, root));
    text = file_text();
    CHECK(text && !SDL_strstr(text, "hub_gem"));
    SDL_free(text);
    CHECK(read_text("[game]\nhub_gem = false\n", &back) && !back.hub_gem);

    CHECK(read_text("[game]\nsubtitles = true\n", &back) && back.subtitles && !back.title_subtitles);
    CHECK(nw_settings_save(&back, root));
    nw_settings_defaults(&h);
    CHECK(!h.subtitles && nw_settings_load(&h, root) && h.subtitles);
    CHECK(nw_settings_equal(&h, &back));
    h.subtitles = false;
    CHECK(!nw_settings_equal(&h, &back));
    CHECK(read_text("[game]\nsubtitles = true\ntitle_subtitles = true\n", &back) && back.title_subtitles);
    CHECK(nw_settings_save(&back, root));
    nw_settings_defaults(&h);
    CHECK(!h.title_subtitles && nw_settings_load(&h, root) && h.title_subtitles);
    CHECK(nw_settings_equal(&h, &back));

    profiles();

    if (failures) {
        fprintf(stderr, "settings: %d checks failed\n", failures);
        return 1;
    }
    printf("settings: all checks passed\n");
    return 0;
}
