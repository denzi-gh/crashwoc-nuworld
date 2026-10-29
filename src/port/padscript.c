#include "port/padscript.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "game/gametypes.h"
#include "nups2/ps2ctl.h"
#include "port/level.h"
#include "shim/shim.h"

#define NW_PAD_SCRIPT_STEPS 1024
#define NW_PAD_REC_HEADER 0x10 /* struct nupadrec_s before data[] */

extern struct ldata_s LData[44];

typedef struct NwPadStep {
    int reads;
    NwPadInput in;
} NwPadStep;

static struct {
    bool on;
    NwPadStep *steps;
    int nsteps, step, read;
    uint8_t *rec; /* a recording's frames */
    int nframes, frame;
} nw_pad_script;

/* Start and select are left out: they pause the game and open its debug menus. */
static const char nw_pad_sweep[] = "60 cross 128 0\n"
                                   "10 square 128 0\n"
                                   "40 cross 255 0\n"
                                   "10 circle 128 0\n"
                                   "40 cross 0 0\n"
                                   "10 triangle 128 0\n"
                                   "20 r1 128 255\n"
                                   "20 l1\n"
                                   "30 r2 128 0 255 128\n"
                                   "30 l2 128 0 0 128\n"
                                   "15 square 255 128\n"
                                   "15 cross|square 128 0 128 0\n"
                                   "15 circle 0 128 128 255\n"
                                   "20 up\n"
                                   "20 right\n"
                                   "20 down\n"
                                   "20 left\n"
                                   "10 l3|r3\n"
                                   "40 - 128 255\n";

static const struct {
    const char *name;
    uint16_t bit;
} nw_pad_names[] = {
    { "l2", NW_PAD_L2 },
    { "r2", NW_PAD_R2 },
    { "l1", NW_PAD_L1 },
    { "r1", NW_PAD_R1 },
    { "triangle", NW_PAD_TRIANGLE },
    { "circle", NW_PAD_CIRCLE },
    { "cross", NW_PAD_CROSS },
    { "square", NW_PAD_SQUARE },
    { "select", NW_PAD_SELECT },
    { "l3", NW_PAD_L3 },
    { "r3", NW_PAD_R3 },
    { "start", NW_PAD_START },
    { "up", NW_PAD_UP },
    { "right", NW_PAD_RIGHT },
    { "down", NW_PAD_DOWN },
    { "left", NW_PAD_LEFT },
};

void nw_pad_script_stop(void)
{
    SDL_free(nw_pad_script.steps);
    SDL_free(nw_pad_script.rec);
    SDL_zero(nw_pad_script);
}

static bool nw_pad_buttons(const char *word, uint16_t *buttons)
{
    *buttons = 0;
    if (SDL_strcmp(word, "-") == 0) {
        return true;
    }
    for (const char *p = word; *p;) {
        size_t len = strcspn(p, "|");
        size_t i = 0;
        while (i < SDL_arraysize(nw_pad_names) &&
               (SDL_strlen(nw_pad_names[i].name) != len || SDL_strncasecmp(p, nw_pad_names[i].name, len) != 0)) {
            i++;
        }
        if (i == SDL_arraysize(nw_pad_names)) {
            return false;
        }
        *buttons |= nw_pad_names[i].bit;
        p += len + (p[len] == '|');
    }
    return true;
}

static bool nw_pad_step(char *line, NwPadStep *step)
{
    char *words[7], *save = NULL;
    int n = 0;
    for (char *w = SDL_strtok_r(line, " \t\r", &save); w && n < (int)SDL_arraysize(words);
         w = SDL_strtok_r(NULL, " \t\r", &save)) {
        words[n++] = w;
    }
    if (n < 2 || n == 3 || n == 5 || n > 6) {
        return false;
    }
    char *end;
    long reads = SDL_strtol(words[0], &end, 10);
    if (*end || reads < 1 || !nw_pad_buttons(words[1], &step->in.buttons)) {
        return false;
    }
    uint8_t *sticks[] = { &step->in.lx, &step->in.ly, &step->in.rx, &step->in.ry };
    for (int i = 0; i < 4; i++) {
        long v = i + 2 < n ? SDL_strtol(words[i + 2], &end, 10) : 0x80;
        if ((i + 2 < n && *end) || v < 0 || v > 0xFF) {
            return false;
        }
        *sticks[i] = (uint8_t)v;
    }
    step->reads = (int)reads;
    step->in.l2 = step->in.buttons & NW_PAD_L2 ? 0xFF : 0;
    step->in.r2 = step->in.buttons & NW_PAD_R2 ? 0xFF : 0;
    return true;
}

