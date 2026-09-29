// bench_mechanisms.c - the Mechanisms bench, ahead right: one of every
// reference mechanism with its default feel (docs/MECHANISMS.md).
//
// To use one in your app: copy its few lines, set spec.draw = false, and draw
// your own model at the result's `part` pose -- same behavior, your look.

#include "toolbox.h"

static struct {
    float knob, selector, crank, lever, sprung, slider, plunger;
    Vector2 stick;
    bool latch, rocker;
    int presses;
} B = { .knob = 0.5f, .selector = 2.0f, .lever = 0.5f, .sprung = 0.5f, .slider = 0.3f };

#define BENCH_W 1.4f
#define BENCH_D 0.6f

// The bench stands front-right on the station ring (see toolbox.h).
static SfxrPose bench_origin(void) { return row_pose(2.5f, TABLE_Y); }

static SfxrPose on_bench(float x, float z)
{
    return sfxr_pose_mul(bench_origin(), (SfxrPose){ { x, 0, z }, QuaternionIdentity() });
}

static void draw_bench(void)
{
    SfxrPose o = bench_origin();
    scenery_box(sfxr_pose_mul(o, (SfxrPose){ { 0, -0.025f, 0 }, QuaternionIdentity() }), (Vector3){ BENCH_W, 0.05f, BENCH_D },
             (Color){ 120, 90, 70, 255 }, MAT_WOOD);
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? BENCH_W * 0.45f : -BENCH_W * 0.45f, z = (i & 2) ? BENCH_D * 0.4f : -BENCH_D * 0.4f;
        scenery_box(sfxr_pose_mul(o, (SfxrPose){ { x, -TABLE_Y * 0.5f, z }, QuaternionIdentity() }),
                 (Vector3){ 0.05f, TABLE_Y - 0.05f, 0.05f }, (Color){ 90, 68, 52, 255 }, MAT_WOOD);
    }
    station_sign(2.5f, "Mechanisms", "one of every reference mechanism, default feel: grab, poke or laser them");
}

void bench_mechanisms(void)
{
    draw_bench();

    // back row: things that turn and swing
    VruiMechSpec s = vrui_knob_spec();
    s.label = "KNOB";
    vrui_rotary(VRUI_ID2(G_BENCH, 1), on_bench(-0.58f, -0.12f), &s, &B.knob);

    s = vrui_selector_spec(5);
    s.label = "SELECTOR";
    vrui_rotary(VRUI_ID2(G_BENCH, 2), on_bench(-0.38f, -0.12f), &s, &B.selector);

    s = vrui_crank_spec();
    s.label = "CRANK";
    vrui_rotary(VRUI_ID2(G_BENCH, 3), on_bench(-0.12f, -0.1f), &s, &B.crank);

    s = vrui_lever_spec();
    s.label = "LEVER";
    s.size = 0.16f;
    vrui_pivot(VRUI_ID2(G_BENCH, 4), on_bench(0.18f, -0.1f), &s, &B.lever);

    s = vrui_lever_spec();          // a dead-man lever: springs back to center
    s.label = "SPRUNG LEVER";
    s.size = 0.16f;
    s.spring = true;
    s.rest = 0.5f;
    s.color = (Color){ 240, 190, 60, 255 };
    vrui_pivot(VRUI_ID2(G_BENCH, 5), on_bench(0.38f, -0.1f), &s, &B.sprung);

    s = vrui_joystick_spec();
    s.label = "STICK";
    vrui_tilt(VRUI_ID2(G_BENCH, 6), on_bench(0.58f, -0.1f), &s, &B.stick);

    // front row: things that slide, and things you press
    s = vrui_slider_spec();
    s.label = "SLIDER";
    vrui_linear(VRUI_ID2(G_BENCH, 7), on_bench(-0.45f, 0.15f), &s, &B.slider);

    s = vrui_plunger_spec();
    s.label = "PLUNGER";
    vrui_linear(VRUI_ID2(G_BENCH, 8), on_bench(-0.05f, 0.15f), &s, &B.plunger);

    VruiPressSpec b = vrui_press_spec();
    b.label = TextFormat("BUTTON (%d)", B.presses);
    if (vrui_press(VRUI_ID2(G_BENCH, 9), on_bench(0.2f, 0.15f), &b, NULL).pressed) B.presses++;

    b = vrui_press_spec();
    b.latching = true;
    b.color = (Color){ 70, 150, 230, 255 };
    b.label = B.latch ? "LATCH: ON" : "LATCH: OFF";
    vrui_press(VRUI_ID2(G_BENCH, 10), on_bench(0.38f, 0.15f), &b, &B.latch);

    VruiRockerSpec w = vrui_rocker_spec();
    w.label = "ROCKER";
    vrui_rocker(VRUI_ID2(G_BENCH, 11), on_bench(0.56f, 0.15f), &w, &B.rocker);
}
