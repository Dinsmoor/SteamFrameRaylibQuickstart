// station_sound.c - Sound, near the left end of the row: where sounds come
// from (docs/AUDIO.md). Five switches on the front of the table, one per
// kind of emitter, all off to start (a sound that repeats wears thin):
//
//   CHIME   a point: the speaker box chimes from wherever it is. Pick it up
//           and carry it round your head: left, right, behind, far.
//   RADIO   a cone: the radio plays a tune loudest the way it faces. Walk
//           round behind it, or pick it up and turn it away from you: it
//           goes quiet and muffled, like a real speaker's back.
//   STREAM  a line: the stream on the floor, off the table's left end. It's
//           heard from its nearest point, so walking along it, it stays
//           beside you; step over it and it swaps ears.
//   RAIN    a box: rain on the canopy in the aisle (to the right as you face the
//           table). Outside, it comes from
//           the canopy's nearest edge; step under it and it's all round you.
//   WIND    ambient: from nowhere in particular, the same in both ears.
//
// The panel shows what each ear gets from each emitter that's on
// (sfxr_audio_ears): how loud, how much later, how muffled. Those three
// differences between your ears are how you hear where a sound is.

#include "toolbox.h"
#include "sfxr_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X0 -9.8f

enum { E_CHIME, E_RADIO, E_STREAM, E_RAIN, E_WIND, E_COUNT };
static const char *const NAMES[E_COUNT] = { "CHIME", "RADIO", "STREAM", "RAIN", "WIND" };
static const char *const KINDS[E_COUNT] = { "point", "cone", "line", "box", "ambient" };

static struct {
    bool init;
    bool on[E_COUNT];
    SfxrPlaying playing[E_COUNT];
    SfxrSound loop[E_COUNT];     // the looping sounds (the chime is sounds.c's, played every 1.2 s)
    SfxrPose speaker, radio, panel;
    float chime_t;
} SN;

// --- loops made in code ------------------------------------------------------------

#define RATE 22050

static float frand(unsigned *s) { *s = *s * 1664525u + 1013904223u; return (float)(*s >> 8) / 8388608.0f - 1.0f; }

// A loop from a float buffer. Its last `fade` seconds are blended into its
// start, so it loops without a click.
static SfxrSound make_loop(float *buf, int n, float fade)
{
    int f = (int)(fade * RATE);
    int len = n - f;
    for (int i = 0; i < f; i++) {
        float t = (float)i / (float)f;
        buf[i] = buf[i] * t + buf[len + i] * (1 - t);
    }
    Wave w = { (unsigned)len, RATE, 32, 1, buf };
    SfxrSound s = sfxr_sound_from_wave(w, 1);
    free(buf);
    return s;
}

// A little tune: eight bell notes, half a second apart.
static SfxrSound make_tune(void)
{
    static const float NOTE[8] = { 523.3f, 659.3f, 784.0f, 1046.5f, 784.0f, 659.3f, 587.3f, 440.0f };
    int n = RATE * 4;
    float *b = calloc((size_t)n, sizeof *b);
    for (int k = 0; k < 8; k++) {
        int at = k * RATE / 2;
        for (int i = 0; i < RATE / 2 && at + i < n; i++) {
            float t = (float)i / RATE;
            float env = fminf(1, t / 0.005f) * expf(-t / 0.18f);
            b[at + i] += 0.35f * env * (sinf(2 * PI * NOTE[k] * t) + 0.3f * sinf(2 * PI * NOTE[k] * 2.0f * t));
        }
    }
    Wave w = { (unsigned)n, RATE, 32, 1, b };
    SfxrSound s = sfxr_sound_from_wave(w, 1);
    free(b);
    return s;
}

// A stream: soft low noise that swells, and little bubbling chirps.
static SfxrSound make_stream(void)
{
    int n = (int)(RATE * 3.3f);
    float *b = calloc((size_t)n, sizeof *b);
    unsigned seed = 7;
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        lp += (frand(&seed) - lp) * 0.08f;
        lp2 += (lp - lp2) * 0.3f;
        float swell = 0.6f + 0.4f * sinf((float)i / RATE * 2 * PI * 0.7f);
        b[i] = lp2 * 1.6f * swell;
    }
    for (int k = 0; k < 40; k++) {   // bubbles
        int at = (int)((frand(&seed) * 0.5f + 0.5f) * (float)(n - RATE / 10));
        float f0 = 500 + 400 * (frand(&seed) * 0.5f + 0.5f);
        for (int i = 0; i < RATE / 20; i++) {
            float t = (float)i / RATE;
            b[at + i] += 0.12f * expf(-t / 0.012f) * sinf(2 * PI * (f0 + 3000 * t) * t);
        }
    }
    return make_loop(b, n, 0.3f);
}

