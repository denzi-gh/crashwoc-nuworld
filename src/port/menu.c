/* Native title/pause menus, save confirmation, autosave consent and notifications. */
#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "game/game.retail.h"
#include "nu_types.h"
#include "platform/buildid.h"
#include "platform/update.h"
#include "port/font3d.h"
#include "port/menu.h"
#include "port/nameentry.h"
#include "port/savemenu.h"
#include "port/saves.h"
#include "port/autosave.h"
#include "port/names.h"
#include "gameplay/gameplay.h"

extern s32 editor_active, GameMode, pause_dir, LANGUAGEOPTION;
extern struct game_s Game;
extern char tbuf[128];
extern char *tNEWGAME[6], *tLOADGAME[6], *tLANGUAGE[6], *LanguageName[6];
extern char *tYES[6], *tNO[6];
extern char *tRESUME[6], *tOPTIONS[6], *tQUIT[6], *tWARPROOM[6], *tRESTARTTRIAL[6];
extern s32 Level, TimeTrial;
extern s32 new_level, cutmovie, next_cut_movie, fade_rate, i_nameinput;
extern struct game_s *game;
extern char *tVIBRATION[6], *tON[6], *tOFF[6], *tSOUNDOPTIONS[6], *tADJUSTSCREEN[6], *tDONE[6];
extern u8 temp_vibration;
extern int nw_hz;
void NuStrCpy(char *dst, char *src);
void OpenGame(void);
void DrawGameSlot(struct game_s *slot, f32 x, f32 y, s32 colour);
extern f32 PANELMENUX, dme_sx, dme_symul;

void Text3D(char *txt, f32 x, f32 y, f32 z, f32 scalex, f32 scaley, f32 scalez, s32 align, s32 colour);
void DrawMenuEntry(struct cursor_s *cursor, char *txt, f32 *x, f32 *y, s32 *i);

#define NW_MENU_QUIT_TITLE 34
#define NW_MENU_QUIT_PAUSE 35
#define NW_MENU_UPDATE 40
#define NW_MENU_L2 0x0001u /* ProcMenu's pad bits */
#define NW_MENU_SFX 0x36   /* retail's confirm sound */

void NewMenu(struct cursor_s *cursor, s32 menu, s32 y, s32 level);
void GameSfx(s32 sfx, void *p);
extern struct gtimer_s GlobalTimer;

#define NW_MENUDY -0.120000005f /* game/game's static MENUDY */

static char *nw_menu_mods[6] = {
    "MODS (SOON)", "MODS (BIENTOT)", "MODS (BALD)", "MOD (PRESTO)", "MODS (PRONTO)", "MODS (BINNENKORT)",
};

static char *nw_menu_update_notice[6][2] = {
    { "NEW VERSION AVAILABLE", "PRESS u TO UPDATE" },
    { "NOUVELLE VERSION DISPONIBLE", "APPUIE SUR u POUR METTRE A JOUR" },
    { "NEUE VERSION VERFUEGBAR", "DRUECKEN SIE u ZUM AKTUALISIEREN" },
    { "NUOVA VERSIONE DISPONIBILE", "PREMI IL TASTO u PER AGGIORNARE" },
    { "NUEVA VERSION DISPONIBLE", "PULSA u PARA ACTUALIZAR" },
    { "NIEUWE VERSIE BESCHIKBAAR", "DRUK OP DE u -TOETS OM BIJ TE WERKEN" },
};

static char nw_menu_typed[NW_NAMES_GAME + 1], nw_menu_own[9];
static int nw_menu_position;
static bool nw_menu_rollback_options, nw_menu_previous_failed;
static uint32_t nw_menu_pressed;

void nw_menu_buttons(uint32_t pressed)
{
    nw_menu_pressed = pressed;
}

/* L2 on the title menu while a newer release is out, then the browser question. */
static bool nw_menu_update_proc(struct cursor_s *cursor, int confirm, int cancel)
{
    if (cursor->menu == 0 && (nw_menu_pressed & NW_MENU_L2) && cursor->item_frame > 1 && GameMode != 1 &&
        (u32)Game.language < 6 && nw_update_available()) {
        GameSfx(NW_MENU_SFX, NULL);
        NewMenu(cursor, NW_MENU_UPDATE, 0, -1);
        return true;
    }
    if (cursor->menu != NW_MENU_UPDATE) {
        return false;
    }
    if (confirm && cursor->y == 0) {
        nw_update_open();
    }
    if (confirm || cancel) {
        NewMenu(cursor, 0, -1, -1);
    }
    return true;
}

