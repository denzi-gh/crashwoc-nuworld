#include "frontend/cheatpanel.h"

#include <SDL3/SDL.h>

#include "frontend/debug.h"
#include "gameplay/cheats.h"
#include "port/level.h"

#define NW_CHEAT_PANEL_ROWS 14
#define NW_CHEAT_PANEL_WIDTH 28.0f

typedef struct NwCheatChange {
    uint16_t mask[NW_CHEATS_LEVELS], value[NW_CHEATS_LEVELS];
    uint8_t power_mask, power_value;
} NwCheatChange;

static const char *const nw_cheat_panel_powers[NW_CHEATS_POWERS] = {
    "Sneak Shoes", "Double Jump", "Death Tornado Spin", "Fruit Bazooka", "Speed Shoes", "Super Belly Flop",
};

static const struct {
    uint16_t item;
    const char *label, *name;
} nw_cheat_panel_items[] = {
    { NW_CHEAT_BOSS, "Beaten", "" },
    { NW_CHEAT_CRYSTAL, "Crystal", "crystal" },
    { NW_CHEAT_CRATE_GEM, "Crate gem", "crate gem" },
    { NW_CHEAT_BONUS_GEM, "Bonus gem", "bonus gem" },
    { NW_CHEAT_RED_GEM, "Red gem", "red gem" },
    { NW_CHEAT_GREEN_GEM, "Green gem", "green gem" },
    { NW_CHEAT_BLUE_GEM, "Blue gem", "blue gem" },
    { NW_CHEAT_YELLOW_GEM, "Yellow gem", "yellow gem" },
    { NW_CHEAT_PURPLE_GEM, "Purple gem", "purple gem" },
};

static const char *const nw_cheat_panel_relics[] = { "None", "Sapphire", "Gold", "Platinum" };
static const char *const nw_cheat_panel_relic_names[] = { "", "sapphire", "gold", "platinum" };
static const uint16_t nw_cheat_panel_tiers[] = { 0, NW_CHEAT_SAPPHIRE, NW_CHEAT_GOLD, NW_CHEAT_PLATINUM };

static struct {
    char filter[64];
    int opened;
    bool quiet, ask_quiet;
    NwCheatChange ask;
    char what[96];
} nw_cheat_panel = { .opened = -1 };

static void nw_cheat_panel_send(const NwCheatChange *c)
{
    for (int l = 0; l < NW_CHEATS_LEVELS; l++) {
        if (c->mask[l]) {
            nw_cheats_level(l, c->mask[l], c->value[l]);
        }
    }
    if (c->power_mask) {
        nw_cheats_powers(c->power_mask, c->power_value);
    }
}

static bool nw_cheat_panel_prompt(void *user)
{
    (void)user;
    bool open = true;
    nw_debug_text("Remove %s?", nw_cheat_panel.what);
    nw_debug_text("Removed items can be earned again.");
    nw_debug_warning("This may break progression flags: warp rooms stay open, gem paths and cutscenes stay as they "
                     "are.");
    nw_debug_checkbox("Don't ask again until NuWorld closes", &nw_cheat_panel.ask_quiet);
    if (nw_debug_button("Remove")) {
        nw_cheat_panel_send(&nw_cheat_panel.ask);
        nw_cheat_panel.quiet = nw_cheat_panel.ask_quiet;
        open = false;
    }
    nw_debug_same_line(0.0f);
    if (nw_debug_button("Cancel")) {
        open = false;
    }
    return open;
}

static void nw_cheat_panel_change(const NwCheatsView *v, const NwCheatChange *c, const char *what)
{
    bool removes = (v->powers & c->power_mask & ~c->power_value) != 0;
    for (int l = 0; l < NW_CHEATS_LEVELS && !removes; l++) {
        removes = (v->flags[l] & c->mask[l] & ~c->value[l]) != 0;
    }
    if (!removes || nw_cheat_panel.quiet) {
        nw_cheat_panel_send(c);
        return;
    }
    nw_cheat_panel.ask = *c;
    nw_cheat_panel.ask_quiet = false;
    SDL_strlcpy(nw_cheat_panel.what, what, sizeof(nw_cheat_panel.what));
    nw_debug_prompt("Remove progress", nw_cheat_panel_prompt, NULL);
}

