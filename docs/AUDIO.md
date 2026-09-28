# Sound and voice

Sound in VR does a job it doesn't on a screen: it tells you **where** things are, including
behind you. This quickstart gives you positional sounds, sounds made in code (so the toolbox
ships no sound files), the microphone, and **voice commands**: hold a button, say a word, and
your app gets it back as one of your commands. Two toolbox stations at the left end of the
row show it: **Sound** (five kinds of emitter, and what each ear gets) and **Voice
commands** (one set of orders bound four ways). Daddy Bug Smasher uses both.

## Positional sound (`sfxr_audio.h`)

```c
sfxr_audio_init();                                  // once (no audio device: it runs offline, silently)
SfxrSound bell = sfxr_sound_load("bell.wav", 4);    // up to 4 at once: it can overlap itself
sfxr_sound_play(bell, lamp_position, 1.0f);         // heard from the lamp
sfxr_sound_play_here(click, 0.5f);                  // not positional: UI, the player's own sounds
```

### How you hear where a sound is
The first version only **panned** sounds (turned one ear down), the way raylib does. In the
headset, left and right were hard to tell apart even with the sound right beside you. Ears
use three cues, and sfxr now gives every positional sound all three, separately for each
ear:

| Cue | A sound straight to your left | sfxr's number |
|---|---|---|
| **Level** | the right ear hears it quieter: your head is in the way | far ear a quarter (-12 dB), near ear 30% up |
| **Time** | the right ear hears it later | up to 0.66 ms (Woodworth's formula, a 8.75 cm head) |
| **Tone** | the right ear hears it duller: a head blocks high notes more than low ones | far ear muffled above 1.8 kHz |

Time is the strongest of the three for anything with a sharp start (a click, a chime, a
footstep), and it's the one panning can't give. On top of that:
- **behind you** it's a little quieter and duller at both ears (above 6 kHz is cut straight
  behind): the ear flaps face forward
- **distance** fades it (full within `near`, 1 m by default; half at twice that) and the air
  dulls it a little at tens of meters
- **everything is kept up to date** as you turn your head: a sound on your left stays on your
  left when you look away. The audio thread glides to each new setting across a block, so a
  sound moving past you never clicks.

It isn't a full HRTF (no ear-shape filtering, so up and down stay weak), but it's cheap: about
64 sounds at once for a fraction of a millisecond of audio-thread time.

**How it's built.** sfxr keeps each sound's samples itself (mono: a sound comes from one
place) and mixes them in `sfxr_audio_mix`, which raylib calls on its audio thread as a
"mixed audio processor"; music still streams through raylib. `sfxr_audio_ears(&emitter)`
returns what each ear gets (gain, delay, cutoff), for your own debug views and for tests.
Without an audio device (a test machine) it runs **offline**: sounds load and play, silently,
and a test can call `sfxr_audio_mix` to hear what you would.

### Emitters: where a sound comes from
A sound at a point is only one shape. `SfxrEmitter` says how a sound fills space, and
`sfxr_sound_emit(sound, &emitter, loop)` plays it (the handle it returns moves or stops a
loop):