// Rain: a hiss, and a crackle of drops.
static SfxrSound make_rain(void)
{
    int n = (int)(RATE * 3.3f);
    float *b = calloc((size_t)n, sizeof *b);
    unsigned seed = 11;
    float prev = 0;
    for (int i = 0; i < n; i++) {
        float w = frand(&seed);
        b[i] = (w - prev) * 0.12f;   // high-passed: a hiss
        prev = w;
    }
    for (int k = 0; k < 900; k++) {   // drops
        int at = (int)((frand(&seed) * 0.5f + 0.5f) * (float)(n - 100));
        float a = 0.08f + 0.1f * (frand(&seed) * 0.5f + 0.5f);
        for (int i = 0; i < 60; i++) b[at + i] += a * expf(-(float)i / 8.0f) * frand(&seed);
    }
    return make_loop(b, n, 0.3f);
}

// Wind: deep noise, slowly rising and falling.
static SfxrSound make_wind(void)
{
    int n = (int)(RATE * 6.5f);
    float *b = calloc((size_t)n, sizeof *b);
    unsigned seed = 3;
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float gust = 0.5f + 0.5f * sinf(2 * PI * t / 6.0f) * sinf(2 * PI * t / 2.3f + 1.0f);
        lp += (frand(&seed) - lp) * (0.01f + 0.03f * gust);   // gusts are brighter
        lp2 += (lp - lp2) * 0.05f;
        b[i] = lp2 * 6.0f * (0.3f + 0.7f * gust);
    }
    return make_loop(b, n, 0.5f);
}

// --- the emitters ------------------------------------------------------------------

static SfxrPose on_table(float x, float y, float z) { return (SfxrPose){ { X0 + x, TABLE_Y + y, ROW_Z + z }, QuaternionIdentity() }; }

#define STREAM_X (X0 - 1.25f)
#define STREAM_Z0 -2.8f
#define STREAM_Z1 2.0f
// the canopy stands in the aisle between here and Menus & HUD: walk under it
static SfxrPose canopy(void) { return (SfxrPose){ { X0 + 0.95f, 1.15f, ROW_Z + 1.9f }, QuaternionIdentity() }; }
static const Vector3 CANOPY_HALF = { 0.55f, 1.15f, 0.55f };

static SfxrEmitter emitter(int e)
{
    switch (e) {
    case E_CHIME: return sfxr_emitter_point(SN.speaker.position, 0.9f);
    case E_RADIO: return sfxr_emitter_cone(SN.radio, 60, 220, 0.8f);
    case E_STREAM: return sfxr_emitter_line((Vector3){ STREAM_X, 0.05f, STREAM_Z0 }, (Vector3){ STREAM_X, 0.05f, STREAM_Z1 }, 0.7f);
    case E_RAIN: return sfxr_emitter_box(canopy(), CANOPY_HALF, 0.6f);
    default: return sfxr_emitter_ambient(0.35f);
    }
}

static void init(void)
{
    SN.speaker = on_table(-0.45f, 0.08f, 0.1f);
    // the radio's front (its -Z) faces the aisle
    SN.radio = (SfxrPose){ { X0 + 0.4f, TABLE_Y + 0.06f, ROW_Z + 0.05f }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, PI) };
    SN.panel = (SfxrPose){ { X0, 1.62f, ROW_Z - 0.3f }, QuaternionIdentity() };
    SN.loop[E_RADIO] = make_tune();
    SN.loop[E_STREAM] = make_stream();
    SN.loop[E_RAIN] = make_rain();
    SN.loop[E_WIND] = make_wind();
    SN.init = true;
}

// Switched on or off: start or stop its loop (the chime has no loop: it's a
// one-shot, repeated).
static void switched(int e)
{
    if (e == E_CHIME) { SN.chime_t = 0; return; }
    if (SN.on[e]) {
        SfxrEmitter em = emitter(e);
        SN.playing[e] = sfxr_sound_emit(SN.loop[e], &em, true);
    } else {
        sfxr_playing_stop(SN.playing[e]);
        SN.playing[e] = 0;
    }
}