static void nw_cheat_panel_level(const NwCheatsView *v, int level, uint16_t mask, uint16_t value, const char *what)
{
    NwCheatChange c = { 0 };
    c.mask[level] = mask;
    c.value[level] = value;
    nw_cheat_panel_change(v, &c, what);
}

static void nw_cheat_panel_levels(const NwCheatsView *v, bool on, const char *what)
{
    NwCheatChange c = { 0 };
    for (int l = 0; l < NW_CHEATS_LEVELS; l++) {
        c.mask[l] = v->items[l];
        c.value[l] = on ? v->items[l] : 0;
    }
    nw_cheat_panel_change(v, &c, what);
}

static int nw_cheat_panel_tier(uint16_t flags)
{
    return flags & 4 ? 3 : flags & 2 ? 2 : flags & 1 ? 1 : 0;
}

static void nw_cheat_panel_note(uint16_t items, uint16_t flags, char *out, size_t size)
{
    if (items & NW_CHEAT_BOSS) {
        SDL_strlcpy(out, flags & NW_CHEAT_BOSS ? "beaten" : "", size);
        return;
    }
    int have = 0, total = 1, tier = nw_cheat_panel_tier(flags);
    for (size_t i = 0; i < SDL_arraysize(nw_cheat_panel_items); i++) {
        if (items & nw_cheat_panel_items[i].item) {
            total++;
            have += (flags & nw_cheat_panel_items[i].item) != 0;
        }
    }
    have += tier != 0;
    if (tier) {
        SDL_snprintf(out, size, "%d/%d  %s", have, total, nw_cheat_panel_relics[tier]);
    } else {
        SDL_snprintf(out, size, "%d/%d", have, total);
    }
}

static void nw_cheat_panel_items_draw(const NwCheatsView *v, int level, const char *name)
{
    uint16_t items = v->items[level], flags = v->flags[level];
    char what[96];
    for (size_t i = 0; i < SDL_arraysize(nw_cheat_panel_items); i++) {
        uint16_t item = nw_cheat_panel_items[i].item;
        if (!(items & item)) {
            continue;
        }
        bool on = (flags & item) != 0;
        if (nw_debug_checkbox(nw_cheat_panel_items[i].label, &on)) {
            if (item == NW_CHEAT_BOSS) {
                SDL_snprintf(what, sizeof(what), "the win over %s", name);
            } else {
                SDL_snprintf(what, sizeof(what), "the %s from %s", nw_cheat_panel_items[i].name, name);
            }
            nw_cheat_panel_level(v, level, item, on ? item : 0, what);
        }
        if (item == NW_CHEAT_BOSS) {
            nw_debug_tooltip("Not beaten: the boss can be fought again and hands out its power again; the next warp "
                             "room stays open");
        }
    }
    if (items & NW_CHEAT_RELIC) {
        int was = nw_cheat_panel_tier(flags), tier = was;
        if (nw_debug_combo("Relic", &tier, nw_cheat_panel_relics, (int)SDL_arraysize(nw_cheat_panel_relics))) {
            SDL_snprintf(what, sizeof(what), "the %s relic from %s", nw_cheat_panel_relic_names[was], name);
            nw_cheat_panel_level(v, level, NW_CHEAT_RELIC, nw_cheat_panel_tiers[tier], what);
        }
    }
    if (!(items & NW_CHEAT_BOSS)) {
        if (nw_debug_button("All")) {
            nw_cheat_panel_level(v, level, items, items, "");
        }
        nw_debug_same_line(0.0f);
        if (nw_debug_button("None")) {
            SDL_snprintf(what, sizeof(what), "everything from %s", name);
            nw_cheat_panel_level(v, level, items, 0, what);
        }
    }
}