bool nw_pad_script_parse(const char *text)
{
    nw_pad_script_stop();
    NwPadStep *steps = SDL_malloc(NW_PAD_SCRIPT_STEPS * sizeof(*steps));
    char *copy = SDL_strdup(text);
    int n = 0, line = 0;
    bool ok = steps && copy;
    for (char *p = copy, *next; ok && p; p = next) {
        next = SDL_strchr(p, '\n');
        if (next) {
            *next++ = '\0';
        }
        line++;
        char *hash = SDL_strchr(p, '#');
        if (hash) {
            *hash = '\0';
        }
        if (p[strspn(p, " \t\r")] == '\0') {
            continue;
        }
        ok = n < NW_PAD_SCRIPT_STEPS && nw_pad_step(p, &steps[n]);
        n += ok;
        if (!ok) {
            SDL_SetError("pad script line %d: not `<reads> <buttons> [lx ly [rx ry]]`", line);
        }
    }
    SDL_free(copy);
    if (ok && n == 0) {
        ok = SDL_SetError("pad script without steps");
    }
    if (!ok) {
        SDL_free(steps);
        return false;
    }
    nw_pad_script.steps = steps;
    nw_pad_script.nsteps = n;
    nw_pad_script.on = true;
    return true;
}

bool nw_pad_script_recording(const uint8_t *data, size_t size)
{
    nw_pad_script_stop();
    if (size < NW_PAD_REC_HEADER) {
        return SDL_SetError("pad recording of %zu bytes", size);
    }
    int32_t nframes;
    SDL_memcpy(&nframes, data + 8, sizeof(nframes));
    size_t have = (size - NW_PAD_REC_HEADER) / sizeof(struct nupadframe_s);
    if (nframes < 0 || (size_t)nframes > have) {
        return SDL_SetError("pad recording of %d frames in %zu bytes", nframes, size);
    }
    nw_pad_script.rec = SDL_malloc(size);
    if (!nw_pad_script.rec) {
        return false;
    }
    SDL_memcpy(nw_pad_script.rec, data, size);
    nw_pad_script.nframes = nframes;
    nw_pad_script.on = true;
    return true;
}

static bool nw_pad_script_ppd(const char *name)
{
    size_t len = SDL_strlen(name);
    return len >= 4 && SDL_strcasecmp(name + len - 4, ".ppd") == 0;
}

/* The level's recording (LEVELS\<path>.PPD), NULL when the disc has none. */
static uint8_t *nw_pad_script_level(const NwVfs *vfs, int level, size_t *size)
{
    char path[NW_VFS_PATH_MAX];
    SDL_snprintf(path, sizeof(path), "cdrom0:\\LEVELS\\%s.PPD;1", LData[level].path);
    NwVfsFile *f = nw_vfs_open(vfs, path, NW_VFS_READ);
    int64_t n = f ? nw_vfs_size(f) : -1;
    uint8_t *data = n > 0 ? SDL_malloc((size_t)n) : NULL;
    if (data && nw_vfs_read(f, data, (size_t)n) != n) {
        SDL_free(data);
        data = NULL;
    }
    nw_vfs_close(f);
    *size = (size_t)n;
    return data;
}

bool nw_pad_script_is_recording(const char *spec)
{
    return SDL_strcmp(spec, "auto") == 0 || nw_pad_script_ppd(spec);
}

bool nw_pad_script_start(const NwVfs *vfs, const char *spec, int level)
{
    size_t size = 0;
    if (SDL_strcmp(spec, "sweep") == 0) {
        return nw_pad_script_parse(nw_pad_sweep);
    }
    if (SDL_strcmp(spec, "auto") == 0) {
        if (level < 0) {
            return SDL_SetError("--pad-script auto needs --level");
        }
        uint8_t *rec = nw_pad_script_level(vfs, level, &size);
        bool ok = rec ? nw_pad_script_recording(rec, size) : nw_pad_script_parse(nw_pad_sweep);
        SDL_free(rec);
        return ok;
    }
    char *data = SDL_LoadFile(spec, &size);
    if (!data) {
        return false;
    }
    bool ok = nw_pad_script_ppd(spec) ? nw_pad_script_recording((const uint8_t *)data, size)
                                      : nw_pad_script_parse(data);
    SDL_free(data);
    return ok;
}

bool nw_pad_script_read(struct nupadframe_s *frame)
{
    if (!nw_pad_script.on || (nw_level_arrival() >= 0 && !nw_level_arrived())) {
        return false;
    }
    if (nw_pad_script.rec) {
        static const NwPadInput idle = { 0, 0x80, 0x80, 0x80, 0x80, 0, 0 };
        if (nw_pad_script.frame < nw_pad_script.nframes) {
            SDL_memcpy(frame, nw_pad_script.rec + NW_PAD_REC_HEADER + nw_pad_script.frame++ * sizeof(*frame),
                       sizeof(*frame));
            frame->ok = 0;
        }
        /* SavePadRecord keeps a frame without a pad with buttons_hi 0, which playback drops. */
        if (nw_pad_script.frame >= nw_pad_script.nframes || frame->buttons_hi == 0) {
            nw_pad_frame(&idle, frame);
        }
        return true;
    }
    const NwPadStep *step = &nw_pad_script.steps[nw_pad_script.step];
    nw_pad_frame(&step->in, frame);
    if (++nw_pad_script.read == step->reads) {
        nw_pad_script.read = 0;
        nw_pad_script.step = (nw_pad_script.step + 1) % nw_pad_script.nsteps;
    }
    return true;
}
