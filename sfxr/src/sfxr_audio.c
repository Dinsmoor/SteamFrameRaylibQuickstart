// sfxr_audio.c - positional sounds, sounds made in code, the microphone
// (sfxr_audio.h, docs/AUDIO.md).
//
// Output is raylib's audio (miniaudio underneath). Each sound gets a few
// "voices" (raylib sound aliases sharing one buffer) so it can overlap
// itself; a voice playing at a world position is re-panned every frame from
// the head pose.
//
// The microphone uses miniaudio directly (raylib only does playback). The
// functions are already in libraylib; the header is included here with the
// same build switches raylib uses, so the structs match.

#include "sfxr_internal.h"
#include "sfxr_audio.h"

#define MA_NO_JACK
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "external/miniaudio.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOUNDS 128
#define MAX_VOICES 8

typedef struct {
    bool used;
    Sound base;
    Sound voice[MAX_VOICES];
    int nvoices, next;
    float pitch;
    bool positional[MAX_VOICES];
    Vector3 at[MAX_VOICES];
    float volume[MAX_VOICES];
} Snd;

static struct {
    bool on;
    float master;
    Snd snd[MAX_SOUNDS];
} A = { .master = 1.0f };

bool sfxr_audio_on(void) { return A.on; }
void sfxr_audio_volume(float master) { A.master = Clamp(master, 0, 1); }

bool sfxr_audio_init(void)
{
    if (A.on) return true;
    if (sfxr_backend() == SFXR_BACKEND_SCRIPT && !sfxr_env_flag("SFXR_AUDIO", false)) return false;   // tests: silent
    InitAudioDevice();
    A.on = IsAudioDeviceReady();
    SFXR_LOG("audio: %s", A.on ? "on" : "off (no audio device)");
    return A.on;
}

void sfxr_audio_shutdown(void)
{
    sfxr_mic_stop();
    if (!A.on) return;
    for (int i = 1; i < MAX_SOUNDS; i++) {
        Snd *s = &A.snd[i];
        if (!s->used) continue;
        for (int v = 1; v < s->nvoices; v++) UnloadSoundAlias(s->voice[v]);
        UnloadSound(s->base);
    }
    memset(A.snd, 0, sizeof A.snd);
    CloseAudioDevice();
    A.on = false;
}

static SfxrSound add(Sound base, int voices)
{
    if (!base.frameCount) return 0;
    for (int i = 1; i < MAX_SOUNDS; i++) {
        Snd *s = &A.snd[i];
        if (s->used) continue;
        memset(s, 0, sizeof *s);
        s->used = true;
        s->base = base;
        s->pitch = 1;
        s->nvoices = voices < 1 ? 1 : voices > MAX_VOICES ? MAX_VOICES : voices;
        s->voice[0] = base;
        for (int v = 1; v < s->nvoices; v++) s->voice[v] = LoadSoundAlias(base);
        return i;
    }
    UnloadSound(base);
    return 0;
}

SfxrSound sfxr_sound_load(const char *path, int voices)
{
    if (!A.on || !path || !FileExists(path)) return 0;
    return add(LoadSound(path), voices);
}

SfxrSound sfxr_sound_from_wave(Wave w, int voices)
{
    if (!A.on || !w.frameCount) return 0;
    return add(LoadSoundFromWave(w), voices);
}

float sfxr_sound_seconds(SfxrSound id)
{
    if (id <= 0 || id >= MAX_SOUNDS || !A.snd[id].used || !A.snd[id].base.stream.sampleRate) return 0;
    return (float)A.snd[id].base.frameCount / (float)A.snd[id].base.stream.sampleRate;
}

void sfxr_sound_pitch(SfxrSound id, float pitch)
{
    if (id > 0 && id < MAX_SOUNDS && A.snd[id].used) A.snd[id].pitch = pitch;
}

// Pan from where the sound is across your view, gain from its distance
// (full within a meter, then 1/distance) and a little quieter behind you
// (your head is in the way). Pan never goes fully to one ear: a sound
// exactly to your side still reaches the other ear.
void sfxr_audio_spatial(Vector3 at, float *pan, float *gain)
{
    Vector3 local = sfxr_pose_apply_inv(sfxr_head(), at);   // +X right, -Z ahead
    float d = Vector3Length(local);
    float p = d > 0.15f ? local.x / d : 0.0f;
    if (SFXR_BREAK(sfxr_audio_pan_flipped)) p = -p;
    float g = d > 1.0f ? 1.0f / d : 1.0f;
    if (local.z > 0 && d > 0.15f) g *= 1.0f - 0.25f * local.z / d;
    if (pan) *pan = Clamp(p * 0.85f, -1, 1);
    if (gain) *gain = Clamp(g, 0, 1);
}

static void start(SfxrSound id, bool positional, Vector3 at, float volume)
{
    if (!A.on || id <= 0 || id >= MAX_SOUNDS || !A.snd[id].used) return;
    Snd *s = &A.snd[id];
    int v = s->next;
    for (int k = 0; k < s->nvoices; k++) {   // a free voice if there is one, else the oldest
        int c = (s->next + k) % s->nvoices;
        if (!IsSoundPlaying(s->voice[c])) { v = c; break; }
    }
    s->next = (v + 1) % s->nvoices;
    s->positional[v] = positional;
    s->at[v] = at;
    s->volume[v] = volume;
    float pan = 0, gain = 1;
    if (positional) sfxr_audio_spatial(at, &pan, &gain);
    SetSoundPitch(s->voice[v], s->pitch);
    SetSoundPan(s->voice[v], pan);
    SetSoundVolume(s->voice[v], volume * gain * A.master);
    PlaySound(s->voice[v]);
}

