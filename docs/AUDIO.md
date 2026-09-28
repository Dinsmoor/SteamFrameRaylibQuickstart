# Sound and voice

Sound in VR does a job it doesn't on a screen: it tells you **where** things are, including
behind you. This quickstart gives you positional sounds, sounds made in code (so the toolbox
ships no sound files), the microphone, and **voice commands**: hold a button, say a word, and
your app gets it back as one of your commands. The toolbox's **Sound & voice** station (left
end of the row) shows all of it, and Daddy Bug Smasher uses it.

## Positional sound (`sfxr_audio.h`)

```c
sfxr_audio_init();                                  // once; false = no audio device, and nothing breaks
SfxrSound bell = sfxr_sound_load("bell.wav", 4);    // 4 voices: it can overlap itself 4 times
sfxr_sound_play(bell, lamp_position, 1.0f);         // heard from the lamp
sfxr_sound_play_here(click, 0.5f);                  // not positional: UI, the player's own sounds
```

- **Where it comes from:** each playing sound is panned by where it is across your view, and
  faded by distance (full within a meter, then 1/distance). It's a little quieter behind you,
  where your head is in the way.
- **Kept up to date:** every frame, as you turn your head. A sound on your left stays on your
  left when you look away.
- **Not a full HRTF:** it's a cheap model (no filtering by ear shape, no height cues), but it
  tells you which way to turn, which is what games need. Pan never goes all the way to one
  ear, because real sounds reach both.
- **Voices:** each sound gets a few copies (raylib sound aliases sharing one buffer), so a
  click can overlap itself. The oldest is reused when all are busy.

**Sounds made in code.** `sfxr_sound_synth(&(SfxrSynth){ freq, freq_end, seconds, attack,
decay, noise, harmonic, volume }, voices)` makes a tone or noise burst with an envelope:
- a click is a short, noisy high blip
- a thump is a falling low tone
- a bell is a slow-decaying tone with an inharmonic partial
- a squish is filtered noise sliding down

`examples/toolbox/sounds.c` makes all the toolbox's sounds this way. There's nothing to
license and nothing to ship.

**vrui's sounds.** Set `vrui_style()->sound` and vrui calls it, with a position, for
- clicks: grabs, presses, panel buttons
- detent ticks: a knob's notches, a valve's quarter turns
- end stops

vrui never plays audio itself, so it stays free of any audio library. The app decides what
things sound like.

**Tested:** `tests/voice/sound-from-the-left-is-on-the-left`, proved by the break switch
`sfxr_audio_pan_flipped`, a real bug in every first attempt at panning.

## Voice commands (`sfxr_voice.h`)

```c
static const char *const CMDS[] = { "attack", "return", "stop" };
sfxr_voice_init(NULL);                            // the recognizer and the microphone
sfxr_voice_set_prompt("attack, return, stop");    // the words to expect
if (bumper.pressed)  sfxr_voice_listen_begin();   // push to talk
if (bumper.released) sfxr_voice_listen_end();     // recognized in the background
char said[128];
if (sfxr_voice_result(said, sizeof said))         // a moment later
    switch (sfxr_voice_match(said, CMDS, 3)) { ... }
```

