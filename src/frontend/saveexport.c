#include "frontend/saveexport.h"

#include <stdbool.h>
#include <stdint.h>

#include "frontend/debug.h"
#include "port/saves.h"

#define NW_SAVEEXPORT_ICON "cdrom0:\\STUFF\\CRASH.ICO;1"
#define NW_SAVEEXPORT_CHOICES (NW_SAVE_GAMES + 2) /* keep, empty, then the saves */
#define NW_SAVEEXPORT_LABEL 96

static const SDL_DialogFileFilter nw_saveexport_filters[] = {
    { "PCSX2 memory cards (.ps2)", "ps2" },
    { "All files", "*" },
};

static struct {
    SDL_Window *window;
    char *dir;
    const NwVfs *vfs;
    bool started;
    uint64_t drawn; /* SDL_GetTicks of the panel's last frame: a gap means it was opened again */
    /* the saves there, by number */
    int count;
    int numbers[NW_SAVE_GAMES];
    char saves[NW_SAVE_GAMES][NW_SAVEEXPORT_LABEL];
    /* the card */
    char *card;
    NwSaveCard state;
    uint8_t games[NW_SAVE_PS2_GAMES * NW_SAVE_GAME_SIZE];
    char why[512];
    /* each PS2 slot's choice: 0 keep, 1 empty, 2 on the saves */
    int choice[NW_SAVE_PS2_GAMES];
    char keep[NW_SAVE_PS2_GAMES][NW_SAVEEXPORT_LABEL];
    const char *items[NW_SAVE_PS2_GAMES][NW_SAVEEXPORT_CHOICES];
    char result[768];
    bool result_ok;
} nw_saveexport;

static void *nw_saveexport_picked_path; /* from the dialog's thread */

void *nw_saveexport_icon(const NwVfs *vfs, size_t *len)
{
    NwVfsFile *f = vfs ? nw_vfs_open(vfs, NW_SAVEEXPORT_ICON, NW_VFS_READ) : NULL;
    int64_t n = f ? nw_vfs_size(f) : -1;
    void *data = n > 0 && n < 0x100000 ? SDL_malloc((size_t)n) : NULL;
    bool ok = data && nw_vfs_read(f, data, (size_t)n) == n;
    if (f) {
        nw_vfs_close(f);
    }
    if (!ok) {
        SDL_free(data);
        SDL_SetError("no STUFF/CRASH.ICO on the imported disc");
        return NULL;
    }
    *len = (size_t)n;
    return data;
}

void nw_saveexport_game(const uint8_t *game, char *out, size_t size)
{
    char name[NW_SAVE_NAME_SIZE];
    SDL_memcpy(name, game + NW_SAVE_NAME, sizeof(name));
    name[sizeof(name) - 1] = '\0';
    for (size_t len = SDL_strlen(name); len && name[len - 1] == ' '; len--) {
        name[len - 1] = '\0';
    }
    SDL_snprintf(out, size, "%s  %d%%", name, game[NW_SAVE_PERCENT]);
}

/* The saves of the folder, as the slots list them. */
static void nw_saveexport_scan(void)
{
    const NwSaveRegistry *reg = nw_saves_levels();
    nw_saveexport.count = 0;
    for (int n = 0; reg && nw_saveexport.dir && n < NW_SAVE_GAMES; n++) {
        uint8_t game[NW_SAVE_GAME_SIZE];
        NwSaveNames names;
        NwSaveTime t;
        if (!nw_save_exists(nw_saveexport.dir, n)) {
            continue;
        }
        NwSaveResult r = nw_save_load(nw_saveexport.dir, n, reg, game, &names, &t);
        if (r != NW_SAVE_OK && r != NW_SAVE_BACKUP) {
            continue;
        }
        char what[48], longer[32] = "";
        nw_saveexport_game(game, what, sizeof(what));
        if (names.game[0]) {
            SDL_snprintf(longer, sizeof(longer), " (%s)", names.game);
        }
        int i = nw_saveexport.count++;
        nw_saveexport.numbers[i] = n;
        SDL_snprintf(nw_saveexport.saves[i], NW_SAVEEXPORT_LABEL, "Save %d: %s%s  %04d-%02d-%02d %02d:%02d", n + 1,
                     what, longer, t.year, t.month, t.day, t.hour, t.minute);
    }
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        nw_saveexport.choice[s] = SDL_min(nw_saveexport.choice[s], nw_saveexport.count + 1);
    }
}

