/* game/text's NewLanguage (port/units.toml override): the power-up texts that name a shoulder button hold its
   glyph code instead (Text3D's q r u v for L1 R1 L2 R2), so they show the bound button of the device in use. */
#include "game/gametypes.h"

extern struct game_s Game;
extern struct powerdata_s PData[6];
extern char *tSNEAKSHOES[6];
extern char *tDOUBLEJUMP[6], *tDOUBLEJUMPTEXT[6][7];
extern char *tDEATHTORNADOSPIN[6], *tDEATHTORNADOSPINTEXT[6][7];
extern char *tFRUITBAZOOKA[6];
extern char *tSPEEDSHOES[6];
extern char *tSUPERBELLYFLOP[6], *tSUPERBELLYFLOPTEXT[6][7];

void DefaultTimeTrialNames(s32 all);

static char *nw_sneakshoes_text[6][7] = {
    { "TO SNEAK ALONG SUSPENDED NITRO", "CRATES HOLD u WHILE MOVING", "", "", "", "", "" },
    {
        "POUR MARCHER SUR LA POINTE",
        "DES PIEDS MAINTIENS u",
        "ENFONCEE PENDANT TA COURSE",
        "OU TA MARCHE",
        "",
        "",
        ""
    },
    {
        "HALTEN SIE W\216HREND DES LAUFENS",
        "ODER GEHENS DIE u-TASTE",
        "GEDR\232CKT, UM SICH AUF",
        "ZEHENSPITZEN ZU BEWEGEN",
        "",
        "",
        ""
    },
    {
        "PER MUOVERTI IN PUNTA DI PIEDI",
        "TIENI PREMUTO IL TASTO u",
        "MENTRE CAMMINI O CORRI",
        "",
        "",
        "",
        ""
    },
    {
        "PARA ANDAR DE PUNTILLAS,",
        "MANT\220N PULSADO u MIENTRAS",
        "CORRES O ANDAS",
        "",
        "",
        "",
        ""
    },
    {
        "HOUD DE u-TOETS INGEDRUKT",
        "TERWIJL JE RENT OF LOOPT OM",
        "OP JE TENEN TE KUNNEN LOPEN",
        "",
        "",
        "",
        ""
    }
};

static char *nw_fruitbazooka_text[6][7] = {
    {
        "TO FIRE FRUIT AT A TARGET",
        "HOLD q TO RAISE THE GUN, AIM",
        "WITH THE DIRECTIONAL BUTTONS,",
        "THEN PRESS o TO FIRE",
        "",
        "",
        ""
    },
    {
        "POUR LANCER DES FRUITS SUR",
        "UNE CIBLE, LEVE LE CANON EN",
        "MAINTENANT LA TOUCHE q",
        "ENFONCEE, VISE A L'AIDE DES",
        "TOUCHES DIRECTIONNELLES ET",
        "APPUIE SUR LA TOUCHE o",
        "POUR TIRER"
    },
    {
        "DR\232CKEN SIE DIE q-TASTE UM",
        "DAS GEWEHR ANZUHEBEN, RICHTEN",
        "SIE ES MIT DEN RICHTUNGSTASTEN",
        "AUF DAS ZIEL AUS UND DR\232CKEN",
        "SIE DIE o -TASTE, UM MIT",
        "FR\232CHTEN AUF EIN ZIEL",
        "ZU SCHIE\341EN"
    },
    {
        "PER COLPIRE IL BERSAGLIO CON",
        "UN FRUTTO TIENI PREMUTO IL",
        "TASTO q PER SOLLEVARE IL",
        "BAZOOKA PRENDI LA MIRA CON I",
        "TASTI DIREZIONALI E PREMI IL",
        "TASTO o PER SPARARE",
        ""
    },
    {
        "PARA DISPARAR FRUTA, MANT\220N",
        "PULSADO q PARA LEVANTAR EL",
        "ARMA APUNTA UTILIZANDO LOS",
        "BOTONES DE DIRECC\241ON Y, A",
        "CONTINUACI\242N PULSA o",
        "PARA DISPARAR",
        ""
    },
    {
        "ALS JE FRUIT WILT AFVUREN,",
        "HOUD JE DE q-TOETS INGEDRUKT",
        "OM AAN TE LEGGEN, RICHT JE",
        "D.M.V. DE RICHTINGSTOETSEN EN",
        "DRUK JE OP DE o -TOETS",
        "OM TE VUREN",
        ""
    }
};

static char *nw_speedshoes_text[6][7] = {
    { "TO RUN FASTER", "HOLD v WHILE RUNNING", "", "", "", "", "" },
    { "POUR COURIR PLUS VITE", "MAINTIENS LA TOUCHE", "v ENFONCEE", "", "", "", "" },
    {
        "HALTEN SIE W\216HREND DES",
        "LAUFENS DIE v-TASTE GEDR\232CKT",
        "UM SCHNELLER ZU WERDEN",
        "",
        "",
        "",
        ""
    },
    {
        "PER CORRERE PI\227 VELOCEMENTE",
        "TIENI PREMUTO IL TASTO v",
        "DURANTE LA CORSA",
        "",
        "",
        "",
        ""
    },
    { "PARA CORRER M\240S R\240PIDO", "MANT\220N PULSADO v", "MIENTRAS CORRES", "", "", "", "" },
    { "HOUD DE v-TOETS INGEDRUKT", "OM SNELLER TE RENNEN", "", "", "", "", "" }
};

void NewLanguage(s32 lang)
{
    Game.language = lang;
    PData[0].name = tSNEAKSHOES[Game.language];
    PData[0].text = nw_sneakshoes_text[Game.language];
    PData[1].name = tDOUBLEJUMP[Game.language];
    PData[1].text = tDOUBLEJUMPTEXT[Game.language];
    PData[2].name = tDEATHTORNADOSPIN[Game.language];
    PData[2].text = tDEATHTORNADOSPINTEXT[Game.language];
    PData[3].name = tFRUITBAZOOKA[Game.language];
    PData[3].text = nw_fruitbazooka_text[Game.language];
    PData[4].name = tSPEEDSHOES[Game.language];
    PData[4].text = nw_speedshoes_text[Game.language];
    PData[5].name = tSUPERBELLYFLOP[Game.language];
    PData[5].text = tSUPERBELLYFLOPTEXT[Game.language];
    DefaultTimeTrialNames(0);
}
