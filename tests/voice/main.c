// tests/voice - sound and voice commands keep their promises (docs/AUDIO.md):
// a sound on your left is heard on your left, and quieter far away or behind
// you; push-to-talk runs the recognizer in the background with your command
// words; a short "attack" still gets heard; matching forgives the ways
// people and Whisper get words slightly wrong.
//
// The recognizer here is a fake with the real shim's three functions
// (tests/voice/fake/sfq_speech.c): it "hears" whatever FAKE_SPEECH says.
//
//   make test T=voice

#include "sfxt.h"
#include "sfxr_audio.h"
#include "sfxr_voice.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void scene(void) {}

static bool start_voice(const char *said)
{
    // the fake recognizer is built next to this test binary; any readable file will do as its "model"
    setenv("SFQ_SPEECH_LIB", "build/host-test/tests/libfake_sfq_speech.so", 1);
    setenv("SFQ_SPEECH_MODEL", "tests/voice/main.c", 1);
    setenv("FAKE_SPEECH", said, 1);
    bool ok = sfxr_voice_init(NULL);
    CHECK(ok, "the fake recognizer loads (%s)", sfxr_voice_status());
    return ok;
}

// Hold to talk, say something, let go: the text arrives a little later, from
// the background thread, with the command words passed along.
static void push_to_talk_recognizes(void)
{
    if (!start_voice("Attack!")) return;
    static const char *const CMDS[] = { "attack", "return", "stop" };
    sfxr_voice_set_prompt("attack, return, stop");
    sfxr_voice_listen_begin();
    static float half_second[8000];
    for (int i = 0; i < 8000; i++) half_second[i] = 0.2f * sinf((float)i * 0.1f);
    sfxr_voice_feed(half_second, 8000);
    sfxr_voice_listen_end();
    char said[256] = "";
    bool got = false;
    for (int f = 0; f < 72 && !got; f++) { sfxt_frames(1); got = sfxr_voice_result(said, sizeof said); }
    CHECK(got, "a result within a second");
    CHECK(strstr(said, "Attack") != NULL, "heard \"%s\"", said);
    CHECK(strstr(said, "[prompt]") != NULL, "the command words reached the recognizer (\"%s\")", said);
    CHECK(sfxr_voice_match(said, CMDS, 3) == 0, "matched attack");
    int samples = 0;
    const char *p = strchr(said, '(');
    if (p) samples = atoi(p + 1);
    CHECK(samples >= 17600, "a half-second clip is padded to over a second for Whisper (%d samples)", samples);
}

// The ways a command comes back slightly wrong, and ones that aren't commands.
static void match_is_forgiving(void)
{
    static const char *const C[] = { "attack", "return", "stop", "go" };
    CHECK(sfxr_voice_match(" Attack.", C, 4) == 0, "\"Attack.\"");
    CHECK(sfxr_voice_match("uh, attack!", C, 4) == 0, "\"uh, attack!\"");
    CHECK(sfxr_voice_match("attach", C, 4) == 0, "\"attach\" is attack (Whisper's near miss)");
    CHECK(sfxr_voice_match("Returns", C, 4) == 1, "\"Returns\" is return");
    CHECK(sfxr_voice_match("return to the tower, then attack", C, 4) == 1, "two commands: the one said first");
    CHECK(sfxr_voice_match("no", C, 4) == -1, "\"no\" isn't \"go\" (tiny words must match exactly)");
    CHECK(sfxr_voice_match("hello there", C, 4) == -1, "no command in \"hello there\"");
    CHECK(sfxr_voice_match("", C, 4) == -1, "nothing heard");
}

// A sound one meter to your left comes out of the left; four meters ahead
// is centered and quieter; behind you is quieter than in front.
static void sound_from_the_left_is_on_the_left(void)
{
    SfxrPose head = sfxr_head();
    float pan, gain, pan_ahead, gain_ahead, gain_behind;
    sfxr_audio_spatial(sfxr_pose_apply(head, (Vector3){ -1, 0, 0 }), &pan, &gain);
    CHECK(pan < -0.5f, "left: pan %.2f", pan);
    sfxr_audio_spatial(sfxr_pose_apply(head, (Vector3){ 0, 0, -4 }), &pan_ahead, &gain_ahead);
    CHECK(fabsf(pan_ahead) < 0.05f, "ahead: centered (%.2f)", pan_ahead);
    CHECK_NEAR(gain_ahead, 0.25f, 0.01f, "four meters: a quarter as loud");
    sfxr_audio_spatial(sfxr_pose_apply(head, (Vector3){ 0, 0, 4 }), NULL, &gain_behind);
    CHECK(gain_behind < gain_ahead, "behind (%.2f) quieter than ahead (%.2f)", gain_behind, gain_ahead);
}

static const SfxtCase CASES[] = {
    { "voice/push-to-talk-recognizes",       push_to_talk_recognizes,           "sfxr_voice_no_prompt" },
    { "voice/match-is-forgiving",             match_is_forgiving,                "sfxr_voice_match_exact" },
    { "voice/sound-from-the-left-is-on-the-left", sound_from_the_left_is_on_the_left, "sfxr_audio_pan_flipped" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
