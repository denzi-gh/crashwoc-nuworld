#include "frontend/presence.h"

#include <SDL3/SDL.h>

#define NW_LEVEL_SAVED 35 /* levels with a Game.level record and a hub preview */
#define NW_LEVEL_TITLES 0x23
#define NW_LEVEL_AIRSHIP 0x24
#define NW_LEVEL_HUB 0x25
#define NW_LEVEL_INTRO 0x27
#define NW_LEVEL_SPACE 0x28
#define NW_LEVEL_OUTRO 0x29
#define NW_LEVEL_TEST 0x2A

#define NW_DOT " \xC2\xB7 "

typedef enum NwScreen { NW_SCREEN_MENU, NW_SCREEN_MOVIE, NW_SCREEN_HUB, NW_SCREEN_LEVEL } NwScreen;

/* LData.vehicle: CData indices. */
static const struct {
    int id;
    const char *name;
} nw_presence_vehicles[] = {
    { 0x20, "Submarine" }, { 0x36, "Plane" },    { 0x3B, "Gyrocopter" },    { 0x44, "Mech" },
    { 0x53, "Atlasphere" }, { 0x6B, "Scooter" }, { 0x81, "Space Fighter" }, { 0x89, "Minecart" },
    { 0x8B, "Firefly" },   { 0x99, "Jeep" },     { 0xA0, "Snowboard" },     { 0xA1, "Mine Tub" },
    { 0xB2, "Fire Engine" },
};

void nw_presence_title(const char *upper, char *out, size_t size)
{
    static const char *const small[] = { "AND", "THE", "OF", "IN", "TO" };
    size_t n = 0;
    if (!size) {
        return;
    }
    for (const char *p = upper; *p;) {
        const char *end = p;
        while (*end && *end != ' ') {
            end++;
        }
        size_t len = (size_t)(end - p);
        bool lower = false;
        for (size_t i = 0; i < SDL_arraysize(small) && p != upper; i++) {
            lower |= len == SDL_strlen(small[i]) && SDL_strncmp(p, small[i], len) == 0;
        }
        for (size_t i = 0; i < len && n + 1 < size; i++) {
            out[n++] = i == 0 && !lower ? p[i] : (char)SDL_tolower((unsigned char)p[i]);
        }
        for (; *end == ' '; end++) {
            if (n + 1 < size) {
                out[n++] = ' ';
            }
        }
        p = end;
    }
    out[n] = '\0';
}

/* part after a dot unless out is empty. */
static void nw_presence_add(char *out, size_t size, const char *part)
{
    if (out[0]) {
        SDL_strlcat(out, NW_DOT, size);
    }
    SDL_strlcat(out, part, size);
}

static void nw_presence_count(char *out, size_t size, int n, const char *one, const char *many)
{
    char part[32];
    if (n > 0) {
        SDL_snprintf(part, sizeof(part), "%d %s", n, n == 1 ? one : many);
        nw_presence_add(out, size, part);
    }
}

static const char *nw_presence_menu(int menu)
{
    if (menu == 1 || menu == 2) {
        return "New Game";
    }
    if (menu == 15) {
        return "New Record";
    }
    if (menu == 19) {
        return "Save Point";
    }
    if (menu >= 20 && menu <= 22) {
        return "Loading a Save";
    }
    if ((menu >= 23 && menu <= 25) || menu == 29 || menu == 30) {
        return "Saving";
    }
    if (menu >= 26 && menu <= 28) {
        return "Deleting a Save";
    }
    return NULL;
}

static void nw_presence_state(const NwPresenceSnap *s, NwScreen screen, char *out, size_t size)
{
    const char *menu = nw_presence_menu(s->menu);
    bool saved = s->level >= 0 && s->level < NW_LEVEL_SAVED;
    char part[32];
    if (s->minigame) {
        SDL_snprintf(out, size, "Minigame" NW_DOT "%d Wumpa", s->minigame_score);
        if (s->minigame_best > 0) {
            SDL_snprintf(part, sizeof(part), "Best %d", s->minigame_best);
            nw_presence_add(out, size, part);
        }
    } else if (s->loading) {
        SDL_strlcpy(out, "Loading", size);
    } else if (screen == NW_SCREEN_MOVIE) {
        if (s->level == NW_LEVEL_INTRO || s->cutmovie == 1 || s->cutmovie == 2) {
            SDL_strlcpy(out, "Intro", size);
        } else if (s->level == NW_LEVEL_OUTRO || s->cutmovie >= 3) {
            SDL_strlcpy(out, "Ending", size);
        }
    } else if (s->game_mode == 1) {
        SDL_strlcpy(out, "Cutscene", size);
    } else if (menu) {
        SDL_strlcpy(out, menu, size);
    } else if (screen == NW_SCREEN_MENU) {
        /* the title menu itself: no state */
    } else if (s->paused && screen == NW_SCREEN_LEVEL) {
        SDL_strlcpy(out, "Paused", size);
    } else if (s->time_trial) {
        SDL_strlcpy(out, "Time Trial", size);
    } else if (s->bonus >= 1 && s->bonus <= 3) {
        SDL_strlcpy(out, "Bonus Round", size);
    } else if (saved && (s->level_flags & 2)) {
        SDL_strlcpy(out, "Boss Fight", size);
    } else if (screen == NW_SCREEN_HUB) {
        if (s->percent > 0) {
            SDL_snprintf(out, size, "%d%%", s->percent);
        }
        nw_presence_count(out, size, s->crystals, "Crystal", "Crystals");
        nw_presence_count(out, size, s->gems, "Gem", "Gems");
        nw_presence_count(out, size, s->relics, "Relic", "Relics");
    } else if (saved) {
        if (s->collected & 0x8) {
            nw_presence_add(out, size, "Crystal");
        }
        int gems = 0;
        for (unsigned bit = 0x10; bit <= 0x400; bit <<= 1) {
            gems += (s->collected & bit) != 0;
        }
        nw_presence_count(out, size, gems, "Gem", "Gems");
        if (s->collected & 0x7) {
            nw_presence_add(out, size, s->collected & 0x4   ? "Platinum Relic"
                                       : s->collected & 0x2 ? "Gold Relic"
                                                            : "Sapphire Relic");
        }
    } else if (s->level == NW_LEVEL_AIRSHIP || s->level == NW_LEVEL_TEST) {
        SDL_strlcpy(out, "Unused Level", size);
    }
}

