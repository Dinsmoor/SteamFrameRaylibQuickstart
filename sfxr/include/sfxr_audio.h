// sfxr_audio.h - sound in VR: sounds that come from where things are, sounds
// made in code (no files needed), and the microphone. docs/AUDIO.md.
//
//   sfxr_audio_init();                                   // once, after sfxr_init
//   SfxrSound bell = sfxr_sound_load("bell.wav", 4);     // 4 voices: up to 4 at once
//   sfxr_sound_play(bell, lamp_position, 1.0f);          // heard from the lamp
//
// Positional sounds are panned and faded from where your head is, and kept
// up to date as you turn (sfxr_frame_begin does that), so a sound on your
// left stays on your left when you look away. It's a cheap model -- pan,
// distance, a little muffling behind you -- not a head-related transfer
// function, but it tells you where things are, which is what matters.
//
// Without an audio device (a headless test machine), every call is a
// harmless no-op, like sfxr_steam.h.

#ifndef SFXR_AUDIO_H
#define SFXR_AUDIO_H

#include "raylib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sfxr_audio_init(void);          // opens the speakers; false: no audio (nothing breaks)
void sfxr_audio_shutdown(void);      // sfxr_shutdown() calls this too
bool sfxr_audio_on(void);
void sfxr_audio_volume(float master);// 0..1 (default 1)

typedef int SfxrSound;               // 0 = none (a missing file, no audio)

// `voices`: how many copies can play at once (a click that can overlap itself).
SfxrSound sfxr_sound_load(const char *path, int voices);
SfxrSound sfxr_sound_from_wave(Wave wave, int voices);          // copies the wave

// Play at a world position (panned and faded from your head), or "here":
// not positional (the player's own sounds, UI).
void sfxr_sound_play(SfxrSound s, Vector3 at, float volume);
void sfxr_sound_play_here(SfxrSound s, float volume);
void sfxr_sound_pitch(SfxrSound s, float pitch);   // for the next plays (1 = as recorded)
float sfxr_sound_seconds(SfxrSound s);            // how long it lasts (0: no such sound)

// How a sound at `at` comes out: pan -1 (left) .. 1 (right), and gain 0..1.
// (What sfxr_sound_play uses; exposed for your own mixing and for tests.)
void sfxr_audio_spatial(Vector3 at, float *pan, float *gain);

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
