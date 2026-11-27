#include "gameplay/vehiclehints.h"

#include <SDL3/SDL.h>

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu_port.h"
#include "port/subtitles.h"

extern struct game_s Game;
extern struct cursor_s Cursor;
extern s32 PLAYERCOUNT, VEHICLECONTROL, Demo, Paused, Level;
extern f32 vtog_time, vtog_duration;
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);

#define NW_VEHICLE_HINT_SECONDS 8
#define NW_VEHICLE_HINT_RIGHT 0.95f
#define NW_VEHICLE_HINT_BOTTOM -0.6f
#define NW_VEHICLE_HINT_STEP 0.1f
#define NW_VEHICLE_HINT_SIZE 0.45f
#define NW_VEHICLE_HINT_WIDTH 0.8f
#define NW_VEHICLE_HINT_NOTE 0.7f
#define NW_VEHICLE_HINT_SEEN_HUB 0
#define NW_VEHICLE_HINT_AIRSHIP 0x24

enum {
    NW_HINT_ACCELERATE,
    NW_HINT_JUMP,
    NW_HINT_TAP,
    NW_HINT_FORWARD,
    NW_HINT_BACK,
    NW_HINT_SPIN,
    NW_HINT_AIM,
    NW_HINT_RELEASE,
    NW_HINT_TORPEDO,
    NW_HINT_BOMB,
    NW_HINT_SWIM,
    NW_HINT_FIRE,
    NW_HINT_ROLL,
    NW_HINT_LOCK,
    NW_HINT_SPRAY,
    NW_HINT_SLOWER,
    NW_HINT_ACTIONS
};

static const char *const nw_vehicle_hint_action[NW_HINT_ACTIONS][6] = {
    { "HOLD TO ACCELERATE", "MAINTIENS: ACCELERER", "HALTEN: GAS GEBEN", "TIENI: ACCELERA", "MANT\220N: ACELERAR",
      "INHOUDEN: GAS GEVEN" },
    { "JUMP", "SAUTER", "SPRINGEN", "SALTA", "SALTAR", "SPRINGEN" },
    { "TAP TO SPEED UP", "TAPOTE: ACCELERER", "TIPPEN: SCHNELLER", "PREMI SPESSO: ACCELERA",
      "PULSA SEGUIDO: ACELERAR", "TIKKEN: SNELLER" },
    { "HOLD TO FLY FORWARD", "MAINTIENS: AVANCER", "HALTEN: VORW\216RTS", "TIENI: AVANTI", "MANT\220N: AVANZAR",
      "INHOUDEN: VOORUIT" },
    { "HOLD TO FLY BACK", "MAINTIENS: RECULER", "HALTEN: ZUR\232CK", "TIENI: INDIETRO", "MANT\220N: RETROCEDER",
      "INHOUDEN: ACHTERUIT" },
    { "SPIN", "TOURNER", "DREHEN", "GIRA", "GIRAR", "DRAAIEN" },
    { "HOLD TO AIM", "MAINTIENS: VISER", "HALTEN: ZIELEN", "TIENI: MIRA", "MANT\220N: APUNTAR", "INHOUDEN: RICHTEN" },
    { "RELEASE TO FIRE", "RELACHE: TIRER", "LOSLASSEN: FEUERN", "RILASCIA: SPARA", "SUELTA: DISPARAR",
      "LOSLATEN: VUREN" },
    { "TORPEDO", "TORPILLE", "TORPEDO", "SILURO", "TORPEDO", "TORPEDO" },
    { "BOMB", "BOMBE", "BOMBE", "BOMBA", "BOMBA", "BOM" },
    { "SWIM FASTER", "NAGER PLUS VITE", "SCHNELLER SCHWIMMEN", "NUOTA PI\227 VELOCE", "NADAR M\240S R\240PIDO",
      "SNELLER ZWEMMEN" },
    { "HOLD TO FIRE", "MAINTIENS: TIRER", "HALTEN: FEUERN", "TIENI: SPARA", "MANT\220N: DISPARAR", "INHOUDEN: VUREN" },
    { "BARREL ROLL", "TONNEAU", "FASSROLLE", "AVVITAMENTO", "TONEL", "ROLLEN" },
    { "HOLD TO LOCK ON", "MAINTIENS: VERROUILLER", "HALTEN: ANVISIEREN", "TIENI: AGGANCIA", "MANT\220N: FIJAR",
      "INHOUDEN: RICHTEN" },
    { "HOLD TO SPRAY WATER", "MAINTIENS: ARROSER", "HALTEN: WASSER SPRITZEN", "TIENI: SPRUZZA ACQUA",
      "MANT\220N: ECHAR AGUA", "INHOUDEN: WATER SPUITEN" },
    { "YOU GO SLOWER WHILE SPRAYING", "TU RALENTIS EN ARROSANT", "BEIM SPRITZEN BIST DU LANGSAMER",
      "SPRUZZANDO VAI PI\227 PIANO", "VAS M\240S LENTO AL ECHAR AGUA", "SPUITEN MAAKT JE TRAGER" },
};

