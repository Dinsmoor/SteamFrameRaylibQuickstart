// sfxr_audio.c - positional sounds, sounds made in code, the microphone
// (sfxr_audio.h, docs/AUDIO.md).
//
// Output is raylib's audio device (miniaudio underneath), but the sounds
// are mixed here, not by raylib: raylib can only pan a sound (turn one ear
// down), and panning alone makes left and right surprisingly hard to tell
// apart in a headset. Ears use three cues, and sfxr_audio_mix gives each
// playing sound all three, per ear:
//   level  the far ear hears it quieter (the head is in the way)
//   time   ...and later: up to 0.66 ms, the width of a head in sound
//   tone   ...and duller: the head blocks high notes more than low ones
// plus quieter and duller behind you, and with distance. The mix is added
// to raylib's own (music streams through raylib as before) by a "mixed
// audio processor" on the audio thread. The frame loop only sets targets
// (sfxr_audio_update, from sfxr_frame_begin); the audio thread glides to
// them across each block, so moving sounds never click.
//
// Without an audio device (tests, a headless machine) it runs "offline":
// sounds still load and play, and sfxr_audio_mix renders them for a test
// to listen to.
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
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOUNDS 128
#define MAX_PLAYS  64
#define MAX_VOICES 8

// The ear model's numbers (docs/AUDIO.md has the reasoning).
#define HEAD_RADIUS   0.0875f   // m
#define SPEED_SOUND   343.0f    // m/s
#define FAR_EAR_LEVEL 0.25f     // a sound straight to one side: the far ear gets a quarter (-12 dB)...
#define NEAR_EAR_LIFT 0.3f      // ...and the near one 30% more than straight ahead
#define SHADOW_HZ     1800.0f   // the far ear's high notes above this are lost, straight to one side
#define BEHIND_HZ     6000.0f   // both ears, straight behind
#define OPEN_HZ       16000.0f  // at or above this, no filter
#define CENTER_LEVEL  0.7f      // each ear, straight ahead (about raylib's centered level)

typedef struct {
    bool used;
    float *pcm;          // mono
    int frames, rate;
    float pitch;
    int voices;          // how many of it may play at once
} Snd;

typedef struct {
    int snd;             // 0: free
    unsigned gen;
    unsigned long order; // start order, for stealing the oldest
    SfxrEmitter em;      // (frame loop only)
    bool loop, stop;
    // audio thread
    double cursor;       // in source frames
    float step;          // source frames per output frame
    float tg[2], td[2], tc[2];   // targets: gain, delay (source frames), filter coefficient per ear
    float g[2], d[2], c[2];      // where the audio thread is
    float lp[2];
    bool fresh;
} Play;

static struct {
    bool on, device;
    bool paused;         // sfxr_audio_pause
    int rate;            // output frames per second
    float master;
    unsigned long starts;
    pthread_mutex_t lock;
    Snd snd[MAX_SOUNDS];
    Play play[MAX_PLAYS];
    Music music;
    bool music_on, music_loop;
    float music_volume, music_fade;
    char music_path[256];
} A = { .master = 1.0f, .lock = PTHREAD_MUTEX_INITIALIZER };

bool sfxr_audio_on(void) { return A.on; }
bool sfxr_audio_device(void) { return A.device; }
int sfxr_audio_rate(void) { return A.rate; }
void sfxr_audio_volume(float master) { A.master = Clamp(master, 0, 1); }

void sfxr_audio_pause(bool paused)
{
    if (paused == A.paused) return;
    A.paused = paused;
    if (A.music_on) { if (paused) PauseMusicStream(A.music); else ResumeMusicStream(A.music); }
}
bool sfxr_audio_paused(void) { return A.paused; }

static void mixed(void *buffer, unsigned int frames) { sfxr_audio_mix((float *)buffer, (int)frames); }

bool sfxr_audio_init(void)
{
    if (A.on) return true;
    A.rate = 48000;
    // tests: offline (sounds play, nothing is heard) unless SFXR_AUDIO asks for the speakers
    if (!(sfxr_backend() == SFXR_BACKEND_SCRIPT && !sfxr_env_flag("SFXR_AUDIO", false))) {
        InitAudioDevice();
        A.device = IsAudioDeviceReady();
    }
    if (A.device) {
        // the device's rate: raylib converts a sound to it on loading
        short one = 0;
        Sound probe = LoadSoundFromWave((Wave){ 1, 22050, 16, 1, &one });
        if (probe.stream.sampleRate) A.rate = (int)probe.stream.sampleRate;
        UnloadSound(probe);
        AttachAudioMixedProcessor(mixed);
    }
    A.on = true;
    SFXR_LOG("audio: %s", A.device ? TextFormat("on (%d Hz)", A.rate) : "offline (no audio device: sounds play, silently)");
    return A.on;
}

