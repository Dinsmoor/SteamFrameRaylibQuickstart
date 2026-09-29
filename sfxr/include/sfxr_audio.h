// sfxr_audio.h - sound in VR: sounds that come from where things are, sounds
// made in code (no files needed), and the microphone. docs/AUDIO.md.
//
//   sfxr_audio_init();                                   // once, after sfxr_init
//   SfxrSound bell = sfxr_sound_load("bell.wav", 4);     // 4 voices: up to 4 at once
//   sfxr_sound_play(bell, lamp_position, 1.0f);          // heard from the lamp
//
// Positional sounds reach each ear the way a real one would: quieter, a
// fraction of a millisecond later and duller at the ear on the far side of
// your head, duller behind you and far away. That's what tells you left
// from right; panning alone (turning one ear down) barely does in a headset.
// They're kept up to date as you turn (sfxr_frame_begin does that), so a
// sound on your left stays on your left when you look away. Not a full
// head-related transfer function (up and down are weak), but cheap, and it
// tells you where things are.
//
// Where a sound comes from is an emitter: a point (a bell), a cone (a
// speaker: loud in front, quiet and muffled round the back), a line (a
// stream), a box (rain on a roof: all round you inside it) or ambient (from
// nowhere: UI, your own sounds).
//
//   SfxrEmitter e = sfxr_emitter_cone(radio_pose, 60, 200, 0.8f);
//   SfxrPlaying p = sfxr_sound_emit(song, &e, true);     // loops
//   ...each frame: e.pose = radio_pose; sfxr_playing_move(p, &e);
//
// Without an audio device (a headless test machine) sounds still load and
// "play", silently; sfxr_audio_mix renders them for a test to check.

#ifndef SFXR_AUDIO_H
#define SFXR_AUDIO_H

#include "raylib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sfxr_audio_init(void);          // opens the speakers (or runs offline without them)
void sfxr_audio_shutdown(void);      // sfxr_shutdown() calls this too
bool sfxr_audio_on(void);
void sfxr_audio_volume(float master);// 0..1 (default 1)
void sfxr_audio_pause(bool paused);  // everything (sounds and music) holds where it is, silent; e.g. while the player is away
bool sfxr_audio_paused(void);

typedef int SfxrSound;               // 0 = none (a missing file, no audio)

// `voices`: how many copies can play at once (a click that can overlap itself).
SfxrSound sfxr_sound_load(const char *path, int voices);
SfxrSound sfxr_sound_from_wave(Wave wave, int voices);          // copies the wave

// Play at a world position (from a point, heard all round), or "here":
// not positional (the player's own sounds, UI).
void sfxr_sound_play(SfxrSound s, Vector3 at, float volume);
void sfxr_sound_play_here(SfxrSound s, float volume);
void sfxr_sound_pitch(SfxrSound s, float pitch);   // for the next plays (1 = as recorded)
float sfxr_sound_seconds(SfxrSound s);            // how long it lasts (0: no such sound)

// --- emitters: where a sound comes from, and how it spreads ------------------------
typedef enum {
    SFXR_EMIT_POINT,    // from a point, the same all round: a bell, a bug
    SFXR_EMIT_CONE,     // from a point, loudest the way it faces (its pose's -Z): a speaker, a megaphone, a voice
    SFXR_EMIT_LINE,     // from the nearest point along a line: a stream, a road, a conveyor belt
    SFXR_EMIT_BOX,      // from the nearest point of a box, all round you inside it: rain on a roof, a room's hum
    SFXR_EMIT_AMBIENT,  // from nowhere in particular: not positional (UI, your own sounds, a bed of wind)
} SfxrEmitKind;

typedef struct {
    SfxrEmitKind kind;
    SfxrPose pose;       // where (a CONE faces its -Z; a BOX is centered and turned by it)
    Vector3  end;        // LINE: the other end (pose.position is the first)
    Vector3  half;       // BOX: half extents (m)
    float inner_deg;     // CONE: full volume within this angle (the whole cone, like a spotlight's)...
    float outer_deg;     // ...fading to outer_gain by this angle,
    float outer_gain;    // and this quiet (and muffled) outside it, round the back (default 0.2)
    float near;          // full volume within this distance (m, default 1), half at twice it, a third at three times...
    float far;           // silent beyond this (m; 0: never)
    float volume;        // 0..1
} SfxrEmitter;

