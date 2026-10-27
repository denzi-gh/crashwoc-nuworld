#include "platform/audio.h"

#include <SDL3/SDL.h>

#include "audio/spu2.h"

#define NW_AUDIO_RATE 48000
#define NW_AUDIO_FRAME (2 * sizeof(float))
#define NW_AUDIO_TARGET_MS 60 /* queued ahead of the device */
#define NW_AUDIO_MAX_MS 250   /* past this the queue is dropped */
#define NW_AUDIO_TRIM 0.005f  /* the most the playback rate is nudged to hold the target */
#define NW_AUDIO_CHUNK 256
#define NW_AUDIO_VOICE 4 /* the channel GameSfx plays voices on (gamesfx_channel) */

/* The SPU runs on the game's EE threads, the device on its own; the lock lets close race the game thread. */
static struct {
    SDL_Mutex *lock;
    SDL_AudioStream *stream; /* under lock */
    float master;            /* under lock once there is one */
    float dc_in[2], dc_out[2];
} nw_audio = { .master = 1.0f };

static unsigned nw_audio_queued_ms(void)
{
    return (unsigned)((uint64_t)SDL_GetAudioStreamQueued(nw_audio.stream) / NW_AUDIO_FRAME * 1000 / NW_AUDIO_RATE);
}

/* After an underrun the queue starts again at the target, with silence. */
static void nw_audio_prime(void)
{
    static const float silence[NW_AUDIO_CHUNK * 2];
    unsigned frames = NW_AUDIO_RATE * NW_AUDIO_TARGET_MS / 1000;
    while (frames) {
        unsigned n = frames < NW_AUDIO_CHUNK ? frames : NW_AUDIO_CHUNK;
        SDL_PutAudioStreamData(nw_audio.stream, silence, (int)(n * NW_AUDIO_FRAME));
        frames -= n;
    }
}

/* The PS2's analog out is AC coupled, so a paused voice's held sample is no offset (PCSX2's DCFilter). */
static void nw_audio_out(void *user, const int16_t *frames, unsigned count)
{
    float buf[NW_AUDIO_CHUNK * 2];
    (void)user;

    SDL_LockMutex(nw_audio.lock);
    if (nw_audio.stream) {
        if (SDL_GetAudioStreamQueued(nw_audio.stream) == 0) {
            nw_audio_prime();
        }
        while (count) {
            unsigned n = count < NW_AUDIO_CHUNK ? count : NW_AUDIO_CHUNK;
            for (unsigned i = 0; i < n * 2; i++) {
                float in = frames[i] / 32767.0f, *last = &nw_audio.dc_in[i & 1], *out = &nw_audio.dc_out[i & 1];
                *out = in - *last + 0.995f * *out;
                *last = in;
                buf[i] = *out;
            }
            SDL_PutAudioStreamData(nw_audio.stream, buf, (int)(n * NW_AUDIO_FRAME));
            frames += n * 2;
            count -= n;
        }
        unsigned ms = nw_audio_queued_ms();
        if (ms > NW_AUDIO_MAX_MS) {
            SDL_ClearAudioStream(nw_audio.stream);
        } else {
            float trim = ((float)ms - NW_AUDIO_TARGET_MS) / NW_AUDIO_TARGET_MS * NW_AUDIO_TRIM;
            trim = trim < -NW_AUDIO_TRIM ? -NW_AUDIO_TRIM : trim > NW_AUDIO_TRIM ? NW_AUDIO_TRIM : trim;
            SDL_SetAudioStreamFrequencyRatio(nw_audio.stream, 1.0f + trim);
        }
    }
    SDL_UnlockMutex(nw_audio.lock);
}

/* Before the game thread starts: the SPU's output is set once. */
bool nw_audio_open(void)
{
    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, NW_AUDIO_RATE };

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }
    if (!nw_audio.lock && !(nw_audio.lock = SDL_CreateMutex())) {
        return false;
    }
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        return false;
    }
    SDL_LockMutex(nw_audio.lock);
    nw_audio.stream = stream;
    SDL_SetAudioStreamGain(stream, nw_audio.master);
    SDL_UnlockMutex(nw_audio.lock);
    SDL_ResumeAudioStreamDevice(stream);
    nw_spu2_set_output(nw_audio_out, NULL);
    return true;
}

void nw_audio_set_buses(int master, int voice)
{
    nw_spu2_set_gain(NW_AUDIO_VOICE, SDL_clamp(voice, 0, 100) * NW_SPU2_UNITY / 100);
    if (nw_audio.lock) {
        SDL_LockMutex(nw_audio.lock);
    }
    nw_audio.master = (float)SDL_clamp(master, 0, 100) / 100.0f;
    if (nw_audio.stream) {
        SDL_SetAudioStreamGain(nw_audio.stream, nw_audio.master);
    }
    if (nw_audio.lock) {
        SDL_UnlockMutex(nw_audio.lock);
    }
}

/* The game thread may still run: the output stays set and goes nowhere. */
void nw_audio_close(void)
{
    if (!nw_audio.lock) {
        return;
    }
    SDL_LockMutex(nw_audio.lock);
    SDL_DestroyAudioStream(nw_audio.stream);
    nw_audio.stream = NULL;
    SDL_UnlockMutex(nw_audio.lock);
}
