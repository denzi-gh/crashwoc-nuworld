/* [prototype] The opening's title (src/port/opening/internal.h): the skip past the logos, the switch to the station
   and its cross-dissolve, SELECT's background alone, NEW GAME's way into the intro without a fade to black (the logo
   leaves, the frame without it is held while the intro loads, and the intro, which starts on that same frame,
   dissolves in). */
#include "port/opening/internal.h"
#include "port/signals.h"

static uint32_t nw_xf_ids; /* the cross-dissolves so far: each takes a new id */
NwGo nw_go;

/* The logos' scene gives way to the station's: world_scene's entries of both logos cuts become it. */
void nw_opening_switch(void)
{
    nw_op.passers_from = nw_op.sky_frames;
    nw_op.switched = true;
    if (!nw_op.swapped) {
        nw_op.xf_id = ++nw_xf_ids;
        nw_op.xf_frame = 0;
    }
}

/* earth.start: the station's earth (shown and carried by LOGOSR.CUT) starts blending in over the Universal one; the
   clouds form once it stands still (clouds.ats, from earth.done). */
void nw_opening_swap(void)
{
    nw_op.swapped = true;
}

/* Once a game frame: the cross-dissolve's opacity for the packets from here on. */
void nw_opening_dissolve(void)
{
    if (nw_go.phase != NW_GO_NONE || nw_op.xf_id == 0) {
        return;
    }
    float u = nw_opening.xf_frames > 0 ? (float)nw_op.xf_frame / nw_opening.xf_frames : 1;
    nw_packet_xfade(nw_op.xf_id, u >= 1 ? 0 : 1 - u * u * (3 - 2 * u));
    nw_op.xf_frame++;
}

/* Once a game frame of the title on the station: NEW GAME's way into the intro. */
#define NW_PAD_SELECT 0x100u     /* game/game's menu bits */
#define NW_PAD_DIRECTIONS 0xF000u

static bool nw_bg_only;

bool nw_opening_title(void)
{
    return nw_opening_on() && cutmovie == 0 && nw_op.switched;
}

s32 NuSoundStopStream(s32 channel);
void GameMusic(s32 sfx, s32 i);
extern s32 logos_played;

static bool nw_opening_skippable(void)
{
    return nw_signal_happened(nw_op.signals.skip_open) && nw_opening_active() && cutworldix == 0 && !nw_op.switched &&
           nw_op.time < nw_opening.skip_to && CutScene[0] != NULL;
}

bool nw_opening_skip_hint(void)
{
    return nw_opening_skippable() && nw_op.sky_frames < nw_op.skip_until;
}

/* TRIANGLE or START (the keyboard's ENTER) past the Konami card shows the hint for a while; pressed again while it
   shows, the logos' first cut goes on from skip_to, the logo flying in from afar to the title (the earth's swap and
   the camera's move made by then, as their times say): the logos' sound stops, the title's music starts there (and
   not again at the cut's end: logos_played, as after a full play). */
#define NW_PAD_TRIANGLE 0x10u /* game/game's menu bits */
#define NW_PAD_START 0x800u

void nw_opening_skip(struct instNUGCUTSCENE_s *scene)
{
    struct nupad_s *pad = Pad[0];
    if (!nw_opening_skippable() || !pad || !(pad->paddata_db & (NW_PAD_TRIANGLE | NW_PAD_START))) {
        return;
    }
    if (!nw_opening_skip_hint()) {
        nw_op.skip_until = nw_op.sky_frames + 240; /* four seconds */
        return;
    }
    nw_op.skip_until = 0;
    scene->time = fminf(nw_opening.skip_to, CutScene[0]->duration - 4.0f); /* under black, the logo's arrival fading in */
    fadeval = 255;
    fade_rate = -16;
    NuSoundStopStream(4);
    logos_played = 1;
    GameMusic(0xaa, 0);
}

bool nw_opening_background_only(void)
{
    return nw_bg_only && nw_opening_title();
}

/* Once a game frame of the title on the station: SELECT shows the background alone, a direction brings the logo
   and menu back. Meanwhile the menu takes no input and the attract demo waits. */
