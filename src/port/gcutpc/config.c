/* The .TXT beside a .CSP (docs/csp-spec.md, paragraph 5), read into NUGCUTPCCONFIG_s: the vocabulary, a small function
   a word, in the order of the spec's table. The words, their values and their order are the same as
   crashWoCEditor/csp_txt.py's VOCABULARY. */
#include <math.h>

#include "port/gcutpc/internal.h"
#include "port/gcutpc/nugcutpc_config.h"

#define DEFAULT_FPSEC 30.0f

#define CONFIG(target) ((NUGCUTPCCONFIG_s *)(target))
#define SETTINGS(target) ((NUGCUTPCSETTINGS_s *)(target))
#define SUBTITLE(target) ((NUGCUTPCSUBTITLE_s *)(target))

/* Words of one value or none, into a field of the configuration, the settings or the subtitle being read. */
#define FLAG(word, field)                                                                                            \
    static bool word_##word(NUGCUTPCTXT_s *txt, void *t)                                                             \
    {                                                                                                                \
        (void)txt;                                                                                                   \
        CONFIG(t)->field = true;                                                                                     \
        return true;                                                                                                 \
    }
#define VALUE(word, reader, target, field)                                                                           \
    static bool word_##word(NUGCUTPCTXT_s *txt, void *t)                                                             \
    {                                                                                                                \
        return reader(txt, &target(t)->field);                                                                       \
    }

/* Time */
VALUE(fpsec, NuGCutPCTxtFloat, CONFIG, fpsec)
FLAG(looping, looping)
VALUE(resume_before_end, NuGCutPCTxtFloat, CONFIG, resume_before_end)
VALUE(fade_in, NuGCutPCTxtFloat, CONFIG, fade_in)
FLAG(snap_out, snap_out)
FLAG(wipe_out, wipe_out)

static bool word_skip(NUGCUTPCTXT_s *txt, void *t)
{
    static const char *const states[] = { "off", "on" };
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    int32_t on;
    if (!NuGCutPCTxtChoice(txt, states, 2, &on)) {
        return false;
    }
    config->skip_given = true;
    config->skip_on = on != 0;
    if (NuGCutPCTxtOption(txt, "to") && !NuGCutPCTxtFloat(txt, &config->skip_to)) {
        return false;
    }
    return !NuGCutPCTxtOption(txt, "hint") || NuGCutPCTxtFloat(txt, &config->skip_hint);
}

/* Scenes and models */
static bool word_scene(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCSCENEFILE_s *scene = NUGCUTPC_PUSH(txt, config->scenes, config->scene_count);
    return scene && NuGCutPCTxtString(txt, &scene->name) && NuGCutPCTxtString(txt, &scene->path);
}

static bool word_play_beside(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCBESIDE_s *beside = NUGCUTPC_PUSH(txt, config->beside, config->beside_count);
    if (!beside || !NuGCutPCTxtString(txt, &beside->cut)) {
        return false;
    }
    beside->looping = NuGCutPCTxtOption(txt, "looping");
    return true;
}

static bool word_character(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCMODEL_s *model = NUGCUTPC_PUSH(txt, config->characters, config->character_count);
    return model && NuGCutPCTxtString(txt, &model->path) && NuGCutPCTxtString(txt, &model->name);
}

/* Sound */
static bool read_floats(NUGCUTPCTXT_s *txt, float *out, int count)
{
    for (int i = 0; i < count; i++) {
        if (!NuGCutPCTxtFloat(txt, &out[i])) {
            return false;
        }
    }
    return true;
}

static bool word_sfx(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCSFX_s *sfx = NUGCUTPC_PUSH(txt, config->sfx, config->sfx_count);
    if (!sfx || !NuGCutPCTxtInt(txt, &sfx->id)) {
        return false;
    }
    sfx->frame = sfx->pos[0] = sfx->pos[1] = sfx->pos[2] = NAN;
    if (NuGCutPCTxtOption(txt, "frame") && !NuGCutPCTxtFloat(txt, &sfx->frame)) {
        return false;
    }
    if (NuGCutPCTxtOption(txt, "pos") && !read_floats(txt, sfx->pos, 3)) {
        return false;
    }
    sfx->per_language = NuGCutPCTxtOption(txt, "per_language");
    return true;
}

