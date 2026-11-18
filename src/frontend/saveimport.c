#include "frontend/saveimport.h"

#include <stdbool.h>
#include <stdint.h>

#include "frontend/debug.h"
#include "frontend/saveexport.h"
#include "port/saves.h"

static const SDL_DialogFileFilter nw_saveimport_filters[] = {
    { "PCSX2 memory cards (.ps2)", "ps2" },
    { "All files", "*" },
};

static struct {
    SDL_Window *window;
    char *dir;
    bool started;
    uint64_t drawn;
    char *card;
    NwSaveCard state;
    uint8_t games[NW_SAVE_PS2_GAMES * NW_SAVE_GAME_SIZE];
    int already[NW_SAVE_PS2_GAMES];
    bool pick[NW_SAVE_PS2_GAMES];
    char why[512];
    NwSavesImport seen;
    char result[768];
} nw_saveimport;

static void *nw_saveimport_picked_path; /* set by the dialog's thread */

static bool nw_saveimport_empty(int s)
{
    return nw_saveimport.games[s * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] != 0;
}

static void nw_saveimport_read_card(void)
{
    const NwSaveRegistry *reg = nw_saves_levels();
    nw_saveimport.why[0] = '\0';
    nw_saveimport.state = reg ? nw_save_card(nw_saveimport.card, reg, nw_saveimport.games) : NW_SAVE_CARD_FAILED;
    if (nw_saveimport.state == NW_SAVE_CARD_FAILED || nw_saveimport.state == NW_SAVE_CARD_BLANK) {
        SDL_strlcpy(nw_saveimport.why, SDL_GetError(), sizeof(nw_saveimport.why));
    }
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        nw_saveimport.already[s] = -1;
    }
    if (nw_saveimport.state == NW_SAVE_CARD_SAVE && nw_saveimport.dir) {
        nw_save_match(nw_saveimport.dir, reg, nw_saveimport.games, NW_SAVE_PS2_GAMES, nw_saveimport.already);
    }
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        nw_saveimport.pick[s] = nw_saveimport.state == NW_SAVE_CARD_SAVE && !nw_saveimport_empty(s) &&
                                nw_saveimport.already[s] < 0;
    }
}

static void nw_saveimport_list(char *out, size_t size, const char *lead, const int *list)
{
    size_t len = SDL_strlen(out);
    bool first = true;
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        if (list[s] >= 0 && len < size) {
            len += SDL_snprintf(out + len, size - len, "%s%s slot %d as Save %d", first ? "" : ",", first ? lead : "",
                                s + 1, list[s] + 1);
            first = false;
        }
    }
    if (!first && len < size) {
        SDL_snprintf(out + len, size - len, ". ");
    }
}

static void nw_saveimport_report(NwSavesImport state, const NwSavesImported *r)
{
    char *out = nw_saveimport.result;
    out[0] = '\0';
    nw_saveimport_list(out, sizeof(nw_saveimport.result), "Imported", r->saved);
    nw_saveimport_list(out, sizeof(nw_saveimport.result), "Already saved:", r->already);
    size_t len = SDL_strlen(out);
    if (state == NW_SAVES_IMPORT_FAILED) {
        SDL_snprintf(out + len, sizeof(nw_saveimport.result) - len, "Not imported: %s", r->why);
    } else if (!len) {
        SDL_strlcpy(out, "Nothing imported.", sizeof(nw_saveimport.result));
    }
}

static void SDLCALL nw_saveimport_picked(void *user, const char *const *files, int filter)
{
    (void)user;
    (void)filter;
    if (files && files[0]) {
        SDL_free(SDL_SetAtomicPointer(&nw_saveimport_picked_path, SDL_strdup(files[0])));
    }
}

static void nw_saveimport_import(void)
{
    uint8_t games[sizeof(nw_saveimport.games)];
    SDL_memcpy(games, nw_saveimport.games, sizeof(games));
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        if (!nw_saveimport.pick[s]) {
            games[s * NW_SAVE_GAME_SIZE + NW_SAVE_EMPTY] = 1;
        }
    }
    if (nw_saves_import_games(games)) {
        nw_saveimport.seen = NW_SAVES_IMPORT_WAITING;
        nw_saveimport.result[0] = '\0';
    }
}