void sfxr_audio_shutdown(void)
{
    sfxr_mic_stop();
    if (!A.on) return;
    sfxr_music_stop();
    if (A.device) {
        DetachAudioMixedProcessor(mixed);
        CloseAudioDevice();
    }
    pthread_mutex_lock(&A.lock);
    for (int i = 1; i < MAX_SOUNDS; i++) free(A.snd[i].pcm);
    memset(A.snd, 0, sizeof A.snd);
    memset(A.play, 0, sizeof A.play);
    pthread_mutex_unlock(&A.lock);
    A.on = A.device = false;
}

SfxrSound sfxr_sound_from_wave(Wave w, int voices)
{
    if (!A.on || !w.frameCount || !w.data) return 0;
    Wave m = WaveCopy(w);
    WaveFormat(&m, (int)m.sampleRate, 32, 1);   // mono float: a sound comes from one place
    SfxrSound id = 0;
    pthread_mutex_lock(&A.lock);
    for (int i = 1; i < MAX_SOUNDS && !id; i++) {
        Snd *s = &A.snd[i];
        if (s->used) continue;
        s->used = true;
        s->pcm = malloc(sizeof(float) * m.frameCount);
        memcpy(s->pcm, m.data, sizeof(float) * m.frameCount);
        s->frames = (int)m.frameCount;
        s->rate = (int)m.sampleRate;
        s->pitch = 1;
        s->voices = voices < 1 ? 1 : voices > MAX_VOICES ? MAX_VOICES : voices;
        id = i;
    }
    pthread_mutex_unlock(&A.lock);
    UnloadWave(m);
    return id;
}

SfxrSound sfxr_sound_load(const char *path, int voices)
{
    if (!A.on || !path || !FileExists(path)) return 0;
    Wave w = LoadWave(path);
    SfxrSound id = sfxr_sound_from_wave(w, voices);
    UnloadWave(w);
    return id;
}

static Snd *snd(SfxrSound id) { return id > 0 && id < MAX_SOUNDS && A.snd[id].used ? &A.snd[id] : NULL; }

float sfxr_sound_seconds(SfxrSound id)
{
    Snd *s = snd(id);
    return s && s->rate ? (float)s->frames / (float)s->rate : 0;
}

void sfxr_sound_pitch(SfxrSound id, float pitch)
{
    Snd *s = snd(id);
    if (s) s->pitch = pitch;
}

// --- emitters --------------------------------------------------------------------------

static SfxrEmitter base_emitter(SfxrEmitKind kind, float volume)
{
    SfxrEmitter e = { 0 };
    e.kind = kind;
    e.pose.orientation = QuaternionIdentity();
    e.near = 1.0f;
    e.volume = volume;
    e.inner_deg = 60;
    e.outer_deg = 180;
    e.outer_gain = 0.2f;
    return e;
}

SfxrEmitter sfxr_emitter_point(Vector3 at, float volume)
{
    SfxrEmitter e = base_emitter(SFXR_EMIT_POINT, volume);
    e.pose.position = at;
    return e;
}

SfxrEmitter sfxr_emitter_cone(SfxrPose pose, float inner_deg, float outer_deg, float volume)
{
    SfxrEmitter e = base_emitter(SFXR_EMIT_CONE, volume);
    e.pose = pose;
    e.inner_deg = inner_deg;
    e.outer_deg = outer_deg;
    return e;
}

SfxrEmitter sfxr_emitter_line(Vector3 a, Vector3 b, float volume)
{
    SfxrEmitter e = base_emitter(SFXR_EMIT_LINE, volume);
    e.pose.position = a;
    e.end = b;
    return e;
}

SfxrEmitter sfxr_emitter_box(SfxrPose center, Vector3 half, float volume)
{
    SfxrEmitter e = base_emitter(SFXR_EMIT_BOX, volume);
    e.pose = center;
    e.half = half;
    return e;
}

SfxrEmitter sfxr_emitter_ambient(float volume) { return base_emitter(SFXR_EMIT_AMBIENT, volume); }