static char *nw_menu_autosave_title[6] = {
    "AUTOSAVE", "SAUVEGARDE AUTO", "AUTOMATISCH SPEICHERN", "AUTOSALVATAGGIO", "GUARDADO AUTOMATICO", "AUTOMATISCH OPSLAAN",
};
static char *nw_menu_back[6] = { "BACK", "RETOUR", "ZURUECK", "INDIETRO", "VOLVER", "TERUG" };
static char *nw_menu_previous[6] = {
    "PREVIOUS VERSIONS", "VERSIONS PRECEDENTES", "VORHERIGE VERSIONEN", "VERSIONI PRECEDENTI", "VERSIONES ANTERIORES", "VORIGE VERSIES",
};

static void nw_menu_resume_name(struct cursor_s *cursor)
{
    NewMenu(cursor, 2, -1, -1);
    SDL_memcpy(Game.name, nw_menu_own, sizeof(nw_menu_own));
    nw_nameentry_resume_game(nw_menu_typed, nw_menu_position);
    cursor->y = cursor->y_max - 1;
}

static bool nw_menu_autosave_proc(struct cursor_s *cursor, int confirm, int cancel)
{
    int menu = cursor->menu;
    if (menu == 2 && confirm && cursor->y == cursor->y_max - 1) {
        SDL_memcpy(nw_menu_own, Game.name, sizeof(nw_menu_own));
        const char *name = nw_nameentry_name();
        SDL_strlcpy(nw_menu_typed, name ? name : Game.name, sizeof(nw_menu_typed));
        nw_menu_position = i_nameinput;
        nw_nameentry_finish();
        NewMenu(cursor, NW_MENU_AUTOSAVE_NEW, 0, -1);
        return true;
    }
    if (menu == NW_MENU_AUTOSAVE_NEW) {
        if (cancel || (confirm && cursor->y == 2)) {
            nw_menu_resume_name(cursor);
        } else if (confirm) {
            for (char *p = Game.name; *p; p++) if (*p == '_') *p = ' ';
            if (SDL_strcmp(Game.name, "        ") == 0) NuStrCpy(Game.name, "CRASH   ");
            if (nw_profile.wombat && SDL_strstr(Game.name, "WOMBAT")) {
                NW_DEVIATION(wombat);
                NuStrCpy(Game.name, "CRASH   ");
                OpenGame();
            }
            bool enabled = cursor->y == 0;
            NewMenu(cursor, -1, -1, -1);
            nw_autosave_new(enabled);
            new_level = 0x25;
            if (cutmovie == 0) {
                next_cut_movie = 1;
                fade_rate = nw_hz == 60 ? 8 : 10;
            } else {
                cutmovie = 1;
            }
        }
        return true;
    }
    if (menu == 4 && confirm && cursor->y == cursor->y_max - 1) {
        NewMenu(cursor, NW_MENU_AUTOSAVE_OPTIONS, nw_autosave_enabled() ? 0 : 1, -1);
        return true;
    }
    if (menu == NW_MENU_AUTOSAVE_OPTIONS) {
        if (cancel || (confirm && cursor->y == 2)) {
            NewMenu(cursor, 4, -1, -1);
        } else if (confirm) {
            if (!nw_autosave_set(cursor->y == 0)) {
                nw_menu_rollback_options = true;
                NewMenu(cursor, NW_MENU_AUTOSAVE_ROLLBACK, 1, -1);
            } else {
                NewMenu(cursor, 4, -1, -1);
            }
        }
        return true;
    }
    if (menu == NW_MENU_AUTOSAVE_ROLLBACK) {
        if (confirm || cancel) {
            bool enabled = confirm && cursor->y == 0;
            if (!enabled) (void)nw_autosave_set(false);
            nw_autosave_accept_rollback(enabled);
            NewMenu(cursor, nw_menu_rollback_options ? 4 : -1, -1, -1);
        }
        return true;
    }
    if (menu == 21 && confirm && cursor->y == 2) {
        nw_menu_previous_failed = false;
        NewMenu(cursor, NW_MENU_SAVE_PREVIOUS, 2, -1);
        return true;
    }
    if (menu == NW_MENU_SAVE_PREVIOUS) {
        if (cancel || (confirm && cursor->y == 2)) {
            NewMenu(cursor, 21, 1, -1);
        } else if (confirm) {
            if (nw_saves_previous(game, cursor->y + 1)) {
                nw_menu_previous_failed = false;
                NewMenu(cursor, 21, 1, -1);
            } else {
                nw_menu_previous_failed = true;
            }
        }
        return true;
    }
    return false;
}