VALUE(music, NuGCutPCTxtInt, CONFIG, music)
FLAG(holdaudio, holdaudio)

/* Look */
VALUE(farclip, NuGCutPCTxtFloat, CONFIG, farclip)
VALUE(nearclip, NuGCutPCTxtFloat, CONFIG, nearclip)
VALUE(top_backr, NuGCutPCTxtByte, CONFIG, top_back[0])
VALUE(top_backg, NuGCutPCTxtByte, CONFIG, top_back[1])
VALUE(top_backb, NuGCutPCTxtByte, CONFIG, top_back[2])
VALUE(bottom_backr, NuGCutPCTxtByte, CONFIG, bottom_back[0])
VALUE(bottom_backg, NuGCutPCTxtByte, CONFIG, bottom_back[1])
VALUE(bottom_backb, NuGCutPCTxtByte, CONFIG, bottom_back[2])

/* The settings block: what changes from its frame on. */
VALUE(set_frame, NuGCutPCTxtFloat, SETTINGS, frame)
VALUE(set_farclip, NuGCutPCTxtFloat, SETTINGS, farclip)
VALUE(set_nearclip, NuGCutPCTxtFloat, SETTINGS, nearclip)
VALUE(set_fognear, NuGCutPCTxtFloat, SETTINGS, fognear)
VALUE(set_fogfar, NuGCutPCTxtFloat, SETTINGS, fogfar)
VALUE(set_fogr, NuGCutPCTxtByte, SETTINGS, fog[0])
VALUE(set_fogg, NuGCutPCTxtByte, SETTINGS, fog[1])
VALUE(set_fogb, NuGCutPCTxtByte, SETTINGS, fog[2])
VALUE(set_foga, NuGCutPCTxtByte, SETTINGS, fog[3])
VALUE(set_top_backr, NuGCutPCTxtByte, SETTINGS, top_back[0])
VALUE(set_top_backg, NuGCutPCTxtByte, SETTINGS, top_back[1])
VALUE(set_top_backb, NuGCutPCTxtByte, SETTINGS, top_back[2])
VALUE(set_bottom_backr, NuGCutPCTxtByte, SETTINGS, bottom_back[0])
VALUE(set_bottom_backg, NuGCutPCTxtByte, SETTINGS, bottom_back[1])
VALUE(set_bottom_backb, NuGCutPCTxtByte, SETTINGS, bottom_back[2])

#define BLOCK_WORD(word, handler) { .name = #word, .read = word_##handler }
#define VOCABULARY(words) { words, sizeof(words) / sizeof(*(words)) }
static const NUGCUTPCWORD_s settings_words[] = {
    BLOCK_WORD(frame, set_frame), BLOCK_WORD(farclip, set_farclip), BLOCK_WORD(nearclip, set_nearclip),
    BLOCK_WORD(fognear, set_fognear), BLOCK_WORD(fogfar, set_fogfar), BLOCK_WORD(fogr, set_fogr),
    BLOCK_WORD(fogg, set_fogg), BLOCK_WORD(fogb, set_fogb), BLOCK_WORD(foga, set_foga),
    BLOCK_WORD(top_backr, set_top_backr), BLOCK_WORD(top_backg, set_top_backg), BLOCK_WORD(top_backb, set_top_backb),
    BLOCK_WORD(bottom_backr, set_bottom_backr), BLOCK_WORD(bottom_backg, set_bottom_backg),
    BLOCK_WORD(bottom_backb, set_bottom_backb),
};
static const NUGCUTPCVOCABULARY_s settings_vocabulary = VOCABULARY(settings_words);

static void *open_settings(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCSETTINGS_s *s = NUGCUTPC_PUSH(txt, config->settings, config->settings_count);
    if (!s) {
        return NULL;
    }
    s->frame = s->farclip = s->nearclip = s->fognear = s->fogfar = NAN;
    for (int i = 0; i < 4; i++) {
        s->fog[i] = NUGCUTPC_NOT_GIVEN;
    }
    for (int i = 0; i < 3; i++) {
        s->top_back[i] = s->bottom_back[i] = NUGCUTPC_NOT_GIVEN;
    }
    return s;
}