void nw_presence_activity(const NwPresenceSnap *s, const char *version, int64_t start_ms, NwActivity *a)
{
    SDL_zerop(a);
    a->start_ms = start_ms;
    SDL_strlcpy(a->large_image, "nuworld", sizeof(a->large_image));
    SDL_snprintf(a->large_text, sizeof(a->large_text), "NuWorld %s", version);
    SDL_strlcpy(a->buttons[0].label, "Get NuWorld", sizeof(a->buttons[0].label));
    SDL_strlcpy(a->buttons[0].url, "https://github.com/denzi-gh/crashwoc-nuworld", sizeof(a->buttons[0].url));
    SDL_strlcpy(a->buttons[1].label, "Join the Discord", sizeof(a->buttons[1].label));
    SDL_strlcpy(a->buttons[1].url, "https://discord.gg/kmCPpW4KvJ", sizeof(a->buttons[1].url));
    a->nbuttons = 2;
    if (!s->valid) {
        SDL_strlcpy(a->details, "Starting Up", sizeof(a->details));
        return;
    }
    char name[NW_PRESENCE_NAME], room[NW_PRESENCE_NAME], warp[NW_PRESENCE_NAME];
    nw_presence_title(s->level_name, name, sizeof(name));
    nw_presence_title(s->warp_room[0] ? s->warp_room : "WARP ROOM", warp, sizeof(warp));
    nw_presence_title(s->room_name, room, sizeof(room));
    if (!name[0]) {
        SDL_snprintf(name, sizeof(name), "Level %d", s->level);
    }
    bool saved = s->level >= 0 && s->level < NW_LEVEL_SAVED;
    if (s->demo) {
        SDL_strlcpy(a->details, "Main Menu", sizeof(a->details));
        SDL_snprintf(a->state, sizeof(a->state), "Demo: %s", name);
        if (saved) {
            SDL_snprintf(a->large_image, sizeof(a->large_image), "level_%02d", s->level);
        }
        return;
    }
    NwScreen screen = s->cutmovie == 0 || s->level == NW_LEVEL_TITLES ? NW_SCREEN_MENU
                      : s->cutmovie > 0 || s->level == NW_LEVEL_INTRO || s->level == NW_LEVEL_SPACE ||
                              s->level == NW_LEVEL_OUTRO
                          ? NW_SCREEN_MOVIE
                      : s->level == NW_LEVEL_HUB ? NW_SCREEN_HUB
                                                 : NW_SCREEN_LEVEL;
    bool playable = screen == NW_SCREEN_HUB;
    if (screen == NW_SCREEN_MENU) {
        SDL_strlcpy(a->details, "Main Menu", sizeof(a->details));
    } else if (screen == NW_SCREEN_MOVIE) {
        SDL_strlcpy(a->details, "Cutscene", sizeof(a->details));
    } else if (screen == NW_SCREEN_HUB && s->hub >= 0 && s->hub < 6 && room[0]) {
        SDL_snprintf(a->details, sizeof(a->details), "%s: %s", warp, room);
        SDL_snprintf(a->large_image, sizeof(a->large_image), "room_%d", s->hub);
    } else if (screen == NW_SCREEN_HUB) {
        SDL_strlcpy(a->details, warp, sizeof(a->details));
        SDL_strlcpy(a->large_image, "warproom", sizeof(a->large_image));
    } else {
        SDL_strlcpy(a->details, name, sizeof(a->details));
        if (saved) {
            SDL_snprintf(a->large_image, sizeof(a->large_image), "level_%02d", s->level);
        }
        playable = saved || s->level == NW_LEVEL_AIRSHIP || s->level == NW_LEVEL_TEST;
    }
    if (playable) {
        bool coco = s->character == 1;
        SDL_strlcpy(a->small_image, coco ? "coco" : "crash", sizeof(a->small_image));
        SDL_strlcpy(a->small_text, coco ? "Coco" : "Crash", sizeof(a->small_text));
        for (size_t i = 0; i < SDL_arraysize(nw_presence_vehicles); i++) {
            if (nw_presence_vehicles[i].id == s->vehicle) {
                nw_presence_add(a->small_text, sizeof(a->small_text), nw_presence_vehicles[i].name);
            }
        }
    }
    nw_presence_state(s, screen, a->state, sizeof(a->state));
}