// Where an emitter's sound reaches you from: the nearest point of it.
Vector3 sfxr_emitter_nearest(const SfxrEmitter *e, Vector3 to)
{
    switch (e->kind) {
    case SFXR_EMIT_LINE: {
        Vector3 ab = Vector3Subtract(e->end, e->pose.position);
        float len2 = Vector3DotProduct(ab, ab);
        float t = len2 > 1e-9f ? Clamp(Vector3DotProduct(Vector3Subtract(to, e->pose.position), ab) / len2, 0, 1) : 0;
        return Vector3Add(e->pose.position, Vector3Scale(ab, t));
    }
    case SFXR_EMIT_BOX: {
        Vector3 l = sfxr_pose_apply_inv(e->pose, to);
        l = (Vector3){ Clamp(l.x, -e->half.x, e->half.x), Clamp(l.y, -e->half.y, e->half.y), Clamp(l.z, -e->half.z, e->half.z) };
        return sfxr_pose_apply(e->pose, l);
    }
    default: return e->pose.position;
    }
}

static float smooth01(float x) { x = Clamp(x, 0, 1); return x * x * (3 - 2 * x); }

SfxrEars sfxr_audio_ears(const SfxrEmitter *e)
{
    SfxrEars out = { { CENTER_LEVEL * e->volume, CENTER_LEVEL * e->volume }, { 0, 0 }, { 20000, 20000 } };
    if (e->kind == SFXR_EMIT_AMBIENT) return out;

    SfxrPose head = sfxr_head();
    Vector3 at = sfxr_emitter_nearest(e, head.position);
    Vector3 local = sfxr_pose_apply_inv(head, at);   // +X right, +Y up, -Z ahead
    float d = Vector3Length(local);

    // distance: full within `near`, then halving with each doubling; gone past `far`
    float near = e->near > 0 ? e->near : 1.0f;
    float g = d > near ? near / d : 1.0f;
    if (e->far > 0) g *= 1.0f - smooth01((d - e->far * 0.8f) / (e->far * 0.2f));

    // where it is around your head; it fades to "all round you" as you get
    // right up to (or inside) it, where a direction means nothing
    float spread = e->kind == SFXR_EMIT_BOX || e->kind == SFXR_EMIT_LINE ? 0.6f : 0.12f;
    float focus = smooth01(d / spread);
    Vector3 dir = d > 1e-4f ? Vector3Scale(local, 1.0f / d) : (Vector3){ 0, 0, -1 };
    float side = Clamp(dir.x, -1, 1) * focus;          // -1 left .. 1 right
    if (SFXR_BREAK(sfxr_audio_pan_flipped)) side = -side;
    float behind = fmaxf(0, dir.z) * focus;            // 0 in front .. 1 straight behind
    float lat = fabsf(side);

    // the three cues, for the ear on the far side
    float near_ear = 1.0f + NEAR_EAR_LIFT * lat, far_ear = 1.0f - (1.0f - FAR_EAR_LEVEL) * lat;
    float az = asinf(lat);
    float itd_ms = 1000.0f * HEAD_RADIUS / SPEED_SOUND * (az + sinf(az));   // Woodworth's formula
    float shadow_hz = 20000.0f * powf(SHADOW_HZ / 20000.0f, lat);

    // behind you: a little quieter and duller at both ears (the ear flaps face forward)
    float rear = 1.0f - 0.2f * behind, rear_hz = 20000.0f * powf(BEHIND_HZ / 20000.0f, behind);
    // far away: the air takes the top off (halving the top every 60 m)
    float air_hz = 20000.0f * powf(0.5f, d / 60.0f);

    // a cone: full volume in front of it, quieter and duller round its back
    float cone = 1.0f, cone_hz = 20000.0f;
    if (e->kind == SFXR_EMIT_CONE && d > 1e-4f && !SFXR_BREAK(sfxr_audio_cone_ignored)) {
        Vector3 facing = sfxr_pose_forward(e->pose);
        Vector3 to_you = Vector3Normalize(Vector3Subtract(head.position, at));
        float ang = acosf(Clamp(Vector3DotProduct(facing, to_you), -1, 1)) * RAD2DEG;
        float in = e->inner_deg * 0.5f, outer = fmaxf(e->outer_deg * 0.5f, in + 1.0f);
        float t = Clamp((ang - in) / (outer - in), 0, 1);
        cone = Lerp(1.0f, e->outer_gain, t);
        cone_hz = 20000.0f * powf(2500.0f / 20000.0f, t);
    }

    float base = CENTER_LEVEL * e->volume * g * rear * cone;
    int far_i = side < 0 ? 1 : 0;   // sound on the left: the right ear is the far one
    out.gain[1 - far_i] = base * near_ear;
    out.gain[far_i] = base * far_ear;
    out.delay_ms[far_i] = SFXR_BREAK(sfxr_audio_no_delay) ? 0 : itd_ms;
    float common = fminf(fminf(rear_hz, air_hz), cone_hz);
    out.cutoff_hz[1 - far_i] = common;
    out.cutoff_hz[far_i] = fminf(common, shadow_hz);
    return out;
}