void sfxr_sound_play(SfxrSound s, Vector3 at, float volume) { start(s, true, at, volume); }
void sfxr_sound_play_here(SfxrSound s, float volume) { start(s, false, (Vector3){ 0 }, volume); }

void sfxr_audio_update(void)
{
    if (!A.on) return;
    for (int i = 1; i < MAX_SOUNDS; i++) {
        Snd *s = &A.snd[i];
        if (!s->used) continue;
        for (int v = 0; v < s->nvoices; v++) {
            if (!s->positional[v] || !IsSoundPlaying(s->voice[v])) continue;
            float pan, gain;
            sfxr_audio_spatial(s->at[v], &pan, &gain);
            SetSoundPan(s->voice[v], pan);
            SetSoundVolume(s->voice[v], s->volume[v] * gain * A.master);
        }
    }
}

// --- sounds made in code ---------------------------------------------------------------

Wave sfxr_wave_synth(const SfxrSynth *sp)
{
    const int rate = 22050;
    int n = (int)(sp->seconds * (float)rate);
    if (n < 1) n = 1;
    short *pcm = RL_CALLOC((size_t)n, sizeof(short));
    float phase = 0, phase2 = 0, lp = 0;
    uint32_t rng = 0x9E3779B9u;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)rate, u = (float)i / (float)n;
        float f = sp->freq + (sp->freq_end - sp->freq) * u;
        phase += 2 * PI * f / (float)rate;
        phase2 += 2 * PI * f * 2.76f / (float)rate;   // an inharmonic partial: bell-like
        float tone = sinf(phase) + sp->harmonic * sinf(phase2);
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float white = (float)(rng & 0xFFFF) / 32768.0f - 1.0f;
        lp += (white - lp) * 0.35f;   // noise a little softened
        float s = sp->freq > 0 ? tone * (1 - sp->noise) + lp * sp->noise : lp;
        float env = sp->attack > 0 && t < sp->attack ? t / sp->attack : 1.0f;
        if (sp->decay > 0) env *= expf(-(t - fminf(t, sp->attack)) / sp->decay);
        env *= fminf(1.0f, (float)(n - i) / (0.005f * (float)rate));   // no click at the end
        pcm[i] = (short)(Clamp(s * env * sp->volume, -1, 1) * 32000.0f);
    }
    return (Wave){ (unsigned)n, (unsigned)rate, 16, 1, pcm };
}

SfxrSound sfxr_sound_synth(const SfxrSynth *sp, int voices)
{
    if (!A.on) return 0;
    Wave w = sfxr_wave_synth(sp);
    SfxrSound s = sfxr_sound_from_wave(w, voices);
    UnloadWave(w);
    return s;
}

// --- the microphone ---------------------------------------------------------------------

#define MIC_RATE 16000
#define RING     (MIC_RATE * 8)   // eight seconds

static struct {
    ma_device dev;
    bool on;
    float ring[RING];
    atomic_uint w, r;             // write and read counts (the ring is indexed modulo RING)
    _Atomic float level;
} M;

static void mic_data(ma_device *d, void *out, const void *in, ma_uint32 frames)
{
    (void)d; (void)out;
    const float *s = in;
    unsigned w = atomic_load(&M.w);
    float sum = 0;
    for (ma_uint32 i = 0; i < frames; i++) {
        M.ring[(w + i) % RING] = s[i];
        sum += s[i] * s[i];
    }
    atomic_store(&M.w, w + frames);
    float rms = frames ? sqrtf(sum / (float)frames) : 0;
    atomic_store(&M.level, Clamp(rms * 4.0f, 0, 1));   // speech peaks around 0.1-0.25 rms
}

bool sfxr_mic_start(void)
{
    if (M.on) return true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = MIC_RATE;
    cfg.dataCallback = mic_data;
    if (ma_device_init(NULL, &cfg, &M.dev) != MA_SUCCESS) { SFXR_LOG("microphone: none available"); return false; }
    if (ma_device_start(&M.dev) != MA_SUCCESS) { ma_device_uninit(&M.dev); SFXR_LOG("microphone: won't start"); return false; }
    atomic_store(&M.w, 0);
    atomic_store(&M.r, 0);
    M.on = true;
    SFXR_LOG("microphone: on (%s)", M.dev.capture.name);
    return true;
}

void sfxr_mic_stop(void)
{
    if (!M.on) return;
    ma_device_uninit(&M.dev);
    M.on = false;
}

bool  sfxr_mic_on(void) { return M.on; }
float sfxr_mic_level(void) { return M.on ? atomic_load(&M.level) : 0.0f; }

int sfxr_mic_read(float *out, int max)
{
    if (!M.on) return 0;
    unsigned w = atomic_load(&M.w), r = atomic_load(&M.r);
    if (w - r > RING) r = w - RING;   // fell behind: drop the oldest
    int n = 0;
    while (r != w && n < max) out[n++] = M.ring[r++ % RING];
    atomic_store(&M.r, r);
    return n;
}