/* What the card holds, and what keeping each of its slots means. */
static void nw_saveexport_read_card(void)
{
    const NwSaveRegistry *reg = nw_saves_levels();
    nw_saveexport.why[0] = '\0';
    nw_saveexport.state = reg ? nw_save_card(nw_saveexport.card, reg, nw_saveexport.games) : NW_SAVE_CARD_FAILED;
    if (nw_saveexport.state == NW_SAVE_CARD_FAILED || nw_saveexport.state == NW_SAVE_CARD_BLANK) {
        SDL_strlcpy(nw_saveexport.why, SDL_GetError(), sizeof(nw_saveexport.why));
    }
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        const uint8_t *game = nw_saveexport.games + s * NW_SAVE_GAME_SIZE;
        char what[48];
        if (nw_saveexport.state == NW_SAVE_CARD_SAVE && !game[NW_SAVE_EMPTY]) {
            nw_saveexport_game(game, what, sizeof(what));
            SDL_snprintf(nw_saveexport.keep[s], NW_SAVEEXPORT_LABEL, "Keep the card's: %s", what);
        } else {
            SDL_strlcpy(nw_saveexport.keep[s], "Keep the card's: empty", NW_SAVEEXPORT_LABEL);
        }
    }
}

static void SDLCALL nw_saveexport_picked(void *user, const char *const *files, int filter)
{
    (void)user;
    (void)filter;
    if (files && files[0]) {
        SDL_free(SDL_SetAtomicPointer(&nw_saveexport_picked_path, SDL_strdup(files[0])));
    }
}

char *nw_saveexport_cards_folder(void)
{
    const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    char *path = NULL;
    if (!docs || SDL_asprintf(&path, "%sPCSX2/memcards/", docs) < 0) {
        return NULL;
    }
    if (!SDL_GetPathInfo(path, NULL)) {
        SDL_free(path);
        return NULL;
    }
    return path;
}

static void nw_saveexport_export(void)
{
    int picks[NW_SAVE_PS2_GAMES];
    size_t icon_len = 0;
    void *icon = nw_saveexport_icon(nw_saveexport.vfs, &icon_len); /* NULL: only a card with the icon takes it */
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        int c = nw_saveexport.choice[s];
        picks[s] = c == 0 ? NW_SAVE_PS2_KEEP : c == 1 ? NW_SAVE_PS2_EMPTY : nw_saveexport.numbers[c - 2];
    }
    const NwSaveRegistry *reg = nw_saves_levels();
    nw_saveexport.result_ok =
        reg && nw_save_export(nw_saveexport.dir, reg, nw_saveexport.card, picks, icon, icon_len);
    if (nw_saveexport.result_ok) {
        SDL_snprintf(nw_saveexport.result, sizeof(nw_saveexport.result),
                     "Exported. The card as it was before is %s.bak.", nw_saveexport.card);
        SDL_Log("saves: exported to %s", nw_saveexport.card);
        SDL_zeroa(nw_saveexport.choice);
    } else {
        SDL_snprintf(nw_saveexport.result, sizeof(nw_saveexport.result), "Not exported: %s", SDL_GetError());
    }
    SDL_free(icon);
    nw_saveexport_read_card();
}