static void draw_scenery(void)
{
    float t = (float)sfxr_time();
    // the stream, flat on the floor, with ripples running down it
    vrui_box((SfxrPose){ { STREAM_X, 0.005f, (STREAM_Z0 + STREAM_Z1) * 0.5f }, QuaternionIdentity() },
             (Vector3){ 0.35f, 0.01f, STREAM_Z1 - STREAM_Z0 }, (Color){ 50, 110, 190, 255 });
    for (int i = 0; i < 12; i++) {
        float z = STREAM_Z0 + fmodf((float)i * 0.4f + t * 0.5f, STREAM_Z1 - STREAM_Z0);
        vrui_line((Vector3){ STREAM_X - 0.12f, 0.012f, z }, (Vector3){ STREAM_X + 0.12f, 0.012f, z + 0.05f },
                  (Color){ 170, 210, 255, 255 });
    }
    vrui_text_at((SfxrPose){ { STREAM_X, 0.02f, STREAM_Z1 + 0.1f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -PI / 2) },
                 "STREAM: a line emitter", 0.03f, RAYWHITE);

    // the canopy over the aisle: four posts and a roof, and rain under it when it's on
    SfxrPose c = canopy();
    Vector3 h = CANOPY_HALF;
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? h.x : -h.x, z = (i & 2) ? h.z : -h.z;
        vrui_box((SfxrPose){ { c.position.x + x, h.y, c.position.z + z }, QuaternionIdentity() }, (Vector3){ 0.04f, 2 * h.y, 0.04f },
                 (Color){ 90, 70, 55, 255 });
    }
    vrui_box((SfxrPose){ { c.position.x, 2 * h.y + 0.02f, c.position.z }, QuaternionIdentity() }, (Vector3){ 2 * h.x + 0.1f, 0.03f, 2 * h.z + 0.1f },
             (Color){ 70, 100, 130, 200 });
    vrui_text3d((Vector3){ c.position.x, 2 * h.y + 0.15f, c.position.z + h.z }, "RAIN: a box emitter (step under it)", 0.03f, RAYWHITE);
    if (SN.on[E_RAIN]) {
        unsigned seed = 5;
        for (int i = 0; i < 60; i++) {
            float x = frand(&seed) * h.x, z = frand(&seed) * h.z;
            float y = 2 * h.y - fmodf(t * 3.0f + (frand(&seed) + 1) * 2.0f, 2 * h.y);
            vrui_line((Vector3){ c.position.x + x, y, c.position.z + z }, (Vector3){ c.position.x + x, y + 0.08f, c.position.z + z },
                      (Color){ 170, 200, 240, 200 });
        }
    }

    // the radio: a box with a grille on its front (-Z), and while it plays,
    // lines showing its loud cone
    SfxrPose r = SN.radio;
    vrui_box(sfxr_pose_mul(r, (SfxrPose){ { 0, 0, -0.036f }, QuaternionIdentity() }), (Vector3){ 0.1f, 0.07f, 0.004f }, (Color){ 30, 30, 34, 255 });
    vrui_text3d(sfxr_pose_apply(r, (Vector3){ 0, 0.1f, 0 }), SN.on[E_RADIO] ? "radio: turn me away from you" : "radio", 0.018f, RAYWHITE);
    if (SN.on[E_RADIO]) {
        const float s = sinf(30 * DEG2RAD), c = cosf(30 * DEG2RAD);   // half the inner cone: 30 degrees
        const Vector3 edge[4] = { { s, 0, -c }, { -s, 0, -c }, { 0, s, -c }, { 0, -s, -c } };
        for (int i = 0; i < 4; i++)
            vrui_line(sfxr_pose_apply(r, (Vector3){ 0, 0, -0.04f }), sfxr_pose_apply(r, Vector3Scale(edge[i], 0.5f)), (Color){ 250, 210, 90, 160 });
    }
}