void nw_menu_autosave_loaded(struct cursor_s *cursor)
{
    if (nw_autosave_rollback()) {
        nw_menu_rollback_options = false;
        NewMenu(cursor, NW_MENU_AUTOSAVE_ROLLBACK, 1, -1);
    }
}

static const char *nw_menu_desktop(void)
{
#if defined(SDL_PLATFORM_WINDOWS)
    return "WINDOWS";
#elif defined(SDL_PLATFORM_MACOS)
    return "MACOS";
#elif defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID)
    return "LINUX";
#else
    return NULL;
#endif
}

static bool nw_menu_has_quit(struct cursor_s *cursor)
{
    return nw_menu_desktop() && (cursor->menu == 0 || cursor->menu == 3);
}

void GetMenuInfo(struct cursor_s *cursor)
{
    GetMenuInfo__retail(cursor);
    if (nw_menu_has_quit(cursor)) {
        cursor->y_max++;
    } else if (cursor->menu == 4) {
        cursor->y_max++;
    } else if (cursor->menu == 21 || cursor->menu == NW_MENU_AUTOSAVE_NEW ||
               cursor->menu == NW_MENU_AUTOSAVE_OPTIONS || cursor->menu == NW_MENU_SAVE_PREVIOUS) {
        cursor->x_min = cursor->x_max = cursor->y_min = 0;
        cursor->y_max = 2;
    } else if (cursor->menu == NW_MENU_AUTOSAVE_ROLLBACK || cursor->menu == NW_MENU_UPDATE) {
        cursor->x_min = cursor->x_max = cursor->y_min = 0;
        cursor->y_max = 1;
    } else if (cursor->menu == NW_MENU_QUIT_TITLE || cursor->menu == NW_MENU_QUIT_PAUSE) {
        cursor->y_max = 1;
    }
}

bool nw_menu_quit(struct cursor_s *cursor, int confirm, int cancel)
{
    if (nw_menu_autosave_proc(cursor, confirm, cancel)) return true;
    if (nw_menu_update_proc(cursor, confirm, cancel)) return true;
    bool title = cursor->menu == NW_MENU_QUIT_TITLE;
    if (title || cursor->menu == NW_MENU_QUIT_PAUSE) {
        if (cancel || (confirm && cursor->y == 1)) {
            NewMenu(cursor, title ? 0 : 3, -1, -1);
        } else if (confirm) {
            nw_autosave_checkpoint(false);
            /* Follow the window-close path on the main thread, including normal cleanup. */
            SDL_Event event = { .type = SDL_EVENT_QUIT };
            SDL_PushEvent(&event);
        }
        return true;
    }
    if (!nw_menu_has_quit(cursor) || cursor->y != cursor->y_max) {
        return false;
    }
    if (confirm) {
        NewMenu(cursor, cursor->menu == 0 ? NW_MENU_QUIT_TITLE : NW_MENU_QUIT_PAUSE, 1, -1);
    }
    return true;
}

static void nw_menu_confirm_text(char *text, f32 x, f32 y, f32 size, f32 letters)
{
    f32 sx = size * SDL_min(1.0f, letters / (f32)SDL_strlen(text));
    Text3D(text, x, y, 1.0f, sx, size, size, 0, 0);
}

static void nw_menu_dialog_entry(struct cursor_s *cursor, char *text, f32 *x, f32 *y, s32 *i)
{
    dme_sx = SDL_min(0.85f, 25.0f / (f32)SDL_strlen(text));
    dme_symul = 0.8f;
    DrawMenuEntry(cursor, text, x, y, i);
    *y -= 0.035f;
}