// --- playing sounds --------------------------------------------------------------------------

static float coef(float hz) { return hz >= OPEN_HZ ? 1.0f : 1.0f - expf(-2.0f * PI * hz / (float)A.rate); }

// The ears' targets for a play (under the lock).
static void aim(Play *p)
{
    SfxrEars e = sfxr_audio_ears(&p->em);
    const Snd *s = &A.snd[p->snd];
    float pitch = p->step * (float)A.rate / (float)s->rate;   // source frames per second of output, over the rate
    for (int k = 0; k < 2; k++) {
        p->tg[k] = e.gain[k] * A.master;
        p->td[k] = e.delay_ms[k] * 0.001f * (float)s->rate * pitch;
        p->tc[k] = coef(e.cutoff_hz[k]);
    }
    if (p->fresh) for (int k = 0; k < 2; k++) { p->g[k] = p->tg[k]; p->d[k] = p->td[k]; p->c[k] = p->tc[k]; }
}

static Play *playing(SfxrPlaying h)
{
    int i = (h & 0xFF) - 1;
    if (i < 0 || i >= MAX_PLAYS) return NULL;
    Play *p = &A.play[i];
    return p->snd && p->gen == ((unsigned)h >> 8) ? p : NULL;
}

SfxrPlaying sfxr_sound_emit(SfxrSound id, const SfxrEmitter *e, bool loop)
{
    Snd *s = snd(id);
    if (!A.on || !s || !e) return 0;
    pthread_mutex_lock(&A.lock);
    // a slot: past this sound's voice count, its oldest play; else a free
    // one; else the oldest of all, preferring one-shots over loops
    int mine = 0, oldest_mine = -1, free_i = -1, oldest = -1;
    for (int i = 0; i < MAX_PLAYS; i++) {
        Play *p = &A.play[i];
        if (!p->snd) { if (free_i < 0) free_i = i; continue; }
        if (p->snd == id) { mine++; if (oldest_mine < 0 || p->order < A.play[oldest_mine].order) oldest_mine = i; }
        if (oldest < 0 || (p->loop < A.play[oldest].loop) || (p->loop == A.play[oldest].loop && p->order < A.play[oldest].order)) oldest = i;
    }
    int i = mine >= s->voices ? oldest_mine : free_i >= 0 ? free_i : oldest;
    Play *p = &A.play[i];
    unsigned gen = p->gen + 1;
    memset(p, 0, sizeof *p);
    p->snd = id;
    p->gen = gen & 0xFFFFFF;
    p->order = ++A.starts;
    p->em = *e;
    p->loop = loop;
    p->step = s->pitch * (float)s->rate / (float)A.rate;
    p->fresh = true;
    aim(p);
    p->fresh = false;
    pthread_mutex_unlock(&A.lock);
    return (SfxrPlaying)((p->gen << 8) | (unsigned)(i + 1));
}

void sfxr_playing_move(SfxrPlaying h, const SfxrEmitter *e)
{
    pthread_mutex_lock(&A.lock);
    Play *p = playing(h);
    if (p && e) p->em = *e;
    pthread_mutex_unlock(&A.lock);
}

void sfxr_playing_stop(SfxrPlaying h)
{
    pthread_mutex_lock(&A.lock);
    Play *p = playing(h);
    if (p && A.device) p->stop = true;   // the audio thread fades it out over its next block
    else if (p) p->snd = 0;              // offline: nothing is mixing it
    pthread_mutex_unlock(&A.lock);
}

bool sfxr_playing_on(SfxrPlaying h)
{
    pthread_mutex_lock(&A.lock);
    bool on = playing(h) != NULL;
    pthread_mutex_unlock(&A.lock);
    return on;
}

void sfxr_sound_play(SfxrSound s, Vector3 at, float volume)
{
    SfxrEmitter e = sfxr_emitter_point(at, volume);
    sfxr_sound_emit(s, &e, false);
}