typedef struct NwVehicleHint {
    int ride;
    struct {
        int action;
        const char *glyphs;
    } line[NW_VEHICLE_HINT_LINES];
} NwVehicleHint;

static const NwVehicleHint nw_vehicle_hints[] = {
    { 0x6B, { { NW_HINT_ACCELERATE, "o" }, { NW_HINT_JUMP, "x" } } },
    { 0xA0, { { NW_HINT_ACCELERATE, "o" }, { NW_HINT_JUMP, "x" } } },
    { 0x99, { { NW_HINT_ACCELERATE, "o" } } },
    { 0xB2, { { NW_HINT_SPRAY, "s" }, { NW_HINT_SLOWER, "" }, { NW_HINT_JUMP, "x" } } },
    { 0x89, { { NW_HINT_TAP, "x" }, { NW_HINT_JUMP, "s" } } },
    { 0x3B, { { NW_HINT_FORWARD, "x" }, { NW_HINT_BACK, "o" }, { NW_HINT_SPIN, "s" } } },
    { 0x44, { { NW_HINT_JUMP, "x" }, { NW_HINT_AIM, "s" }, { NW_HINT_RELEASE, "s" } } },
    { 0x20, { { NW_HINT_TORPEDO, "s" }, { NW_HINT_BOMB, "x" } } },
    { NW_VEHICLE_HINT_SWIM, { { NW_HINT_SWIM, "x" }, { NW_HINT_SPIN, "s" } } },
    { 0x36, { { NW_HINT_FIRE, "x" }, { NW_HINT_ROLL, "u v" } } },
    { 0x81, { { NW_HINT_FIRE, "x" }, { NW_HINT_ROLL, "u v" } } },
    { 0x8B, { { NW_HINT_LOCK, "x" }, { NW_HINT_RELEASE, "x" } } },
};

static struct {
    int ride;
    int frames;
} nw_vehicle_hint = { -1, 0 };

static int nw_vehicle_hints_find(int ride)
{
    for (int i = 0; i < (int)SDL_arraysize(nw_vehicle_hints); i++) {
        if (nw_vehicle_hints[i].ride == ride) {
            return i;
        }
    }
    return -1;
}

int nw_vehicle_hints_lines(int ride, char lines[NW_VEHICLE_HINT_LINES][NW_VEHICLE_HINT_TEXT])
{
    u32 l = (u32)Game.language;
    int k = nw_vehicle_hints_find(ride);
    if (l >= 6 || Level == NW_VEHICLE_HINT_AIRSHIP || k < 0) {
        return 0;
    }
    const NwVehicleHint *h = &nw_vehicle_hints[k];
    int n = 0;
    while (n < NW_VEHICLE_HINT_LINES && h->line[n].glyphs) {
        SDL_snprintf(lines[n], NW_VEHICLE_HINT_TEXT, *h->line[n].glyphs ? "%s %s" : "%s",
                     nw_vehicle_hint_action[h->line[n].action][l], h->line[n].glyphs);
        n++;
    }
    return n;
}

