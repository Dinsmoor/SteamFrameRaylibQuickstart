// station_smoothing.c - Smoothing: how a held thing follows the hand, side by
// side (docs/SMOOTHING.md, vrui.h section 11).
//
//   The sword   pick up the sword on its stand and wave it: five ghost swords
//               copy it, each following through a different mode of
//               vrui_smooth_pose -- Snap, Lag, Spring, Heavy, Steady. Let go
//               and it springs back to its stand (a Spring smoother too).
//   The panel   each mode's setting; Daddy Bug Smasher's hammer uses the same ones
//               (smoothing_spec), so tune here, then go and smash bugs.
//   The balls   the easing curves (vrui_ease), each ball riding its curve
//               up its rail every two seconds.

#include "toolbox.h"

#define X0 10.9f

static struct {
    bool init;
    VruiSmoothSpec spec[VRUI_SMOOTH_COUNT];
    VruiSmooth ghost[VRUI_SMOOTH_COUNT];
    SfxrPose sword, sword_shown;
    VruiSmooth home;           // brings the sword back to its stand
    bool held;
    SfxrPose panel;
    float t;
} SM;

static void init(void)
{
    for (int m = 0; m < VRUI_SMOOTH_COUNT; m++) SM.spec[m] = vrui_smooth_spec((VruiSmoothMode)m);
    SM.panel = (SfxrPose){ { X0 + 0.95f, 1.35f, ROW_Z + 0.05f }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -20 * DEG2RAD) };
    SM.init = true;
}

const VruiSmoothSpec *smoothing_spec(VruiSmoothMode mode)
{
    if (!SM.init) init();
    return &SM.spec[(unsigned)mode < VRUI_SMOOTH_COUNT ? mode : 0];
}

static SfxrPose on_bench(float x, float y, float z) { return (SfxrPose){ { X0 + x, TABLE_Y + y, ROW_Z + z }, QuaternionIdentity() }; }

// A sword: the pose is its grip; the blade goes up +Y.
static void draw_sword(SfxrPose p, Color blade)
{
    vrui_box(sfxr_pose_mul(p, (SfxrPose){ { 0, 0, 0 }, QuaternionIdentity() }), (Vector3){ 0.03f, 0.12f, 0.03f }, (Color){ 90, 60, 40, 255 });
    vrui_box(sfxr_pose_mul(p, (SfxrPose){ { 0, 0.065f, 0 }, QuaternionIdentity() }), (Vector3){ 0.12f, 0.015f, 0.03f }, (Color){ 160, 150, 90, 255 });
    vrui_box(sfxr_pose_mul(p, (SfxrPose){ { 0, 0.3f, 0 }, QuaternionIdentity() }), (Vector3){ 0.035f, 0.45f, 0.008f }, blade);
}

static void panel(void)
{
    if (!vrui_panel_begin(VRUI_ID2(G_SMOOTH, 0), &SM.panel, 0.572f, 0.65f, "Smoothing")) return;
    vrui_layout_begin(vrui_panel_content(), 5);
    VruiSmoothSpec *s = SM.spec;
    vrui_label(vrui_row(22), "Lag: seconds to close half the gap");
    vrui_slider(1, vrui_row(32), "halflife", &s[VRUI_SMOOTH_LAG].halflife, 0.01f, 0.3f);
    vrui_label(vrui_row(22), "Spring: swings a second, and damping");
    vrui_slider(2, vrui_row(32), "Hz", &s[VRUI_SMOOTH_SPRING].frequency, 0.5f, 8.0f);
    vrui_slider(3, vrui_row(32), "damping", &s[VRUI_SMOOTH_SPRING].damping, 0.05f, 1.5f);
    vrui_label(vrui_row(22), "Heavy: top speed (m/s)");
    vrui_slider(4, vrui_row(32), "m/s", &s[VRUI_SMOOTH_HEAVY].max_speed, 0.5f, 8.0f);
    s[VRUI_SMOOTH_HEAVY].max_turn_deg = s[VRUI_SMOOTH_HEAVY].max_speed * 100.0f;   // one knob for both
    vrui_label(vrui_row(22), "Steady: how fast motion opens it up");
    vrui_slider(5, vrui_row(32), "beta", &s[VRUI_SMOOTH_STEADY].beta, 0.5f, 20.0f);
    if (vrui_button(6, vrui_row(32), "Defaults")) init();
    vrui_panel_end();
}

