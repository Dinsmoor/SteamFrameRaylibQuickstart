// tests/voice - sound and voice commands keep their promises (docs/AUDIO.md):
// a sound on your left is heard on your left (louder, sooner, brighter), and
// quieter far away or behind you; a speaker is quiet behind it; push-to-talk runs the recognizer in the background with your command
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

// Whisper sometimes loops on a short clip ("return return return..."): the
// text that comes back has each repeat once.
static void repeats_are_collapsed(void)
{
    if (!start_voice("Return return return, return. Go to the tower go to the tower")) return;
    sfxr_voice_listen_begin();
    sfxr_voice_listen_end();
    char said[256] = "";
    bool got = false;
    for (int f = 0; f < 72 && !got; f++) { sfxt_frames(1); got = sfxr_voice_result(said, sizeof said); }
    CHECK(got, "a result within a second");
    CHECK(strncmp(said, "Return Go to the tower [prompt]", 26) == 0 || strncmp(said, "Return Go to the tower (", 24) == 0,
          "one of each: \"%s\"", said);
}

// Hands-free: two seconds of room noise start nothing; then speech starts a
// clip, and a pause ends it and gets it recognized, with no button at all.
static void hands_free_speech_starts_and_a_pause_ends(void)
{
    if (!start_voice("Bugs, attack!")) return;
    sfxr_voice_hands_free(true, 0.15f, 0.4f);
    static float buf[16000 * 2];
    for (int i = 0; i < 32000; i++) buf[i] = 0.01f * sinf((float)i * 0.37f) * sinf((float)i * 0.011f);   // a quiet room
    sfxr_voice_feed(buf, 32000);
    CHECK(!sfxr_voice_listening() && !sfxr_voice_busy(), "room noise starts nothing");
    for (int i = 0; i < 8000; i++) buf[i] = 0.3f * sinf((float)i * 0.12f) * (0.6f + 0.4f * sinf((float)i * 0.002f));   // half a second of "speech"
    sfxr_voice_feed(buf, 8000);
    CHECK(sfxr_voice_listening(), "speech started a clip");
    for (int i = 0; i < 9600; i++) buf[i] = 0;   // 0.6 s of quiet
    sfxr_voice_feed(buf, 9600);
    CHECK(!sfxr_voice_listening(), "the pause ended it");
    char said[256] = "";
    bool got = false;
    for (int f = 0; f < 72 && !got; f++) { sfxt_frames(1); got = sfxr_voice_result(said, sizeof said); }
    CHECK(got && strstr(said, "attack"), "recognized: \"%s\"", said);
    CHECK(sfxr_voice_hands_free_heard(), "and it knows speech started it, not a button");
    sfxr_voice_hands_free(false, 0, 0);
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

// A sound a meter to your left: louder in the left ear, and the right ear
// hears it later and duller (the three cues ears use). Four meters ahead is
// the same in both ears and a quarter as loud as one meter; behind you is
// quieter and duller than in front.
static void sound_from_the_left_is_on_the_left(void)
{
    SfxrPose head = sfxr_head();
    SfxrEmitter e = sfxr_emitter_point(sfxr_pose_apply(head, (Vector3){ -1, 0, 0 }), 1.0f);
    SfxrEars left = sfxr_audio_ears(&e);
    CHECK(left.gain[0] > 3.0f * left.gain[1], "left ear louder: %.2f vs %.2f", left.gain[0], left.gain[1]);
    CHECK(left.delay_ms[1] - left.delay_ms[0] > 0.5f, "right ear later: %.2f ms vs %.2f", left.delay_ms[1], left.delay_ms[0]);
    CHECK(left.cutoff_hz[1] < 2500 && left.cutoff_hz[0] > 15000, "right ear duller: %.0f Hz vs %.0f", left.cutoff_hz[1], left.cutoff_hz[0]);
    e.pose.position = sfxr_pose_apply(head, (Vector3){ 0, 0, -1 });
    SfxrEars one = sfxr_audio_ears(&e);
    e.pose.position = sfxr_pose_apply(head, (Vector3){ 0, 0, -4 });
    SfxrEars ahead = sfxr_audio_ears(&e);
    CHECK_NEAR(ahead.gain[0], ahead.gain[1], 0.001f, "ahead: the same in both ears");
    CHECK_NEAR(ahead.delay_ms[1], 0.0f, 0.001f, "ahead: at the same moment");
    CHECK_NEAR(ahead.gain[0] / one.gain[0], 0.25f, 0.01f, "four meters: a quarter as loud as one");
    e.pose.position = sfxr_pose_apply(head, (Vector3){ 0, 0, 4 });
    SfxrEars behind = sfxr_audio_ears(&e);
    CHECK(behind.gain[0] < ahead.gain[0], "behind (%.3f) quieter than ahead (%.3f)", behind.gain[0], ahead.gain[0]);
    CHECK(behind.cutoff_hz[0] < 0.5f * ahead.cutoff_hz[0], "behind duller: %.0f Hz vs %.0f", behind.cutoff_hz[0], ahead.cutoff_hz[0]);
}

// The mixer, heard: a click a meter to your left, rendered. The right
// channel is the left one about 0.66 ms later (31 frames at 48 kHz) --
// found by sliding one over the other -- and much quieter.
static void click_on_the_left_reaches_the_left_ear_first(void)
{
    CHECK(sfxr_audio_init(), "audio starts (offline)");
    SfxrSound click = sfxr_sound_synth(&(SfxrSynth){ 1800, 1200, 0.04f, 0.001f, 0.012f, 0.5f, 0, 0.45f }, 1);
    CHECK(click != 0, "a click made in code");
    SfxrEmitter e = sfxr_emitter_point(sfxr_pose_apply(sfxr_head(), (Vector3){ -1, 0, 0 }), 1.0f);
    sfxr_sound_emit(click, &e, false);
    enum { N = 4096 };
    static float out[N * 2];
    memset(out, 0, sizeof out);
    sfxr_audio_mix(out, N);
    float el = 0, er = 0;
    for (int i = 0; i < N; i++) { el += out[i * 2] * out[i * 2]; er += out[i * 2 + 1] * out[i * 2 + 1]; }
    CHECK(el > 4.0f * er && er > 0, "left carries most of it: energy %.2f vs %.2f", el, er);
    int best = 0;
    float best_c = -1e9f;
    for (int lag = -60; lag <= 60; lag++) {
        float c = 0;
        for (int i = 60; i < N - 60; i++) c += out[i * 2] * out[(i + lag) * 2 + 1];
        if (c > best_c) { best_c = c; best = lag; }
    }
    float expect = 0.656f * 0.001f * (float)sfxr_audio_rate();
    CHECK(fabsf((float)best - expect) < 6, "the right ear lags by %d frames (expect about %.0f)", best, expect);
}

// A cone (a speaker) is loud in front of it and quiet and muffled behind;
// a line (a stream) is heard from its nearest point, so walking along it
// it stays beside you.
static void cones_face_and_lines_follow(void)
{
    SfxrPose head = sfxr_head();
    Vector3 at = sfxr_pose_apply(head, (Vector3){ 0, 0, -2 });
    SfxrPose facing_you = { at, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, PI) };   // its -Z toward you
    SfxrPose facing_away = { at, QuaternionIdentity() };
    SfxrEmitter cone = sfxr_emitter_cone(facing_you, 60, 200, 1.0f);
    SfxrEars front = sfxr_audio_ears(&cone);
    cone.pose = facing_away;
    SfxrEars back = sfxr_audio_ears(&cone);
    CHECK(back.gain[0] < 0.35f * front.gain[0], "behind the speaker: %.3f vs %.3f in front", back.gain[0], front.gain[0]);
    CHECK(back.cutoff_hz[0] < 4000, "and muffled (%.0f Hz)", back.cutoff_hz[0]);

    Vector3 a = sfxr_pose_apply(head, (Vector3){ -20, 0, -3 }), b = sfxr_pose_apply(head, (Vector3){ 20, 0, -3 });
    SfxrEmitter stream = sfxr_emitter_line(a, b, 1.0f);
    SfxrEars s = sfxr_audio_ears(&stream);
    CHECK_NEAR(s.gain[0], s.gain[1], 0.001f, "a long line straight ahead: centered");
    Vector3 near = sfxr_emitter_nearest(&stream, head.position);
    CHECK_NEAR(Vector3Distance(near, head.position), 3.0f, 0.01f, "heard from its nearest point, 3 m away");
}

static const SfxtCase CASES[] = {
    { "voice/push-to-talk-recognizes",       push_to_talk_recognizes,           "sfxr_voice_no_prompt" },
    { "voice/hands-free-speech-starts-and-a-pause-ends", hands_free_speech_starts_and_a_pause_ends, "sfxr_voice_any_sound_starts" },
    { "voice/repeats-are-collapsed",          repeats_are_collapsed,             "sfxr_voice_keeps_repeats" },
    { "voice/match-is-forgiving",             match_is_forgiving,                "sfxr_voice_match_exact" },
    { "voice/sound-from-the-left-is-on-the-left", sound_from_the_left_is_on_the_left, "sfxr_audio_pan_flipped" },
    { "voice/click-reaches-the-near-ear-first", click_on_the_left_reaches_the_left_ear_first, "sfxr_audio_no_delay" },
    { "voice/cones-face-and-lines-follow",    cones_face_and_lines_follow,       "sfxr_audio_cone_ignored" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