static void nw_menu_autosave_draw(struct cursor_s *cursor)
{
    int lang = Game.language, menu = cursor->menu;
    static char *disclaimer[6][5] = {
        { "PROGRESS IS SAVED AUTOMATICALLY", "IN A SEPARATE SLOT. MANUAL SAVING", "CAN STILL BE PERFORMED NORMALLY.", "AUTOSAVE CAN BE TURNED ON AND OFF", "AT ANY TIME IN THE OPTIONS MENU." },
        { "PROGRESSION SAUVEE AUTOMATIQUEMENT DANS", "UN EMPLACEMENT SEPARE. LA SAUVEGARDE", "MANUELLE FONCTIONNE TOUJOURS.", "LA SAUVEGARDE AUTO S'ACTIVE OU SE", "DESACTIVE A TOUT MOMENT DANS OPTIONS." },
        { "FORTSCHRITT WIRD AUTOMATISCH IN EINEM", "EIGENEN SLOT GESPEICHERT. MANUELLES", "SPEICHERN FUNKTIONIERT WIE GEWOHNT.", "AUTOSAVE LAESST SICH JEDERZEIT UNTER", "OPTIONEN AKTIVIEREN ODER DEAKTIVIEREN." },
        { "I PROGRESSI SI SALVANO AUTOMATICAMENTE", "IN UNO SLOT SEPARATO. IL SALVATAGGIO", "MANUALE FUNZIONA COME SEMPRE.", "L'AUTOSALVATAGGIO SI ATTIVA E DISATTIVA", "IN QUALSIASI MOMENTO DALLE OPZIONI." },
        { "EL PROGRESO SE GUARDA AUTOMATICAMENTE", "EN UN ESPACIO SEPARADO. EL GUARDADO", "MANUAL FUNCIONA COMO SIEMPRE.", "EL GUARDADO AUTOMATICO SE ACTIVA O", "DESACTIVA CUANDO QUIERAS EN OPCIONES." },
        { "VOORTGANG WORDT AUTOMATISCH OPGESLAGEN", "IN EEN APART SLOT. HANDMATIG OPSLAAN", "WERKT GEWOON ZOALS ALTIJD.", "AUTOMATISCH OPSLAAN ZET JE ALTIJD AAN", "OF UIT IN HET MENU OPTIES." },
    };
    static char *activate[6] = { "ENABLE", "ACTIVER", "AKTIVIEREN", "ATTIVA", "ACTIVAR", "AANZETTEN" };
    static char *disable[6] = { "DISABLE", "DESACTIVER", "DEAKTIVIEREN", "DISATTIVA", "DESACTIVAR", "UITZETTEN" };
    static char *without[6] = { "CONTINUE WITHOUT", "CONTINUER SANS", "OHNE FORTFAHREN", "CONTINUA SENZA", "CONTINUAR SIN", "DOORGAAN ZONDER" };
    static char *enable_title[6] = {
        "ENABLE AUTOSAVE?", "ACTIVER LA SAUVEGARDE AUTO?", "AUTOMATISCHES SPEICHERN AKTIVIEREN?",
        "ATTIVARE L'AUTOSALVATAGGIO?", "ACTIVAR GUARDADO AUTOMATICO?", "AUTOMATISCH OPSLAAN AANZETTEN?",
    };
    static char *rollback[6][3] = {
        { "A NEWER AUTOSAVE EXISTS.", "REPLACE IT WITH THIS PROGRESS?", "ITS PREVIOUS VERSIONS WILL BE KEPT." },
        { "UNE SAUVEGARDE AUTO PLUS RECENTE EXISTE.", "LA REMPLACER PAR CETTE PROGRESSION?", "LES VERSIONS PRECEDENTES SONT GARDEES." },
        { "EIN NEUERER AUTOSAVE IST VORHANDEN.", "DURCH DIESEN FORTSCHRITT ERSETZEN?", "VORHERIGE VERSIONEN BLEIBEN ERHALTEN." },
        { "ESISTE UN AUTOSALVATAGGIO PIU RECENTE.", "SOSTITUIRLO CON QUESTI PROGRESSI?", "LE VERSIONI PRECEDENTI RESTANO DISPONIBILI." },
        { "EXISTE UN GUARDADO AUTOMATICO MAS NUEVO.", "SUSTITUIRLO CON ESTE PROGRESO?", "SE CONSERVAN LAS VERSIONES ANTERIORES." },
        { "ER IS EEN NIEUWERE AUTOMATISCHE SAVE.", "VERVANGEN DOOR DEZE VOORTGANG?", "VORIGE VERSIES BLIJVEN BEWAARD." },
    };
    f32 x = 0.0f, y = -0.36f;
    s32 i = 0;
    nw_menu_confirm_text(menu == NW_MENU_AUTOSAVE_NEW ? enable_title[lang] : nw_menu_autosave_title[lang], 0, 0.65f, 0.6f, 28);
    if (menu == NW_MENU_AUTOSAVE_ROLLBACK) {
        for (int row = 0; row < 3; row++) nw_menu_confirm_text(rollback[lang][row], 0, 0.28f - row * 0.15f, 0.4f, 39);
        nw_menu_dialog_entry(cursor, activate[lang], &x, &y, &i);
        nw_menu_dialog_entry(cursor, without[lang], &x, &y, &i);
    } else {
        for (int row = 0; row < 5; row++) nw_menu_confirm_text(disclaimer[lang][row], 0, 0.38f - row * 0.13f, 0.4f, 39);
        nw_menu_dialog_entry(cursor, activate[lang], &x, &y, &i);
        nw_menu_dialog_entry(cursor, menu == NW_MENU_AUTOSAVE_NEW ? without[lang] : disable[lang], &x, &y, &i);
        nw_menu_dialog_entry(cursor, nw_menu_back[lang], &x, &y, &i);
    }
}