static bool word_ambient(NUGCUTPCTXT_s *txt, void *t)
{
    return read_floats(txt, CONFIG(t)->ambient, 3);
}

static bool word_light(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCLIGHT_s *light = NUGCUTPC_PUSH(txt, config->lights, config->light_count);
    if (!light || !NuGCutPCTxtInt(txt, &light->index)) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        light->dir[i] = light->colour[i] = NAN;
    }
    if (NuGCutPCTxtOption(txt, "dir") && !read_floats(txt, light->dir, 3)) {
        return false;
    }
    return !NuGCutPCTxtOption(txt, "colour") || read_floats(txt, light->colour, 3);
}

FLAG(super_widescreen, super_widescreen)

/* Effects */
VALUE(ptl, NuGCutPCTxtString, CONFIG, ptl)

static bool word_ptl_merge(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    const char **path = NUGCUTPC_PUSH(txt, config->ptl_merge, config->ptl_merge_count);
    return path && NuGCutPCTxtString(txt, path);
}

static bool word_ats(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    if (!NuGCutPCTxtMore(txt)) {
        return NuGCutPCTxtFail(txt, "ats needs at least one file");
    }
    while (NuGCutPCTxtMore(txt)) {
        const char **file = NUGCUTPC_PUSH(txt, config->ats, config->ats_count);
        if (!file || !NuGCutPCTxtString(txt, file)) {
            return false;
        }
    }
    return true;
}

static bool word_tex_anim(NUGCUTPCTXT_s *txt, void *t)
{
    static const char *const states[] = { "off", "on" };
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCTEXANIM_s *anim = NUGCUTPC_PUSH(txt, config->tex_anim, config->tex_anim_count);
    int32_t on;
    if (!anim || !NuGCutPCTxtInt(txt, &anim->bit) || !NuGCutPCTxtChoice(txt, states, 2, &on)) {
        return false;
    }
    anim->on = on != 0;
    return true;
}

VALUE(render_group, NuGCutPCTxtInt, CONFIG, render_group)

/* The subtitle block. */
static const char *const aligns[] = {
    "centre", "top", "top_right", "right", "bottom_right", "bottom", "bottom_left", "left", "top_left",
};

VALUE(sub_text_id, NuGCutPCTxtInt, SUBTITLE, text_id)
VALUE(sub_text, NuGCutPCTxtString, SUBTITLE, text)
VALUE(sub_start_time, NuGCutPCTxtFloat, SUBTITLE, start_time)
VALUE(sub_end_time, NuGCutPCTxtFloat, SUBTITLE, end_time)
VALUE(sub_scale, NuGCutPCTxtFloat, SUBTITLE, scale)
VALUE(sub_fade_time, NuGCutPCTxtFloat, SUBTITLE, fade_time)

static bool word_sub_colour(NUGCUTPCTXT_s *txt, void *t)
{
    return read_floats(txt, SUBTITLE(t)->colour, 4);
}

static bool word_sub_pos(NUGCUTPCTXT_s *txt, void *t)
{
    return read_floats(txt, SUBTITLE(t)->pos, 2);
}

static bool word_sub_align(NUGCUTPCTXT_s *txt, void *t)
{
    return NuGCutPCTxtChoice(txt, aligns, (int)(sizeof(aligns) / sizeof(*aligns)), &SUBTITLE(t)->align);
}

static const NUGCUTPCWORD_s subtitle_words[] = {
    BLOCK_WORD(text_id, sub_text_id), BLOCK_WORD(text, sub_text), BLOCK_WORD(start_time, sub_start_time),
    BLOCK_WORD(end_time, sub_end_time), BLOCK_WORD(colour, sub_colour), BLOCK_WORD(align, sub_align),
    BLOCK_WORD(pos, sub_pos), BLOCK_WORD(scale, sub_scale), BLOCK_WORD(fade_time, sub_fade_time),
};
static const NUGCUTPCVOCABULARY_s subtitle_vocabulary = VOCABULARY(subtitle_words);

static void *open_subtitle(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCSUBTITLE_s *s = NUGCUTPC_PUSH(txt, config->subtitles, config->subtitle_count);
    if (!s) {
        return NULL;
    }
    s->text_id = s->align = NUGCUTPC_NOT_GIVEN;
    s->start_time = s->end_time = s->scale = s->fade_time = NAN;
    s->colour[0] = s->colour[1] = s->colour[2] = s->colour[3] = s->pos[0] = s->pos[1] = NAN;
    return s;
}