void nw_opening_background(void)
{
    struct nupad_s *pad = Pad[0];
    u32 db = pad ? pad->paddata_db : 0;
    if (Cursor.menu != 0 || nw_go.phase != NW_GO_NONE) {
        nw_bg_only = false;
        return;
    }
    if (!nw_bg_only && (db & NW_PAD_SELECT)) {
        nw_bg_only = true;
    } else if (nw_bg_only && (db & NW_PAD_DIRECTIONS)) {
        nw_bg_only = false;
        Cursor.button_lock = 8; /* that press only brings the menu back */
        return;
    }
    if (nw_bg_only) {
        Cursor.button_lock = 2;
        Cursor.item_frame = 1;
    }
}

/* NEW GAME confirmed (the name entry's last item, game.c): the name entry asked for the intro and a fade to black,
   and this frame draws no menu any more. Called as it draws (the menu ran before), so the frame kept over it is the one
   before, with the menu's text: the text fades first (go_text), then the logo (nw_opening_go). The fade is undone. */
void nw_opening_go_start(void)
{
    if (!nw_new_game_dissolve || nw_go.phase != NW_GO_NONE || next_cut_movie != 1 || !nw_op.switched || cutmovie != 0) {
        return;
    }
    nw_go.phase = NW_GO_LEAVING;
    nw_go.frame = 0;
    nw_go.id = ++nw_xf_ids;
    nw_packet_xfade(nw_go.id, 1);
}

bool nw_opening_quiet(void)
{
    return nw_opening_on() && (nw_go.phase != NW_GO_NONE || nw_intro.on);
}

void nw_opening_go(void)
{
    if (nw_go.phase != NW_GO_LEAVING) {
        return;
    }
    fade_rate = 0;
    fadeval = 0;
    nw_go.frame++;
    int text = (int)nw_opening.go_text, wait = text + (int)nw_opening.go_wait, out = (int)nw_opening.go_frames;
    if (nw_go.frame <= text) {
        float u = (float)nw_go.frame / (float)(text > 0 ? text : 1);
        nw_packet_xfade(nw_go.id, 1 - u * u * (3 - 2 * u)); /* the menu's text fading */
        return;
    }
    if (nw_go.frame <= wait) {
        return; /* the logo stays a moment */
    }
    if (nw_go.frame == wait + 1) {
        /* The logo dissolves out: the frame with it is kept over the live one, which no longer draws it. */
        nw_go.id = ++nw_xf_ids;
    }
    if (nw_go.frame <= wait + out) {
        float u = (float)(nw_go.frame - wait - 1) / (float)(out > 0 ? out : 1);
        nw_packet_xfade(nw_go.id, 1 - u * u * (3 - 2 * u));
        return;
    }
    /* Held: this frame keeps the one before (the station alone) over itself and what follows, and the title ends at
       once (PlayCutMovie's loop leaves on a full fade). */
    nw_go.phase = NW_GO_HOLDING;
    nw_go.id = ++nw_xf_ids;
    nw_packet_xfade(nw_go.id, 1);
    fadeval = 0xFF;
    fadehack = 0;
}

/* The intro after NEW GAME loaded at once, in the frame the title ended: no loading screen's frames (its tunnel, its
   blank swaps), which would hold the picture still between the title's station and the intro's. As retail leaves
   it: the fade full, the loaded cut's result. */
s32 BGLoadCutMovie(s32 movie)
{
    if (!nw_opening_on() || nw_go.phase != NW_GO_HOLDING || movie != 1) {
        return BGLoadCutMovie__retail(movie);
    }
    s32 rv = LoadCutMovie(movie);
    fadeval = 0xFF;
    return rv;
}

/* Once a game frame of the intro after NEW GAME: no fade in from black; the held frame dissolves into it. */
void nw_opening_reveal(void)
{
    if (nw_go.phase == NW_GO_HOLDING) {
        nw_go.phase = NW_GO_REVEALING;
        nw_go.frame = 0;
    }
    fadeval = 0;
    fade_rate = 0;
    float u = nw_opening.reveal_frames > 0 ? (float)++nw_go.frame / nw_opening.reveal_frames : 1;
    if (u >= 1) {
        nw_packet_xfade(nw_go.id, 0);
        nw_go.phase = NW_GO_NONE;
        return;
    }
    nw_packet_xfade(nw_go.id, 1 - u * u * (3 - 2 * u));
}