static void nw_saveexport_draw(void *user)
{
    (void)user;
    uint64_t now = SDL_GetTicks();
    if (!nw_saveexport.drawn || now - nw_saveexport.drawn > 500) {
        nw_saveexport_scan();
        if (nw_saveexport.card) {
            nw_saveexport_read_card();
        }
    }
    nw_saveexport.drawn = now;
    char *picked = SDL_SetAtomicPointer(&nw_saveexport_picked_path, NULL);
    if (picked) {
        SDL_free(nw_saveexport.card);
        nw_saveexport.card = picked;
        nw_saveexport.result[0] = '\0';
        SDL_zeroa(nw_saveexport.choice);
        nw_saveexport_read_card();
    }

    nw_debug_text("Puts saves into the game's PS2 save on a PCSX2 memory card (.ps2), one for each of its 4 slots. "
                  "Longer names go as the game's own (8 letters, 3 for times).");
    if (nw_debug_button("Choose card...")) {
        char *folder = nw_saveexport_cards_folder();
        SDL_ShowOpenFileDialog(nw_saveexport_picked, NULL, nw_saveexport.window, nw_saveexport_filters,
                               (int)SDL_arraysize(nw_saveexport_filters), folder, false);
        SDL_free(folder);
    }
    nw_debug_same_line(0.0f);
    nw_debug_label(nw_saveexport.card ? nw_saveexport.card : "no card chosen");
    if (!nw_saveexport.card) {
        return;
    }
    switch (nw_saveexport.state) {
    case NW_SAVE_CARD_BLANK:
        nw_debug_text("This card is not formatted. Format it in PCSX2 first (the PS2 browser, or let the game format "
                      "it when it saves), then choose it again.");
        return;
    case NW_SAVE_CARD_FAILED:
        nw_debug_text("%s", nw_saveexport.why);
        nw_debug_text("A slot that keeps the card's game cannot be exported to this card.");
        break;
    case NW_SAVE_CARD_NONE:
        nw_debug_text("The card has no save of the game: one is made.");
        break;
    case NW_SAVE_CARD_SAVE:
        nw_debug_text("The card has the game's save: the slots below replace its games.");
        break;
    }
    nw_debug_heading("PS2 slots");
    for (int s = 0; s < NW_SAVE_PS2_GAMES; s++) {
        char label[16];
        nw_saveexport.items[s][0] = nw_saveexport.keep[s];
        nw_saveexport.items[s][1] = "Empty";
        for (int i = 0; i < nw_saveexport.count; i++) {
            nw_saveexport.items[s][i + 2] = nw_saveexport.saves[i];
        }
        SDL_snprintf(label, sizeof(label), "Slot %d", s + 1);
        if (nw_debug_combo(label, &nw_saveexport.choice[s], nw_saveexport.items[s], nw_saveexport.count + 2)) {
            nw_saveexport.result[0] = '\0';
        }
    }
    if (!nw_saveexport.count) {
        nw_debug_text("No saves to export yet.");
    }
    nw_debug_text("Close PCSX2 before exporting: it keeps the card open.");
    if (nw_debug_button("Export")) {
        nw_saveexport_export();
    }
    if (nw_saveexport.result[0]) {
        nw_debug_text("%s", nw_saveexport.result);
    }
}

void nw_saveexport_start(SDL_Window *window, const char *dir, const NwVfs *vfs)
{
    nw_saveexport_stop();
    nw_saveexport.window = window;
    nw_saveexport.dir = dir ? SDL_strdup(dir) : NULL;
    nw_saveexport.vfs = vfs;
    nw_saveexport.started = true;
    nw_debug_panel("Saves", "Export to PS2 card", nw_saveexport_draw, NULL, false);
}

void nw_saveexport_stop(void)
{
    if (!nw_saveexport.started) {
        return;
    }
    SDL_free(nw_saveexport.dir);
    SDL_free(nw_saveexport.card);
    SDL_free(SDL_SetAtomicPointer(&nw_saveexport_picked_path, NULL));
    SDL_zero(nw_saveexport);
}