void station_smoothing(void)
{
    if (!SM.init) init();
    station_sign(X0, "Smoothing", "wave the sword: five ghosts follow it, one per mode (Daddy Bug Smasher's hammer uses these)");
    Color wood = { 120, 92, 66, 255 };
    vrui_box(on_bench(0, -0.025f, 0), (Vector3){ 1.4f, 0.05f, 0.6f }, wood);
    for (int i = 0; i < 4; i++)
        vrui_box(on_bench((i & 1) ? 0.62f : -0.62f, -TABLE_Y * 0.5f, (i & 2) ? 0.24f : -0.24f), (Vector3){ 0.05f, TABLE_Y - 0.05f, 0.05f },
                 (Color){ 90, 68, 52, 255 });

    // --- the sword on its stand at the front, and its five ghosts behind it
    SfxrPose stand = on_bench(0, 0.07f, 0.2f);
    vrui_box(on_bench(0, 0.005f, 0.2f), (Vector3){ 0.08f, 0.01f, 0.08f }, (Color){ 60, 60, 70, 255 });
    // (with_player off: the stand is part of the world, so a teleport mustn't
    // carry the returning sword along with you)
    VruiSmoothSpec home = *smoothing_spec(VRUI_SMOOTH_SPRING);
    home.with_player = false;
    if (!SM.held) SM.sword = vrui_smooth_pose(&SM.home, stand, &home);   // springs home
    VruiGrab g = vrui_grab_region(VRUI_ID2(G_SMOOTH, 10), &SM.sword, (Vector3){ 0.03f, 0.08f, 0.03f });
    SM.held = g.held;
    if (g.released) vrui_smooth_reset(&SM.home, SM.sword);
    draw_sword(SM.sword, (Color){ 220, 225, 235, 255 });

    static const Color GHOST[VRUI_SMOOTH_COUNT] = { { 240, 240, 240, 150 }, { 120, 200, 255, 150 }, { 250, 170, 80, 150 },
                                                    { 200, 90, 90, 150 }, { 120, 220, 140, 150 } };
    for (int m = 0; m < VRUI_SMOOTH_COUNT; m++) {
        // each ghost's target is the sword, moved sideways to its own lane
        float dx = 0.26f * (float)(m - 2);
        SfxrPose target = { Vector3Add(SM.sword.position, (Vector3){ dx, 0, -0.35f }), SM.sword.orientation };
        // the ghosts ride with you only while the sword is in your hand; left
        // on the bench, they're world things and stay put when you teleport
        VruiSmoothSpec spec = SM.spec[m];
        spec.with_player = SM.held;
        SfxrPose p = vrui_smooth_pose(&SM.ghost[m], target, &spec);
        draw_sword(p, GHOST[m]);
        vrui_text_at((SfxrPose){ { X0 + dx, TABLE_Y + 0.002f, ROW_Z - 0.28f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -PI / 2) },
                     vrui_smooth_name((VruiSmoothMode)m), 0.035f, GHOST[m]);
    }

    // --- easing curves: balls riding rails up the back of the bench
    SM.t += sfxr_dt();
    float phase = fmodf(SM.t, 2.0f) / 1.5f;   // 1.5 s of motion, half a second of rest
    static const char *const EASE[VRUI_EASE_COUNT] = { "linear", "smooth", "in", "out", "in-out", "back", "elastic", "bounce" };
    for (int e = 0; e < VRUI_EASE_COUNT; e++) {
        float x = -0.6f + 0.17f * (float)e;
        Vector3 bottom = { X0 + x, TABLE_Y + 0.72f, ROW_Z - 0.29f }, top = Vector3Add(bottom, (Vector3){ 0, 0.3f, 0 });
        vrui_line(bottom, top, (Color){ 120, 120, 130, 255 });
        vrui_box((SfxrPose){ Vector3Lerp(bottom, top, vrui_ease((VruiEase)e, phase)), QuaternionIdentity() }, (Vector3){ 0.03f, 0.03f, 0.03f },
                 (Color){ 250, 210, 90, 255 });
        vrui_text_at((SfxrPose){ Vector3Add(bottom, (Vector3){ 0, -0.03f, 0.01f }), QuaternionIdentity() }, EASE[e], 0.03f, RAYWHITE);
    }
    vrui_box(on_bench(0, 0.85f, -0.31f), (Vector3){ 1.4f, 0.4f, 0.02f }, (Color){ 50, 54, 62, 255 });   // the board behind the rails
    vrui_box(on_bench(-0.66f, 0.4f, -0.31f), (Vector3){ 0.04f, 0.9f, 0.04f }, (Color){ 90, 68, 52, 255 });
    vrui_box(on_bench(0.66f, 0.4f, -0.31f), (Vector3){ 0.04f, 0.9f, 0.04f }, (Color){ 90, 68, 52, 255 });

    panel();
}