static void nw_cheat_panel_list(const NwCheatsView *v)
{
    nw_debug_filter("filter levels", nw_cheat_panel.filter, sizeof(nw_cheat_panel.filter));
    nw_debug_list_sized("cheat levels", NW_CHEAT_PANEL_ROWS, NW_CHEAT_PANEL_WIDTH);
    for (int i = 0, shown = -1; i < nw_level_count(); i++) {
        int room, slot, level = nw_level_at(i, &room, &slot);
        if (level < 0 || level >= NW_CHEATS_LEVELS || !room || !v->items[level]) {
            continue;
        }
        const char *name = nw_level_name(level);
        if (!name || !SDL_strcasestr(name, nw_cheat_panel.filter)) {
            continue;
        }
        char label[80], note[32];
        if (room != shown) {
            SDL_snprintf(label, sizeof(label), "Warp Room %d", room);
            nw_debug_heading(label);
            shown = room;
        }
        if (v->items[level] & NW_CHEAT_BOSS) {
            SDL_snprintf(label, sizeof(label), "Boss: %s###%d", name, level);
        } else {
            SDL_snprintf(label, sizeof(label), "%d. %s###%d", slot + 1, name, level);
        }
        nw_cheat_panel_note(v->items[level], v->flags[level], note, sizeof(note));
        bool here = level == v->level;
        if (nw_debug_tree(label, note, here, here && nw_cheat_panel.opened != level)) {
            nw_cheat_panel_items_draw(v, level, name);
            nw_debug_tree_end();
        }
    }
    nw_debug_list_end();
    nw_cheat_panel.opened = v->level;
}

static void nw_cheat_panel_draw(void *user)
{
    (void)user;
    NwCheatsView v;
    nw_cheats_view(&v);
    if (!v.valid) {
        nw_debug_text("Cheats work once the game runs, not during a demo.");
        return;
    }
    bool invincible = v.invincible;
    if (nw_debug_checkbox("Invincibility", &invincible)) {
        nw_cheats_invincible(invincible);
    }
    nw_debug_tooltip("The PS2 debug menu's cheat: nothing can kill Crash, INVINCIBLE blinks in levels");
    nw_debug_heading("Powers");
    for (int i = 0; i < NW_CHEATS_POWERS; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        bool on = (v.powers & bit) != 0;
        if (nw_debug_checkbox(nw_cheat_panel_powers[i], &on)) {
            NwCheatChange c = { .power_mask = bit, .power_value = on ? bit : 0 };
            nw_cheat_panel_change(&v, &c, nw_cheat_panel_powers[i]);
        }
        nw_debug_tooltip("A removed power is handed out again by its boss once that boss is not beaten");
    }
    NwCheatChange powers = { .power_mask = (uint8_t)((1u << NW_CHEATS_POWERS) - 1) };
    if (nw_debug_button("All##powers")) {
        powers.power_value = powers.power_mask;
        nw_cheat_panel_change(&v, &powers, "");
    }
    nw_debug_same_line(0.0f);
    if (nw_debug_button("None##powers")) {
        nw_cheat_panel_change(&v, &powers, "every power");
    }
    nw_debug_heading("Levels");
    nw_debug_text("%d%%   crystals %d/25   gems %d/46   relics %d/30", v.percent, v.crystals, v.gems, v.relics);
    nw_debug_warning("Removing an item makes it earnable again and may break progression flags.");
    if (nw_debug_button("All##levels")) {
        nw_cheat_panel_levels(&v, true, "");
    }
    nw_debug_same_line(0.0f);
    if (nw_debug_button("None##levels")) {
        nw_cheat_panel_levels(&v, false, "every crystal, gem, relic and boss win");
    }
    nw_cheat_panel_list(&v);
}

void nw_cheat_panel_start(void)
{
    nw_debug_panel("Gameplay", "Cheats", nw_cheat_panel_draw, NULL, false);
}