static void nw_saveimport_draw(void *user)
{
    (void)user;
    uint64_t now = SDL_GetTicks();
    bool read = !nw_saveimport.drawn || now - nw_saveimport.drawn > 500;
    nw_saveimport.drawn = now;
    char *picked = SDL_SetAtomicPointer(&nw_saveimport_picked_path, NULL);
    if (picked) {
        SDL_free(nw_saveimport.card);
        nw_saveimport.card = picked;
        nw_saveimport.result[0] = '\0';
        read = true;
    }
    NwSavesImported done;
    NwSavesImport state = nw_saves_import_result(&done);
    if (state != nw_saveimport.seen) {
        if (state == NW_SAVES_IMPORT_DONE || state == NW_SAVES_IMPORT_FAILED) {
            nw_saveimport_report(state, &done);
            read = true;
        }
        nw_saveimport.seen = state;
    }
    if (read && nw_saveimport.card) {
        nw_saveimport_read_card();
    }

    nw_debug_text("Copies the games of the game's PS2 save on a PCSX2 memory card (.ps2) into free saves. The card "
                  "is only read; a game already in a save is not copied again.");
    if (nw_debug_button("Choose card...")) {
        char *folder = nw_saveexport_cards_folder();
        SDL_ShowOpenFileDialog(nw_saveimport_picked, NULL, nw_saveimport.window, nw_saveimport_filters,
                               (int)SDL_arraysize(nw_saveimport_filters), folder, false);
        SDL_free(folder);
    }
    nw_debug_same_line(0.0f);
    nw_debug_label(nw_saveimport.card ? nw_saveimport.card : "no card chosen");
    if (!nw_saveimport.card) {
        return;
    }
    switch (nw_saveimport.state) {
    case NW_SAVE_CARD_BLANK:
        nw_debug_text("This card is not formatted, so it holds no saves.");
        return;
    case NW_SAVE_CARD_FAILED:
        nw_debug_text("%s", nw_saveimport.why);
        return;
    case NW_SAVE_CARD_NONE:
        nw_debug_text("The card has no save of the game.");
        return;
    case NW_SAVE_CARD_SAVE:
        break;
    }
    nw_debug_heading("PS2 slots");
    int picks = 0;
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        char what[48], label[96];
        bool importable = !nw_saveimport_empty(s) && nw_saveimport.already[s] < 0;
        if (nw_saveimport_empty(s)) {
            SDL_snprintf(label, sizeof(label), "Slot %d: empty", s + 1);
        } else {
            nw_saveexport_game(nw_saveimport.games + s * NW_SAVE_GAME_SIZE, what, sizeof(what));
            if (importable) {
                SDL_snprintf(label, sizeof(label), "Slot %d: %s", s + 1, what);
            } else {
                SDL_snprintf(label, sizeof(label), "Slot %d: %s (already Save %d)", s + 1, what,
                             nw_saveimport.already[s] + 1);
            }
        }
        nw_debug_enable_begin(importable);
        nw_debug_checkbox(label, &nw_saveimport.pick[s]);
        nw_debug_enable_end();
        picks += importable && nw_saveimport.pick[s];
    }
    nw_debug_text("Close PCSX2 first: it may not have written its latest save to the card yet.");
    bool screen = nw_saves_screen();
    bool busy = state == NW_SAVES_IMPORT_WAITING || state == NW_SAVES_IMPORT_RUNNING;
    nw_debug_enable_begin(picks && !screen && !busy);
    if (nw_debug_button("Import")) {
        nw_saveimport_import();
    }
    nw_debug_enable_end();
    if (screen) {
        nw_debug_text("Leave the load/save screen to import.");
    } else if (busy) {
        nw_debug_text("Importing...");
    }
    if (!busy && nw_saveimport.result[0]) {
        nw_debug_text("%s", nw_saveimport.result);
    }
}

void nw_saveimport_start(SDL_Window *window, const char *dir)
{
    nw_saveimport_stop();
    nw_saveimport.window = window;
    nw_saveimport.dir = dir ? SDL_strdup(dir) : NULL;
    nw_saveimport.started = true;
    nw_debug_panel("Saves", "Import from PS2 card", nw_saveimport_draw, NULL, false);
}

void nw_saveimport_stop(void)
{
    if (!nw_saveimport.started) {
        return;
    }
    SDL_free(nw_saveimport.dir);
    SDL_free(nw_saveimport.card);
    SDL_free(SDL_SetAtomicPointer(&nw_saveimport_picked_path, NULL));
    SDL_zero(nw_saveimport);
}