static void nw_menu_update_draw(struct cursor_s *cursor)
{
    int lang = Game.language;
    static char *title[6] = {
        "OPEN BROWSER?", "OUVRIR LE NAVIGATEUR?", "BROWSER OEFFNEN?", "APRIRE IL BROWSER?", "ABRIR EL NAVEGADOR?",
        "BROWSER OPENEN?",
    };
    static char *text[6][3] = {
        { "THIS OPENS THE RELEASE PAGE", "OF THE NEW VERSION IN YOUR BROWSER.", "DOWNLOAD AND START THE NEW VERSION." },
        { "CECI OUVRE LA PAGE DE LA NOUVELLE", "VERSION DANS TON NAVIGATEUR.", "TELECHARGE ET LANCE LA NOUVELLE VERSION." },
        { "DIES OEFFNET DIE SEITE DER NEUEN", "VERSION IN IHREM BROWSER.", "LADEN UND STARTEN SIE DIE NEUE VERSION." },
        { "SI APRIRA LA PAGINA DELLA NUOVA", "VERSIONE NEL TUO BROWSER.", "SCARICA E AVVIA LA NUOVA VERSIONE." },
        { "SE ABRIRA LA PAGINA DE LA NUEVA", "VERSION EN TU NAVEGADOR.", "DESCARGA E INICIA LA NUEVA VERSION." },
        { "DIT OPENT DE PAGINA VAN DE NIEUWE", "VERSIE IN JE BROWSER.", "DOWNLOAD EN START DE NIEUWE VERSIE." },
    };
    static char *open[6] = {
        "OPEN BROWSER", "OUVRIR LE NAVIGATEUR", "BROWSER OEFFNEN", "APRI IL BROWSER", "ABRIR EL NAVEGADOR", "BROWSER OPENEN",
    };
    f32 x = 0.0f, y = -0.36f;
    s32 i = 0;
    nw_menu_confirm_text(title[lang], 0, 0.65f, 0.6f, 28);
    for (int row = 0; row < 3; row++) nw_menu_confirm_text(text[lang][row], 0, 0.28f - row * 0.15f, 0.4f, 39);
    nw_menu_dialog_entry(cursor, open[lang], &x, &y, &i);
    nw_menu_dialog_entry(cursor, nw_menu_back[lang], &x, &y, &i);
}