void sfxr_sound_play_here(SfxrSound s, float volume)
{
    SfxrEmitter e = sfxr_emitter_ambient(volume);
    sfxr_sound_emit(s, &e, false);
}

// A source sample at a fractional position (silence outside it, unless looping).
static inline float sample(const Snd *s, double pos, bool loop)
{
    if (loop) { pos = fmod(pos, (double)s->frames); if (pos < 0) pos += s->frames; }
    else if (pos < 0 || pos >= (double)s->frames) return 0;
    int i = (int)pos;
    float f = (float)(pos - (double)i);
    float a = s->pcm[i];
    float b = i + 1 < s->frames ? s->pcm[i + 1] : loop ? s->pcm[0] : 0;
    return a + (b - a) * f;
}

void sfxr_audio_mix(float *out, int frames)
{
    if (!A.on || frames <= 0 || A.paused) return;   // paused: silence, and nothing moves on
    pthread_mutex_lock(&A.lock);
    float inv = 1.0f / (float)frames;
    for (int i = 0; i < MAX_PLAYS; i++) {
        Play *p = &A.play[i];
        if (!p->snd) continue;
        const Snd *s = &A.snd[p->snd];
        float tg[2] = { p->stop ? 0 : p->tg[0], p->stop ? 0 : p->tg[1] };
        float dg[2], dd[2], dc[2];
        for (int k = 0; k < 2; k++) {   // glide from where it was to the targets across the block
            dg[k] = (tg[k] - p->g[k]) * inv;
            dd[k] = (p->td[k] - p->d[k]) * inv;
            dc[k] = (p->tc[k] - p->c[k]) * inv;
        }
        for (int f = 0; f < frames; f++) {
            for (int k = 0; k < 2; k++) {
                p->g[k] += dg[k]; p->d[k] += dd[k]; p->c[k] += dc[k];
                float x = sample(s, p->cursor - (double)p->d[k], p->loop);
                p->lp[k] += (x - p->lp[k]) * p->c[k];   // one-pole low-pass: the muffling
                out[f * 2 + k] += p->lp[k] * p->g[k];
            }
            p->cursor += p->step;
        }
        float lag = fmaxf(p->d[0], p->d[1]);
        if (p->stop || (!p->loop && p->cursor - (double)lag >= (double)s->frames)) p->snd = 0;   // done
        else if (p->loop && p->cursor >= (double)s->frames) p->cursor -= (double)s->frames;
    }
    pthread_mutex_unlock(&A.lock);
}

// --- music -----------------------------------------------------------------------

void sfxr_music_stop(void)
{
    if (!A.music_on) return;
    StopMusicStream(A.music);
    UnloadMusicStream(A.music);
    A.music_on = false;
    A.music_path[0] = 0;
}

bool sfxr_music_play(const char *path, bool loop, float volume)
{
    if (!A.device || !path || !FileExists(path)) return false;
    if (A.music_on && !strcmp(A.music_path, path) && IsMusicStreamPlaying(A.music)) {   // already playing it
        A.music_volume = volume;
        return true;
    }
    sfxr_music_stop();
    A.music = LoadMusicStream(path);
    if (!A.music.frameCount) return false;
    A.music.looping = loop;
    A.music_on = true;
    A.music_loop = loop;
    A.music_volume = volume;
    A.music_fade = 0;
    snprintf(A.music_path, sizeof A.music_path, "%s", path);
    SetMusicVolume(A.music, 0);
    PlayMusicStream(A.music);
    return true;
}

void sfxr_music_volume(float volume) { A.music_volume = volume; }

static void music_update(void)
{
    if (!A.music_on) return;
    UpdateMusicStream(A.music);   // streams keep playing only if fed every frame
    A.music_fade = fminf(1, A.music_fade + sfxr_dt() / 0.5f);
    SetMusicVolume(A.music, A.music_volume * A.music_fade * A.master);
    if (!A.music_loop && !IsMusicStreamPlaying(A.music)) sfxr_music_stop();
}

void sfxr_audio_update(void)
{
    if (!A.on) return;
    music_update();
    // every playing sound's ears, from where your head is now
    pthread_mutex_lock(&A.lock);
    for (int i = 0; i < MAX_PLAYS; i++)
        if (A.play[i].snd) aim(&A.play[i]);
    pthread_mutex_unlock(&A.lock);
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
