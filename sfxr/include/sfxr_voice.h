// sfxr_voice.h - voice commands: hold a button, say a word, get it back as
// text and as one of your commands. docs/AUDIO.md.
//
//   static const char *const CMDS[] = { "attack", "return", "stop" };
//   sfxr_voice_init(NULL);                       // once; false: no recognizer (harmless)
//   sfxr_voice_set_prompt("attack, return, stop");
//   if (bumper.pressed)  sfxr_voice_listen_begin();
//   if (bumper.released) sfxr_voice_listen_end();   // recognition runs in the background
//   char said[128];
//   if (sfxr_voice_result(said, sizeof said)) {
//       int c = sfxr_voice_match(said, CMDS, 3);    // -1: none of them
//       ...
//   }
//
// The recognizer is Whisper (whisper.cpp, the tiny English model), loaded at
// run time from libsfq_speech.so built by scripts/get-speech.sh. Without it
// every call is a harmless "no", like sfxr_steam.h, so the same build runs
// everywhere. Push-to-talk is on purpose: it's what keeps other people in
// the room (and the game's own sounds) from giving your commands, and it
// lets recognition run once on a short clip instead of all the time.

#ifndef SFXR_VOICE_H
#define SFXR_VOICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Load the recognizer and open the microphone. model_path NULL: SFQ_SPEECH_MODEL,
// else next to the app (speech/ggml-tiny.en-q5_1.bin), else the source tree's
// external/speech/. The library: SFQ_SPEECH_LIB, else next to the app, else
// build/host-speech/. false: no voice (sfxr_voice_status() says why).
bool        sfxr_voice_init(const char *model_path);
void        sfxr_voice_shutdown(void);
bool        sfxr_voice_available(void);
const char *sfxr_voice_status(void);

// Words the recognizer should expect (it hears short commands much better
// with them). A comma-separated list works best: "attack, return, stop".
void sfxr_voice_set_prompt(const char *words);

void  sfxr_voice_listen_begin(void);   // start collecting what the microphone hears
void  sfxr_voice_listen_end(void);     // stop, and recognize it on a background thread
bool  sfxr_voice_listening(void);
bool  sfxr_voice_busy(void);           // recognizing
float sfxr_voice_level(void);          // the microphone's loudness now (0..1, for a meter)
float sfxr_voice_last_ms(void);        // how long the last recognition took

// A new transcript, once (true when there is one). Empty text: nothing was heard.
bool sfxr_voice_result(char *text, int size);

// Which of `commands` the text says (-1: none). Forgiving: case,
// punctuation, "attack!" or "Attack." or "uh, attack", and a letter or two
// wrong ("attach" is attack when attack is a command).
int  sfxr_voice_match(const char *text, const char *const *commands, int count);

// Hands-free: no button. Speech starts a clip (with the moment before it),
// a pause of `pause_s` ends it and recognizes it. `threshold` is the level
// (as sfxr_voice_level) that counts as speech. Anything loud enough starts
// one -- other people, the game's own sounds through open speakers -- so use
// a wake word ("bugs, attack") and ignore transcripts without it.
void sfxr_voice_hands_free(bool on, float threshold, float pause_s);
bool sfxr_voice_hands_free_on(void);
bool sfxr_voice_hands_free_heard(void);   // the current (or last) clip was started by speech, not a button

// For tests and tools: samples (16 kHz mono) as if the microphone heard them:
// into the clip while listening, else into the moment-before (and, hands-free,
// to the speech detector).
void sfxr_voice_feed(const float *pcm, int n);

// --- Design notes: talk to your tools from inside the headset (docs/NOTES.md) ---
//
// Not a command: a note. Hold a button, say what you want changed ("make a
// variant of this one that flies and spits arcs of slop while it hovers"),
// let go. The clip is saved next to the app, in notes/ (SFXR_NOTES_DIR), as
//   <id>.wav    16 kHz mono, with the 0.3 s from before the button
//   <id>.json   when and where: time, frame, head and hands, gaze if the
//               headset has it, and `context`: what the app says you meant
//               (any JSON: the widget you point at, the enemy under your
//               gaze, the level...), or null
//   <id>.png    a screenshot of that frame (both eyes on the headset)
// Nothing is recognized on the headset: the build machine pulls the notes
// (scripts/frame.sh notes), transcribes them with a big model and hands them
// to the coding agent with the context. Works without libsfq_speech.so; it
// needs only the microphone.
bool sfxr_note_begin(void);                    // false: no microphone
bool sfxr_note_end(const char *context_json);  // false: nothing was recording; context_json may be NULL
bool sfxr_note_recording(void);
float sfxr_note_seconds(void);                 // how long the current (or last) note is
const char *sfxr_note_last(void);              // the last saved note's id ("" if none)
int  sfxr_note_count(void);                    // saved this run

#ifdef __cplusplus
}
#endif

#endif
