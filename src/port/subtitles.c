#include "port/subtitles.h"

#include <SDL3/SDL.h>

#include "audio/irx.h"
#include "audio/spu2.h"
#include "game/font3d.h"
#include "game/gametypes.h"
#include "game/main.retail.h"
#include "game/panel.retail.h"
#include "gameplay/gameplay.h"
#include "nu3d/nucamera.h"
#include "numath/numtx.h"
#include "platform/log.h"
#include "port/font3d.h"
#include "port/level.h"
#include "port/menu.h"

extern struct game_s Game;
extern s32 Level, cutmovie, new_level, new_mode;
extern s32 Paused, screendump, save_paused;
extern s32 loadthread_id;
extern struct nucamera_s *pNuCam;
extern struct nugscn_s *font3d_scene;
void MaxVP(void);
void GameVP(void);
void LoadFont3D(void);
void NuCameraSet(struct nucamera_s *camera);
void NuRndrClear(int flags, int colour, float depth);
void Text3D(char *txt, f32 x, f32 y, f32 z, f32 sx, f32 sy, f32 sz, s32 align, s32 colour);

static float nw_subtitles_hud_top = -1.0f;

bool nw_subtitles_enabled(bool title)
{
    return nw_profile.subtitles && (!title || nw_profile.title_subtitles);
}

void nw_subtitles_hud_line(float y)
{
    nw_subtitles_hud_top = SDL_max(nw_subtitles_hud_top, y);
}

static const char *nw_subtitles_current(void)
{
    if (!nw_subtitles_enabled(cutmovie == 0) || loadthread_id != 0) {
        return NULL;
    }
    const NwIrx *audio = nw_nusound_irx();
    for (int i = 0; i < NW_IRX_CHANNELS; i++) {
        const STREAMDATA *voice = &audio->StreamBuffer[i];
        if (!voice->Status || voice->iStatus != 0 || voice->Channel >= NW_IRX_CHANNELS ||
            audio->SPU_KeyStatus[voice->Channel] != 1 || nw_spu2_voice_lead_in(voice->Channel)) {
            continue;
        }
        for (int j = 0; j < audio->fileinfo_cnt; j++) {
            const NwIrxFilename *file = &audio->ADPCM_FileNames[j];
            if (file->ID != voice->ID || SDL_strncasecmp(file->Filename, "SFX\\VCE_", 8)) {
                continue;
            }
            const char *name = SDL_strrchr(file->Filename, '\\');
            uint32_t rate = (uint16_t)file->Pitch;
            if (name && rate) {
                /* The SPU starts counting at key-on, including after a stream seek. */
                uint64_t samples = (uint64_t)voice->SampleOffset + nw_spu2_voice_samples(voice->Channel);
                uint32_t ms = (uint32_t)(samples * 1000 / rate);
                const char *text = nw_subtitles_at(name + 1, Game.language, ms);
                if (text) {
                    return text;
                }
            }
            break;
        }
    }
    return NULL;
}

static void nw_subtitles_draw(void)
{
    if (cutmovie < 0 && (screendump ? save_paused : Paused)) {
        return;
    }
    const char *text = nw_subtitles_current();
    if (text && pNuCam) {
        static const char *last;
        static NwSubtitleLayout layout;
        static char first_line[NW_SUBTITLE_LINE_SIZE + 4];
        if (text != last) {
            last = text;
            if (!nw_subtitles_layout(text, &layout)) {
                nw_log_error(NW_LOG_PORT, "subtitles: cannot render Text3D line: %s", text);
                layout.count = 0;
            } else if (layout.speaker_length) {
                /* Text3D ignores its colour controls when measuring and centring the line. */
                SDL_snprintf(first_line, sizeof(first_line), "#b%.*s#o%s",
                    layout.speaker_length, layout.line[0], layout.line[0] + layout.speaker_length);
            }
        }
        if (!layout.count) {
            return;
        }
        /* Space omits the HUD font. Load it on its first subtitle, also after a live toggle. */
        if (cutmovie < 0 && Level == NW_LEVEL_SPACE && !font3d_scene) {
            LoadFont3D();
        }
        struct numtx_s camera = pNuCam->mtx;
        /* Finish the scene and clear only its depth before drawing the text. */
        NuRndrClear(2, 0, 1.0f);
        MaxVP();
        NuMtxSetIdentity(&pNuCam->mtx);
        NuCameraSet(pNuCam);
        /* Vehicle health and objectives occupy the bottom of gameplay's HUD. */
        float bottom = cutmovie < 0 && Level != NW_LEVEL_HUB && Level != NW_LEVEL_GAMEOVER &&
                       Level != NW_LEVEL_SPACE ? -0.50f : -0.88f;
        if (cutmovie < 0) {
            float hud_top = SDL_max(nw_subtitles_hud_top, nw_menu_autosave_hud_top());
            bottom = SDL_max(bottom, hud_top + 0.10f);
        }
        float y = bottom + (layout.count - 1) * 0.10f;
        bool punctuation = nw_font3d_subtitle_punctuation(true);
        for (int i = 0; i < layout.count; i++) {
            char *line = i == 0 && layout.speaker_length ? first_line : layout.line[i];
            Text3D(line, 0.0f, y, 1.0f, layout.scale, layout.scale, layout.scale, 0, 0);
            y -= 0.10f;
        }
        nw_font3d_subtitle_punctuation(punctuation);
        pNuCam->mtx = camera;
        NuCameraSet(pNuCam);
        GameVP();
    }
}

/* Movies and gameplay have separate drawing loops. */
void DrawFade(void)
{
    if (cutmovie >= 0) {
        nw_subtitles_draw();
    }
    DrawFade__retail();
}

void DrawPanel(void)
{
    nw_subtitles_hud_top = -1.0f;
    nw_menu_autosave_hud_reset();
    DrawPanel__retail();
    if (cutmovie < 0 && new_level == -1 && new_mode == -1) {
        nw_subtitles_draw();
    }
}