SfxrEmitter sfxr_emitter_point(Vector3 at, float volume);
SfxrEmitter sfxr_emitter_cone(SfxrPose pose, float inner_deg, float outer_deg, float volume);
SfxrEmitter sfxr_emitter_line(Vector3 a, Vector3 b, float volume);
SfxrEmitter sfxr_emitter_box(SfxrPose center, Vector3 half, float volume);
SfxrEmitter sfxr_emitter_ambient(float volume);
Vector3     sfxr_emitter_nearest(const SfxrEmitter *e, Vector3 to);   // the point of it a sound at `to` hears

typedef int SfxrPlaying;   // one sound playing (0: none). Stale handles are safe: they do nothing.
SfxrPlaying sfxr_sound_emit(SfxrSound s, const SfxrEmitter *e, bool loop);
void sfxr_playing_move(SfxrPlaying p, const SfxrEmitter *e);   // it moved (or its volume changed)
void sfxr_playing_stop(SfxrPlaying p);                         // fades out over a few milliseconds
bool sfxr_playing_on(SfxrPlaying p);

// What each ear gets from an emitter, from where your head is now: what the
// mixer does with it (for your own mixing, a debug view, and tests).
typedef struct {
    float gain[2];       // left, right
    float delay_ms[2];   // the far ear hears it later (up to 0.66 ms)
    float cutoff_hz[2];  // high notes above this are muffled (head shadow, behind you, far away)
} SfxrEars;
SfxrEars sfxr_audio_ears(const SfxrEmitter *e);

// Renders the playing sounds, ADDED into `stereo` (interleaved left/right
// floats, `frames` of them at sfxr_audio_rate). The audio thread calls it;
// tests call it to hear what you would.
void sfxr_audio_mix(float *stereo, int frames);
int  sfxr_audio_rate(void);          // output frames per second
bool sfxr_audio_device(void);        // false: offline (no speakers; sounds play silently)

void sfxr_audio_update(void);        // called by sfxr_frame_begin: re-pans playing sounds

// --- music ------------------------------------------------------------------------
// One track at a time, streamed from its file (ogg, mp3, wav), not positional.
// A new track replaces the old one and fades in over half a second.
bool sfxr_music_play(const char *path, bool loop, float volume);   // false: no file / no audio
void sfxr_music_stop(void);
void sfxr_music_volume(float volume);   // e.g. duck it while someone speaks

// --- sounds made in code --------------------------------------------------------
// A tone or a noise burst with an envelope: clicks, ticks, thumps, bells,
// squishes, without shipping a single file.
typedef struct {
    float freq, freq_end;   // Hz at the start and end (a sweep); 0 = noise only
    float seconds;
    float attack;           // seconds to full volume
    float decay;            // seconds for the volume to fall to about a third (exponential)
    float noise;            // 0 pure tone .. 1 pure noise
    float harmonic;         // 0..1 of a second partial at 2.76x (bells)
    float volume;           // 0..1
} SfxrSynth;
Wave      sfxr_wave_synth(const SfxrSynth *s);     // 22050 Hz mono; UnloadWave it
SfxrSound sfxr_sound_synth(const SfxrSynth *s, int voices);

// --- the microphone ----------------------------------------------------------------
// 16 kHz mono float samples (what speech recognition wants), into a ring
// buffer a few seconds long. docs/AUDIO.md says what the Frame's mics need.
bool  sfxr_mic_start(void);          // false: no microphone
void  sfxr_mic_stop(void);
bool  sfxr_mic_on(void);
float sfxr_mic_level(void);          // loudness of the last ~50 ms, 0..1 (a meter)
int   sfxr_mic_read(float *out, int max_samples);   // samples since the last read (oldest dropped if you fall behind)

#ifdef __cplusplus
}
#endif

#endif