static u32 nw_vehicle_hints_seen(void)
{
    const struct hub_s *h = &Game.hub[NW_VEHICLE_HINT_SEEN_HUB];
    return (u8)h->pad1 | (u32)(u8)h->pad2 << 8;
}

static void nw_vehicle_hints_mark(int k)
{
    u32 seen = nw_vehicle_hints_seen() | 1u << k;
    Game.hub[NW_VEHICLE_HINT_SEEN_HUB].pad1 = (char)(seen & 0xFF);
    Game.hub[NW_VEHICLE_HINT_SEEN_HUB].pad2 = (char)(seen >> 8);
}

static int nw_vehicle_hints_ride(void)
{
    if (!PLAYERCOUNT || !player || Demo || player->obj.finished) {
        return -1;
    }
    if (VEHICLECONTROL == 2) {
        return NW_VEHICLE_HINT_SWIM;
    }
    if (VEHICLECONTROL == 1 && player->obj.vehicle != -1 && vtog_time == vtog_duration) {
        return player->obj.vehicle;
    }
    return -1;
}

static f32 nw_vehicle_hints_scale(const char *s)
{
    char c = s[SDL_strlen(s) - 1];
    return c >= 'a' && c <= 'z' ? 1.0f : NW_VEHICLE_HINT_NOTE;
}

static f32 nw_vehicle_hints_width(const char *s)
{
    f32 w = 0.0f;
    for (const char *c = s; *c; c++) {
        w += *c >= 'a' && *c <= 'z' ? 2.0f : *c == ':' || *c == '.' ? 0.5f : 1.0f;
    }
    return w * nw_vehicle_hints_scale(s);
}

void nw_vehicle_hints_draw(void)
{
    if (!nw_profile.vehicle_hints || Paused) {
        return;
    }
    int ride = nw_vehicle_hints_ride();
    if (ride != nw_vehicle_hint.ride) {
        nw_vehicle_hint.ride = ride;
        nw_vehicle_hint.frames = 0;
    }
    int total = NW_VEHICLE_HINT_SECONDS * NW_HZ(50, 60);
    int k = nw_vehicle_hints_find(ride);
    if (k < 0 || Cursor.menu != -1 || (nw_vehicle_hints_seen() >> k & 1)) {
        return;
    }
    char lines[NW_VEHICLE_HINT_LINES][NW_VEHICLE_HINT_TEXT];
    int n = nw_vehicle_hints_lines(ride, lines);
    if (!n) {
        return;
    }
    NW_DEVIATION(vehicle_hints);
    int frames = nw_vehicle_hint.frames++;
    if (nw_vehicle_hint.frames >= total) {
        nw_vehicle_hints_mark(k);
    }
    f32 w = 0.0f;
    for (int i = 0; i < n; i++) {
        w = SDL_max(w, nw_vehicle_hints_width(lines[i]));
    }
    f32 size = SDL_min(NW_VEHICLE_HINT_SIZE, NW_VEHICLE_HINT_WIDTH / (w * 0.1f));
    f32 t = SDL_min(frames, total - 1 - frames) / (f32)(NW_HZ(50, 60) / 4);
    t = SDL_clamp(t, 0.0f, 1.0f);
    f32 x = NW_VEHICLE_HINT_RIGHT + (1.0f - t * t * (3.0f - 2.0f * t)) * (w * size * 0.1f + 0.1f);
    f32 y = NW_VEHICLE_HINT_BOTTOM;
    for (int i = n - 1; i >= 0; i--) {
        f32 k = nw_vehicle_hints_scale(lines[i]), s = size * k;
        Text3D(lines[i], x, y, 1.0f, s, s, s, 8, k < 1.0f ? 2 : 0);
        if (i) {
            y += NW_VEHICLE_HINT_STEP * (k + nw_vehicle_hints_scale(lines[i - 1])) * 0.5f;
        }
    }
    nw_subtitles_hud_line(y);
}

void nw_vehicle_hints_reset(void)
{
    nw_vehicle_hint.ride = -1;
    nw_vehicle_hint.frames = 0;
}