/* Direction */
static bool word_event(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCCONFIG_s *config = CONFIG(t);
    NUGCUTPCEVENT_s *event = NUGCUTPC_PUSH(txt, config->events, config->event_count);
    return event && NuGCutPCTxtString(txt, &event->name) && NuGCutPCTxtFloat(txt, &event->frame);
}

VALUE(next_cut_scene, NuGCutPCTxtString, CONFIG, next_cut_scene)

static bool word_on_end(NUGCUTPCTXT_s *txt, void *t)
{
    static const char *const actions[] = { "title_menu", "end_movie", "next" };
    int32_t action;
    if (!NuGCutPCTxtChoice(txt, actions, 3, &action)) {
        return false;
    }
    CONFIG(t)->on_end = (NuGCutPCOnEnd)(NUGCUTPC_END_TITLE_MENU + action);
    return true;
}

/* The vocabulary, in the spec's order. */
#define WORD(word) { .name = #word, .read = word_##word }
#define REPEATED(word) { .name = #word, .read = word_##word, .repeat = true }
#define BLOCK(word, vocabulary) { .name = #word, .open = open_##word, .block = &vocabulary, .repeat = true }

static const NUGCUTPCWORD_s config_words[] = {
    WORD(fpsec), WORD(looping), WORD(resume_before_end), WORD(skip), WORD(fade_in), WORD(snap_out), WORD(wipe_out),
    REPEATED(scene), REPEATED(play_beside), REPEATED(character),
    REPEATED(sfx), WORD(music), WORD(holdaudio),
    WORD(farclip), WORD(nearclip), WORD(top_backr), WORD(top_backg), WORD(top_backb), WORD(bottom_backr),
    WORD(bottom_backg), WORD(bottom_backb), BLOCK(settings, settings_vocabulary), WORD(ambient),
    REPEATED(light), WORD(super_widescreen),
    WORD(ptl), REPEATED(ptl_merge), WORD(ats), REPEATED(tex_anim), WORD(render_group),
    BLOCK(subtitle, subtitle_vocabulary),
    REPEATED(event), WORD(next_cut_scene), WORD(on_end),
};
static const NUGCUTPCVOCABULARY_s config_vocabulary = VOCABULARY(config_words);

static void set_not_given(NUGCUTPCCONFIG_s *config)
{
    config->fpsec = DEFAULT_FPSEC;
    config->resume_before_end = config->skip_to = config->skip_hint = config->fade_in = NAN;
    config->music = config->render_group = NUGCUTPC_NOT_GIVEN;
    config->farclip = config->nearclip = NAN;
    for (int i = 0; i < 3; i++) {
        config->top_back[i] = config->bottom_back[i] = NUGCUTPC_NOT_GIVEN;
        config->ambient[i] = NAN;
    }
}

NUGCUTPCCONFIG_s *NuGCutPCConfigure(const char *text, size_t size, const char *where, NUGCUTPCERROR_s *error)
{
    NUGCUTPCERROR_s ignored;
    error = error ? error : &ignored;
    error->text[0] = '\0';
    NUGCUTPCARENA_s *arena = NuGCutPCArenaCreate();
    NUGCUTPCCONFIG_s *config = arena ? NuGCutPCArenaAlloc(arena, 1, sizeof(*config)) : NULL;
    if (!config) {
        NuGCutPCArenaDestroy(arena);
        NuGCutPCFail(error, "out of memory");
        return NULL;
    }
    config->arena = arena;
    set_not_given(config);
    NUGCUTPCTXT_s txt = { .text = text, .size = size, .where = where ? where : "<txt>", .arena = arena,
                          .error = error };
    if (!NuGCutPCTxtRead(&txt, &config_vocabulary, config)) {
        NuGCutPCArenaDestroy(arena);
        return NULL;
    }
    return config;
}

void NuGCutPCConfigFree(NUGCUTPCCONFIG_s *config)
{
    if (config) {
        NuGCutPCArenaDestroy(config->arena); /* the configuration lives in its own arena */
    }
}