// What each ear gets from each emitter that's on.
static void ears_panel(void)
{
    if (!vrui_panel_begin(VRUI_ID2(G_SOUND, 0), &SN.panel, 0.62f, 0.62f, "What your ears get")) return;
    vrui_layout_begin(vrui_panel_content(), 3);
    vrui_label(vrui_row(20), sfxr_audio_device() ? TextFormat("sound: on (%d Hz)", sfxr_audio_rate()) : "sound: offline (no audio device)");
    int shown = 0;
    for (int e = 0; e < E_COUNT; e++) {
        if (!SN.on[e]) continue;
        shown++;
        SfxrEmitter em = emitter(e);
        SfxrEars ears = sfxr_audio_ears(&em);
        int later = ears.delay_ms[0] > ears.delay_ms[1] ? 0 : 1;
        int duller = ears.cutoff_hz[0] < ears.cutoff_hz[1] ? 0 : 1;
        vrui_label(vrui_row(20), TextFormat("%s (%s)", NAMES[e], KINDS[e]));
        Rectangle cols[2];
        vrui_row_cols(12, 2, cols);
        vrui_progress(cols[0], ears.gain[0], vrui_style()->accent);
        vrui_progress(cols[1], ears.gain[1], vrui_style()->accent);
        float dt = fabsf(ears.delay_ms[0] - ears.delay_ms[1]);
        vrui_label(vrui_row(18), dt < 0.02f && ears.cutoff_hz[duller] > 15000 ? "  the same in both ears" :
                   TextFormat("  %s ear %.2f ms later, muffled above %.1f kHz", later ? "right" : "left", dt,
                              ears.cutoff_hz[duller] / 1000.0f));
    }
    if (!shown) vrui_label(vrui_row(20), "switch an emitter on (front of the table)");
    vrui_space(4);
    vrui_label(vrui_row(20), "the toolbox's sounds (made in code):");
    static const char *const SOUNDS[] = { "click", "tick", "stop", "bell", "chime", "thump", "squish", "chomp", "whoosh", "trill" };
    for (int i = 0; i < 10; i += 5) {
        Rectangle c5[5];
        vrui_row_cols(28, 5, c5);
        for (int k = 0; k < 5; k++)
            if (vrui_button(10 + i + k, c5[k], SOUNDS[i + k])) sound_play((SoundId)(i + k), SN.panel.position, 1.0f);
    }
    vrui_panel_end();
}

void station_sound(void)
{
    if (!SN.init) init();
    station_sign(X0, "Sound", "five kinds of emitter: switch them on at the\nfront of the table, then walk around them");
    vrui_box(on_table(0, -0.025f, 0), (Vector3){ 1.4f, 0.05f, 0.7f }, (Color){ 120, 92, 66, 255 });

    // the switches, along the front edge
    for (int e = 0; e < E_COUNT; e++) {
        SfxrPose at = on_table(-0.56f + 0.28f * (float)e, 0, 0.28f);
        if (vrui_switch(VRUI_ID2(G_SOUND, 1 + e), at, &SN.on[e], NAMES[e])) switched(e);
    }

    // the speaker (a point): chimes from wherever it is, while its switch is
    // on and you're near it (or carrying it)
    VruiGrab g = vrui_grabbable(VRUI_ID2(G_SOUND, 10), &SN.speaker, (Vector3){ 0.05f, 0.07f, 0.05f }, (Color){ 50, 50, 56, 255 });
    vrui_name_widget(VRUI_ID2(G_SOUND, 10), "speaker box");
    if (g.released && SN.speaker.position.y < 0.2f) SN.speaker.position.y = TABLE_Y + 0.08f;   // dropped: back on the table
    bool near = g.held || Vector3Distance(sfxr_head().position, SN.speaker.position) < 3.0f;
    if (SN.on[E_CHIME] && near && (SN.chime_t -= sfxr_dt()) <= 0) {
        SN.chime_t = 1.2f;
        sound_play(SND_CHIME, SN.speaker.position, 0.9f);
    }
    vrui_text3d(Vector3Add(SN.speaker.position, (Vector3){ 0, 0.11f, 0 }),
                SN.on[E_CHIME] ? "speaker: carry me around" : "speaker", 0.018f, RAYWHITE);

    // the radio (a cone): grab it and turn it
    VruiGrab rg = vrui_grabbable(VRUI_ID2(G_SOUND, 11), &SN.radio, (Vector3){ 0.07f, 0.05f, 0.035f }, (Color){ 150, 60, 50, 255 });
    vrui_name_widget(VRUI_ID2(G_SOUND, 11), "radio box");
    if (rg.released && SN.radio.position.y < 0.2f) SN.radio.position.y = TABLE_Y + 0.06f;

    // keep every playing loop where its emitter is (the radio moves; the rest
    // stay put, but a volume or a shape could change just the same)
    for (int e = E_RADIO; e < E_COUNT; e++) {
        sfxr_report(TextFormat("%s_playing", KINDS[e]), (float)sfxr_playing_on(SN.playing[e]));   // tests: "app cone_playing == 1"
        if (!SN.on[e]) continue;
        SfxrEmitter em = emitter(e);
        sfxr_playing_move(SN.playing[e], &em);
    }
    draw_scenery();
    ears_panel();
}