| Kind | Made with | Heard | For |
|---|---|---|---|
| **point** | `sfxr_emitter_point(at, volume)` | the same all round | a bell, a bug, a footstep |
| **cone** | `sfxr_emitter_cone(pose, inner_deg, outer_deg, volume)` | full inside `inner_deg` of the way it faces (its pose's -Z), fading to `outer_gain` (0.2) and **muffled** past `outer_deg` | a speaker, a radio, a megaphone, someone talking |
| **line** | `sfxr_emitter_line(a, b, volume)` | from its nearest point to you | a stream, a road, a conveyor belt |
| **box** | `sfxr_emitter_box(center, half, volume)` | from its nearest point outside; **all round you** inside | rain on a roof, a room's hum, a waterfall's spray |
| **ambient** | `sfxr_emitter_ambient(volume)` | the same in both ears, from nowhere | wind, UI, your own sounds |

```c
SfxrEmitter radio = sfxr_emitter_cone(radio_pose, 60, 220, 0.8f);
SfxrPlaying p = sfxr_sound_emit(tune, &radio, true);   // loop
radio.pose = radio_pose;  sfxr_playing_move(p, &radio);   // each frame it moves
sfxr_playing_stop(p);                                       // fades out in a few ms
```

Every emitter also has `near` (full volume within it, 1 m by default; half at twice) and
`far` (silent beyond; 0 = never). Line and box emitters spread over you as you get within
0.6 m, where a direction stops meaning anything; a point spreads only within 12 cm.

**Sounds made in code, as loops.** The Sound station's radio tune, stream, rain and wind are
made at startup from a few lines each (`make_tune`, `make_stream`... in `station_sound.c`):
bells on a timeline, filtered noise with bubbles, a hiss with drops, noise with gusts. A loop
needs no click where it wraps: each one's last 0.3 s is blended into its start.

**Music.** `sfxr_music_play(path, loop, volume)` streams one track at a time (ogg, mp3,
wav), fading it in over half a second; `sfxr_music_volume` ducks it (Daddy Bug Smasher
lowers it under the Bugmaster's voice). Music isn't positional: it's not *in* the world.

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

**Tested** (`tests/voice`):
- `sound-from-the-left-is-on-the-left`: louder, sooner and brighter in the left ear; a
  quarter as loud at 4 m as at 1 m; duller behind. Its break switch is
  `sfxr_audio_pan_flipped`, a real bug in every first attempt at panning.
- `click-reaches-the-near-ear-first`: renders a click a meter to the left and slides the
  channels over each other: the right one lags by about 31 frames (0.66 ms at 48 kHz).
  Break switch `sfxr_audio_no_delay`.
- `cones-face-and-lines-follow`: behind a speaker it's under a third as loud and muffled; a
  line is heard from its nearest point. Break switch `sfxr_audio_cone_ignored`.
- `tests/toolbox/sound.sfxt`: poking the Sound station's RADIO switch starts its loop, and
  poking it again stops it.

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

### Binding a command
The same commands can be given many ways, and which suits depends on the game. The **Voice
commands** station binds one set of orders (attack, return, stop, to three little bugs)
four ways, side by side:

| # | Binding | How | Good for | Cost |
|---|---|---|---|---|
| 1 | **Point + bumper** (in context) | point at a bug: it says "hold the bumper". Hold it, speak, let go | ordering *this* one | the bumper talks only while pointing at a bug, so it's free for other things everywhere else |
| 2 | **Point + A: a ring menu** (in context) | point at a bug, hold A, tilt the stick to an order, let go | no voice at all: noisy rooms, no recognizer, speed once learned | A opens *this* ring on a bug and the toolbox's own ring menu anywhere else: the same button, chosen by what you point at |
| 3 | **A button in the world** (an intercom) | hold TALK on the table, speak | orders for everyone; nothing to aim | you have to be at it, and a hand is on it |
| 4 | **Hands-free** | flip the switch, say "bugs, attack" | hands busy (a sword, a steering wheel) | the microphone hears everything, so a wake word ("bugs") gates it, and a pause ends it |

And a **global** binding, for comparison: in Daddy Bug Smasher the bumper means "listen"
everywhere, for "hammer" and "restart".

Context bindings are what make one controller go a long way: *what you point at* picks what
a button does. Show the binding where the player is looking (the pointed-at bug says "hold
the bumper") and they never have to remember it. In code it's an `if`: offer the ring menu
only while `pointed >= 0`, and let an open ring claim the hand
(`vrui_claim_input`), which the toolbox's own ring checks before opening
(`station_menus.c`).

**Hands-free** is `sfxr_voice_hands_free(true, threshold, pause_s)`: a speech detector on
20 ms blocks starts a clip after 60 ms above the threshold (keeping the 0.3 s before it),
and a pause ends it; a clip also ends at 6 s. `sfxr_voice_hands_free_heard()` says a
transcript came from it rather than a button, so the station can insist on the wake word.
Test: `voice/hands-free-speech-starts-and-a-pause-ends` (room noise starts nothing; speech
does; a pause ends it), break switch `sfxr_voice_any_sound_starts`. The ring menus' tests
are `tests/toolbox/orders.sfxt`.

### Why it's built this way

- **Push-to-talk by default.** Holding a button while speaking means other people in the room, and
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
  the floor. It still loops now and then on the Frame ("return return return return"), so
  sfxr keeps one of each run of repeated words or short phrases before you see the text.
- **A shim, loaded at run time.** whisper.cpp's parameter struct changes between releases, so
  sfxr talks to a three-function C shim (`tools/speech/sfq_speech.c`) compiled against the
  whisper.h it ships with, and loads it with `dlopen`.

**On the Frame:** audio plays through PulseAudio (PipeWire), the microphone opens as "Built-in
Audio Capture", and the recognizer and model load in about 0.2 s at startup.

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
3. **Beamforming:** it needs the raw mic channels rather than one processed stream. **On the
   Frame (checked 2026-09-28):** `pactl list sources` shows the microphone as a
   **2-channel** 48 kHz PipeWire source (`HiFi__Mic__source`), plus a monitor of the
   speakers, and no echo-cancel source. So there are at least two mics to compare; a
   simple two-mic trick (the wearer's mouth is equidistant from both) might work. sfxr
   captures mono today, which averages the channels. Not tried yet.

Also watch which source is the default. On a machine without a microphone, the default
"capture" can be the monitor of your own speakers (the log says which: "microphone: on
(...)"), and then the app hears itself.

## In the toolbox and the game

- **Sound station:** five switches along the table's front, all off to start (a sound that
  repeats wears thin), one per kind of emitter:
  - CHIME, a point: the speaker box chimes from wherever you carry it
  - RADIO, a cone: walk round behind it, or turn it away from you, and it goes quiet and
    muffled
  - STREAM, a line: the stream on the floor across the aisle; walk along it and it stays
    beside you, step over it and it swaps ears
  - RAIN, a box: the canopy in the aisle; step under it and it's all round you
  - WIND, ambient: the same in both ears
  The panel above shows what each ear gets from each emitter that's on: how loud, how much
  later, how muffled.
- **Voice commands station:** the four bindings above, three little bugs and a cardboard
  Daddy. Pointing plus voice is the Bugmaster's controls in the planned Revenge of the
  Bugmaster, in miniature. The panel's buttons give the same orders without a recognizer.
- **Daddy Bug Smasher:**
  - smashes, hits and bites come from where they happen
  - the Bugmaster shouts from the top of his tower
  - hold the bumper and say **hammer** (it comes back to your belt) or **restart**
  - the original game's recordings: the bugs' death splats, scurrying, dings, and the
    Bugmaster's voice (`resources/garden/sfx/` and `voice/`, converted to mono so they can be
    positioned). The subtitles over his tower are what Whisper heard in his lines.
  - its music, made by the author and their kids: the menu tune while you pick up the
    hammer, combat while the bugs come, win or lose at the end

## Tests

`make test T=voice`, with a fake recognizer (`tests/voice/fake/sfq_speech.c`) that has the
shim's three functions and "hears" whatever `FAKE_SPEECH` says:

| Case | Proves | Break switch |
|---|---|---|
| push-to-talk-recognizes | the clip reaches the background thread with the command words, is padded to over a second, and matches | `sfxr_voice_no_prompt` |
| match-is-forgiving | near misses and extra words match; tiny words and non-commands don't | `sfxr_voice_match_exact` |
| sound-from-the-left-is-on-the-left | each ear's level, time and tone; distance; behind you | `sfxr_audio_pan_flipped` |
| click-reaches-the-near-ear-first | the mixer, rendered: the far ear 0.66 ms late | `sfxr_audio_no_delay` |
| cones-face-and-lines-follow | a speaker is quiet behind; a line is heard from its nearest point | `sfxr_audio_cone_ignored` |
| hands-free-speech-starts-and-a-pause-ends | room noise starts nothing, speech does, a pause ends it | `sfxr_voice_any_sound_starts` |
| repeats-are-collapsed | Whisper's loops come back as one of each | `sfxr_voice_keeps_repeats` |