The recognizer is **Whisper**, through [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
with its smallest model: `ggml-tiny.en-q5_1.bin`. That's "tiny", English only and 5-bit
quantized, 31 MB. It's plenty for a handful of command words.

**Getting it.** It isn't in the repo:

```
scripts/get-speech.sh           # for this machine: build/host-speech/libsfq_speech.so + the model
scripts/get-speech.sh frame     # for the headset, in the Steam Runtime SDK container
make package EX=toolbox         # copies both next to the app when they're there
build/host-debug/bin/sfq_listen said.wav attack,return,stop    # try it on a recording
build/host-debug/bin/sfq_listen --mic 2                        # or on your microphone
```

Without it, every voice call is a harmless "no", like Steam: the same build runs anywhere,
and the status line says how to get it.

### Why it's built this way

- **Push-to-talk.** Holding a button while speaking means other people in the room, and
  the game's own sounds, can't give commands. It also means recognition runs once, on a
  short clip, instead of listening all the time, which saves battery and CPU. It keeps
  0.3 s from before the press, because people start talking as they press.
- **Never on the frame loop's thread.** Recognition runs on a background thread; the frame is
  14 ms and recognition takes longer.
- **Tell it the words.** `sfxr_voice_set_prompt` passes your commands to Whisper as a prompt.
  Short commands are exactly what it mishears without context. The break switch
  `sfxr_voice_no_prompt` and its test hold this in place.
- **Forgiving matching.** `sfxr_voice_match` ignores case and punctuation, and finds the
  command among other words ("uh, attack!"). It accepts a letter off for short words and
  two for long ones ("attach" is attack). Tiny words must be exact ("no" isn't "go"). When
  two commands are in one sentence, the first one said wins.
- **A short window for short clips.** Whisper works on 30-second windows. The shim tells it
  the clip is short, which makes it about three times faster, but below 512 encoder
  positions the tiny model starts repeating itself ("attack attack attack..."), so 512 is
  the floor.
- **A shim, loaded at run time.** whisper.cpp's parameter struct changes between releases, so
  sfxr talks to a three-function C shim (`tools/speech/sfq_speech.c`) compiled against the
  whisper.h it ships with, and loads it with `dlopen`.

**Measured on the build machine** (an ARM64 workstation, 4 threads), on synthesized speech:
"attack", "stop" and "return to the tower" were each recognized correctly in 115–135 ms.
"go away" matched no command, correctly. **On the Frame it isn't measured yet**; expect a few
hundred milliseconds. Try `sfq_listen` on the headset through `frame.sh exec`.

### Can the headset tell the wearer's voice from the room?

Not that we know of.
- **OpenXR:** it has no API for where a sound came from.
- **Valve:** no "wearer's voice" signal is documented for the Frame.
- **SteamOS:** it captures the mic array through PipeWire, possibly with echo cancellation.

What works:
1. **Push-to-talk:** the reason it's the default here.
2. **Loudness:** the wearer's voice is much louder than anyone else's, because the mics are
   centimeters from the mouth. A level gate helps (`sfxr_voice_level`).
3. **Beamforming:** only if the headset exposes the raw mic channels rather than one processed
   stream. Check with `scripts/frame.sh exec 'pactl list sources'` (the channel count, and any
   echo-cancel source).

Also watch which source is the default. On a machine without a microphone, the default
"capture" can be the monitor of your own speakers (the log says which: "microphone: on
(...)"), and then the app hears itself.

## In the toolbox and the game

- **Sound & voice station:**
  - the speaker chimes from wherever you carry it
  - the TALK button is push-to-talk in the world: hold it, speak, and the transcript shows on
    the board above it
  - three little bugs and a cardboard Daddy: point at a bug with the right laser, hold the
    bumper, and say **attack**, **return** or **stop**. That's pointing plus voice, the
    Bugmaster's controls in the planned Revenge of the Bugmaster, in miniature. The panel's
    buttons do the same without a recognizer.
- **Daddy Bug Smasher:**
  - smashes, hits and bites come from where they happen
  - the Bugmaster shouts from the top of his tower
  - hold the bumper and say **hammer** (it comes back to your belt) or **restart**
  - the original game's recordings play if you copy them into
    `examples/toolbox/resources/garden/sfx/` (death1–3, scurry, high/low ding) and `voice/`
    (bugmaster_intro/win/lose, taunt1–4). They aren't in the public repo.

## Tests

`make test T=voice`, with a fake recognizer (`tests/voice/fake/sfq_speech.c`) that has the
shim's three functions and "hears" whatever `FAKE_SPEECH` says:

| Case | Proves | Break switch |
|---|---|---|
| push-to-talk-recognizes | the clip reaches the background thread with the command words, is padded to over a second, and matches | `sfxr_voice_no_prompt` |
| match-is-forgiving | near misses and extra words match; tiny words and non-commands don't | `sfxr_voice_match_exact` |
| sound-from-the-left-is-on-the-left | panning and distance | `sfxr_audio_pan_flipped` |
