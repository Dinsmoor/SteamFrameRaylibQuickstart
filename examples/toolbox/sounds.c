// sounds.c - the toolbox's sounds, all made in code (sfxr_wave_synth): no
// sound files to ship or license. vrui's clicks, ticks and stops play from
// where they happen (vrui_style()->sound), and the stations and Daddy Bug
// Smasher use the rest. docs/AUDIO.md.

#include "toolbox.h"
#include "sfxr_audio.h"

static SfxrSound S[SND_COUNT];

void sounds_init(void)
{
    if (!sfxr_audio_init()) return;   // no audio device: every play below is a no-op
    //                                   freq  end   secs  attack decay  noise harm  volume
    S[SND_CLICK]  = sfxr_sound_synth(&(SfxrSynth){ 1800, 1200, 0.04f, 0.001f, 0.012f, 0.5f, 0,    0.45f }, 4);
    S[SND_TICK]   = sfxr_sound_synth(&(SfxrSynth){ 2600, 2400, 0.02f, 0,      0.006f, 0.2f, 0,    0.3f  }, 6);
    S[SND_STOP]   = sfxr_sound_synth(&(SfxrSynth){ 130,  70,   0.14f, 0.002f, 0.045f, 0.4f, 0,    0.7f  }, 3);
    S[SND_BELL]   = sfxr_sound_synth(&(SfxrSynth){ 880,  880,  1.6f,  0.002f, 0.5f,   0,    0.5f, 0.55f }, 3);
    S[SND_CHIME]  = sfxr_sound_synth(&(SfxrSynth){ 660,  660,  0.7f,  0.004f, 0.22f,  0,    0.3f, 0.6f  }, 3);
    S[SND_THUMP]  = sfxr_sound_synth(&(SfxrSynth){ 95,   45,   0.25f, 0.002f, 0.08f,  0.3f, 0,    0.9f  }, 4);
    S[SND_SQUISH] = sfxr_sound_synth(&(SfxrSynth){ 300,  90,   0.35f, 0.004f, 0.12f,  0.85f, 0,   0.8f  }, 6);
    S[SND_CHOMP]  = sfxr_sound_synth(&(SfxrSynth){ 180,  60,   0.18f, 0.001f, 0.05f,  0.6f, 0,    0.9f  }, 3);
    S[SND_WHOOSH] = sfxr_sound_synth(&(SfxrSynth){ 300,  1400, 0.3f,  0.06f,  0.12f,  0.8f, 0,    0.4f  }, 2);
    S[SND_TRILL]  = sfxr_sound_synth(&(SfxrSynth){ 240,  520,  0.9f,  0.02f,  0.5f,   0.1f, 0.6f, 0.8f  }, 2);
    S[SND_BLIP]   = sfxr_sound_synth(&(SfxrSynth){ 1200, 1600, 0.08f, 0.002f, 0.03f,  0,    0,    0.4f  }, 2);
    vrui_style()->sound = sounds_vrui;
}

void sound_play(SoundId id, Vector3 at, float volume) { if (id < SND_COUNT) sfxr_sound_play(S[id], at, volume); }
void sound_play_here(SoundId id, float volume) { if (id < SND_COUNT) sfxr_sound_play_here(S[id], volume); }
void sound_pitch(SoundId id, float pitch) { if (id < SND_COUNT) sfxr_sound_pitch(S[id], pitch); }
int sound_handle(SoundId id) { return id < SND_COUNT ? S[id] : 0; }

// vrui's sounds: quiet and short, from where the hand or laser acts
void sounds_vrui(VruiSound kind, Vector3 at, float strength)
{
    switch (kind) {
    case VRUI_SOUND_CLICK: sound_play(SND_CLICK, at, 0.8f); break;
    case VRUI_SOUND_TICK:  sound_play(SND_TICK, at, Clamp(0.3f + strength * 2, 0.3f, 0.9f)); break;
    case VRUI_SOUND_STOP:  sound_play(SND_STOP, at, Clamp(0.4f + strength, 0.4f, 1.0f)); break;
    }
}