static void nw_menu_load_confirm(struct cursor_s *cursor)
{
    f32 x = 0, y = -0.5f;
    s32 i = 0;
    static char *title[6] = { "LOAD GAME", "CHARGER PARTIE", "SPIEL LADEN", "CARICA PARTITA", "CARGAR PARTIDA", "SPEL LADEN" };
    nw_menu_confirm_text(title[Game.language], 0, 0.75f, 0.6f, 28);
    if (game) DrawGameSlot(game, 0, 0.15f, 0);
    DrawMenuEntry(cursor, tYES[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, tNO[Game.language], &x, &y, &i);
    dme_sx = 0.75f;
    DrawMenuEntry(cursor, nw_menu_previous[Game.language], &x, &y, &i);
}

static void nw_menu_previous_draw(struct cursor_s *cursor)
{
    static char *versions[6][2] = {
        { "PREVIOUS VERSION", "TWO SAVES AGO" }, { "VERSION PRECEDENTE", "DEUX SAUVEGARDES AVANT" },
        { "VORHERIGE VERSION", "ZWEI SPEICHERUNGEN ZURUECK" }, { "VERSIONE PRECEDENTE", "DUE SALVATAGGI FA" },
        { "VERSION ANTERIOR", "DOS GUARDADOS ANTES" }, { "VORIGE VERSIE", "TWEE SAVES GELEDEN" },
    };
    static char *missing[6] = { "VERSION NOT AVAILABLE", "VERSION INDISPONIBLE", "VERSION NICHT VERFUEGBAR", "VERSIONE NON DISPONIBILE", "VERSION NO DISPONIBLE", "VERSIE NIET BESCHIKBAAR" };
    nw_menu_confirm_text(nw_menu_previous[Game.language], 0, 0.65f, 0.6f, 28);
    if (nw_menu_previous_failed) nw_menu_confirm_text(missing[Game.language], 0, 0.2f, 0.45f, 32);
    f32 x = 0, y = -0.35f;
    s32 i = 0;
    DrawMenuEntry(cursor, versions[Game.language][0], &x, &y, &i);
    DrawMenuEntry(cursor, versions[Game.language][1], &x, &y, &i);
    DrawMenuEntry(cursor, nw_menu_back[Game.language], &x, &y, &i);
}

static void nw_menu_options(struct cursor_s *cursor, s32 paused)
{
    if (paused < 25) return;
    f32 x = PANELMENUX, y = -0.5f - (cursor->y_max - cursor->y_min) * NW_MENUDY * 0.5f;
    s32 i = 0;
    SDL_snprintf(tbuf, sizeof(tbuf), "%s: %s", tVIBRATION[Game.language], temp_vibration ? tON[Game.language] : tOFF[Game.language]);
    DrawMenuEntry(cursor, tbuf, &x, &y, &i);
    DrawMenuEntry(cursor, tSOUNDOPTIONS[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, tADJUSTSCREEN[Game.language], &x, &y, &i);
    if (LANGUAGEOPTION) DrawMenuEntry(cursor, tLANGUAGE[Game.language], &x, &y, &i);
    SDL_snprintf(tbuf, sizeof(tbuf), "%s: %s", nw_menu_autosave_title[Game.language], nw_autosave_enabled() ? tON[Game.language] : tOFF[Game.language]);
    dme_sx = SDL_min(1.0f, 18.0f / SDL_strlen(tbuf));
    DrawMenuEntry(cursor, tbuf, &x, &y, &i);
    DrawMenuEntry(cursor, tDONE[Game.language], &x, &y, &i);
}

void nw_menu_autosave_notice(void)
{
    NwAutosaveStatus status = nw_autosave_status();
    if (status == NW_AUTOSAVE_IDLE || (u32)Game.language >= 6 || !nw_autosave_notice_visible(SDL_GetTicks())) return;
    static char *notice[4][6] = {
        { "AUTOSAVE IN PROGRESS...", "SAUVEGARDE AUTO EN COURS...", "AUTOMATISCHES SPEICHERN...", "AUTOSALVATAGGIO IN CORSO...", "GUARDANDO AUTOMATICAMENTE...", "AUTOMATISCH OPSLAAN..." },
        { "AUTOSAVE COMPLETE", "SAUVEGARDE AUTO TERMINEE", "AUTOMATISCH GESPEICHERT", "AUTOSALVATAGGIO COMPLETATO", "GUARDADO AUTOMATICO COMPLETADO", "AUTOMATISCH OPGESLAGEN" },
        { "AUTOSAVE FAILED", "ECHEC SAUVEGARDE AUTO", "AUTOMATISCHES SPEICHERN FEHLGESCHLAGEN", "AUTOSALVATAGGIO NON RIUSCITO", "ERROR DE GUARDADO AUTOMATICO", "AUTOMATISCH OPSLAAN MISLUKT" },
        { "SAVE RECOVERED FROM BACKUP", "SAUVEGARDE RESTAUREE", "SPEICHERSTAND WIEDERHERGESTELLT", "SALVATAGGIO RECUPERATO DAL BACKUP", "GUARDADO RECUPERADO DE COPIA", "SAVE HERSTELD VANUIT BACKUP" },
    };
    char *text = notice[status - 1][Game.language];
    nw_autosave_notice_presented();
    nw_menu_confirm_text(text, 0.0f, -0.88f, 0.34f, 40);
}

static void nw_menu_confirm(struct cursor_s *cursor, s32 paused)
{
    static char *question[6] = {
        "ARE YOU SURE?", "VOULEZ-VOUS QUITTER?", "BIST DU SICHER?", "SEI SICURO?", "ESTAS SEGURO?", "WEET JE HET ZEKER?",
    };
    static char *warning[6][2] = {
        { "UNSAVED PROGRESS", "WILL BE LOST." },
        { "LES PROGRES NON SAUVEGARDES", "SERONT PERDUS." },
        { "NICHT GESPEICHERTE", "FORTSCHRITTE GEHEN VERLOREN." },
        { "PERDERAI I PROGRESSI", "NON SALVATI." },
        { "PERDERAS EL PROGRESO", "NO GUARDADO." },
        { "NIET OPGESLAGEN VOORTGANG", "GAAT VERLOREN." },
    };
    bool pause = cursor->menu == NW_MENU_QUIT_PAUSE;
    if (pause && paused < 25) {
        return;
    }
    f32 x = pause ? PANELMENUX : 0.0f;
    f32 y = pause ? -0.62f : -0.65f;
    s32 i = 0;
    nw_menu_confirm_text(question[Game.language], x, pause ? -0.25f : -0.43f, 0.5f, pause ? 20.0f : 30.0f);
    if (pause) {
        nw_menu_confirm_text(warning[Game.language][0], x, -0.39f, 0.38f, 27.0f);
        nw_menu_confirm_text(warning[Game.language][1], x, -0.47f, 0.38f, 27.0f);
    }
    DrawMenuEntry(cursor, tYES[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, tNO[Game.language], &x, &y, &i);
}

static void nw_menu_draw_quit(struct cursor_s *cursor, f32 *x, f32 *y, s32 *i)
{
    static const char *text[6] = {
        "QUIT TO %s", "QUITTER VERS %s", "ZURUECK ZU %s", "ESCI E TORNA A %s", "SALIR A %s", "TERUG NAAR %s",
    };
    if (!nw_menu_has_quit(cursor)) {
        return;
    }
    SDL_snprintf(tbuf, sizeof(tbuf), text[Game.language], nw_menu_desktop());
    /* The pause menu shares the screen with the inventory; fit long translations in its left panel. */
    f32 letters = cursor->menu == 3 ? 17.0f : 29.0f;
    dme_sx = SDL_min(1.0f, letters / (f32)SDL_strlen(tbuf));
    DrawMenuEntry(cursor, tbuf, x, y, i);
}

/* Retail's menu 0 with a grey MODS (SOON) line under LOAD GAME, one row more to centre. */
static void nw_menu_title(struct cursor_s *cursor, s32 paused)
{
    f32 x = 0.0f;
    if (pause_dir != 0) {
        f32 size = (25 - paused) * 0.08f;
        x += pause_dir == 1 ? -size : size;
    }
    /* Leave the extra desktop row the same bottom margin as retail's language row. */
    f32 centre = nw_menu_has_quit(cursor) ? -0.69f : -0.75f;
    f32 y = centre - (cursor->y_max - cursor->y_min + 1) * NW_MENUDY * 0.5f;
    s32 i = 0;
    DrawMenuEntry(cursor, tNEWGAME[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, tLOADGAME[Game.language], &x, &y, &i);
    Text3D(nw_menu_mods[Game.language], x, y, 1.0f, 0.6f, 0.6f, 0.6f, 0, NW_FONT3D_GREY);
    y += NW_MENUDY;
    if (LANGUAGEOPTION) {
        SDL_snprintf(tbuf, sizeof(tbuf), "%s: %s", tLANGUAGE[Game.language], LanguageName[Game.language]);
        DrawMenuEntry(cursor, tbuf, &x, &y, &i);
    }
    nw_menu_draw_quit(cursor, &x, &y, &i);
    if (nw_update_available()) {
        /* DrawMenuEntry's flashing colours for the selected row */
        s32 colour = (GlobalTimer.frame % 10 < 5) ? 0 : 3;
        Text3D(nw_menu_update_notice[Game.language][0], 0.95f, 0.9f, 1.0f, 0.3f, 0.3f, 0.3f, 8, colour);
        Text3D(nw_menu_update_notice[Game.language][1], 0.95f, 0.82f, 1.0f, 0.3f, 0.3f, 0.3f, 8, colour);
    }
}

static void nw_menu_pause(struct cursor_s *cursor, s32 paused)
{
    if (paused < 25) {
        return;
    }
    f32 x = PANELMENUX;
    f32 y = -0.5f - (cursor->y_max - cursor->y_min) * NW_MENUDY * 0.5f;
    s32 i = 0;
    DrawMenuEntry(cursor, tRESUME[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, tOPTIONS[Game.language], &x, &y, &i);
    DrawMenuEntry(cursor, Level == 0x25 ? tQUIT[Game.language] : tWARPROOM[Game.language], &x, &y, &i);
    if (TimeTrial) {
        DrawMenuEntry(cursor, tRESTARTTRIAL[Game.language], &x, &y, &i);
    }
    nw_menu_draw_quit(cursor, &x, &y, &i);
}

void DrawMenu(struct cursor_s *cursor, s32 paused)
{
    static char id[64]; /* upper case: Text3D draws lower case as models and pad buttons */

    if (editor_active || cursor->wait || GameMode == 1) {
        DrawMenu__retail(cursor, paused);
        nw_menu_autosave_notice();
        return;
    }
    nw_savemenu_prepare(cursor);
    if ((cursor->menu == 25 || cursor->menu == 28) && nw_saves_pending() && (u32)Game.language < 6) {
        static char *writing[6] = { "SAVING...", "SAUVEGARDE EN COURS...", "SPEICHERN...", "SALVATAGGIO IN CORSO...", "GUARDANDO...", "OPSLAAN..." };
        static char *deleting[6] = { "DELETING...", "SUPPRESSION EN COURS...", "LOESCHEN...", "ELIMINAZIONE IN CORSO...", "ELIMINANDO...", "VERWIJDEREN..." };
        nw_menu_confirm_text(cursor->menu == 25 ? writing[Game.language] : deleting[Game.language], 0, 0, 0.5f, 32);
    } else if ((cursor->menu == NW_MENU_AUTOSAVE_NEW || cursor->menu == NW_MENU_AUTOSAVE_OPTIONS ||
         cursor->menu == NW_MENU_AUTOSAVE_ROLLBACK) && (u32)Game.language < 6) {
        nw_menu_autosave_draw(cursor);
    } else if (cursor->menu == NW_MENU_SAVE_PREVIOUS && (u32)Game.language < 6) {
        nw_menu_previous_draw(cursor);
    } else if (cursor->menu == 21 && (u32)Game.language < 6) {
        nw_menu_load_confirm(cursor);
    } else if (cursor->menu == 4 && (u32)Game.language < 6) {
        nw_menu_options(cursor, paused);
    } else if ((cursor->menu == NW_MENU_QUIT_TITLE || cursor->menu == NW_MENU_QUIT_PAUSE) && (u32)Game.language < 6) {
        nw_menu_confirm(cursor, paused);
    } else if (cursor->menu == NW_MENU_UPDATE && (u32)Game.language < 6) {
        nw_menu_update_draw(cursor);
    } else if (cursor->menu == 0 && (u32)Game.language < 6) {
        nw_menu_title(cursor, paused);
    } else if (cursor->menu == 3 && nw_menu_has_quit(cursor) && (u32)Game.language < 6) {
        nw_menu_pause(cursor, paused);
    } else if (nw_nameentry_draw(cursor)) {
        /* the name typed on the keyboard: no letter table */
    } else {
        DrawMenu__retail(cursor, paused);
        nw_savemenu_draw(cursor);
    }
    /* 0: title, 3 and 4: pause */
    if (cursor->menu == 0 || cursor->menu == NW_MENU_QUIT_TITLE || cursor->menu == NW_MENU_UPDATE ||
        ((cursor->menu == 3 || cursor->menu == 4 || cursor->menu == NW_MENU_QUIT_PAUSE) && paused >= 25)) {
        if (!id[0]) {
            SDL_snprintf(id, sizeof(id), "NUWORLD %s", nw_build_name);
            for (char *c = id; *c; c++) {
                *c = (char)SDL_toupper((unsigned char)*c);
            }
        }
        Text3D(id, -0.95f, 0.9f, 1.0f, 0.3f, 0.3f, 0.3f, 2, 2);
    }
    nw_menu_autosave_notice();
}
