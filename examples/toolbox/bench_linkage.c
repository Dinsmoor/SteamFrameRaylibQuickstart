// bench_linkage.c - the Linkage bench, behind you on the right: controls wired to
// mechanical displays. The point is the wiring: each control's value is read
// and fed into something else, in plain code, every frame.
//
//   crank    -> speed gauge (turns/s) and a rolling total counter
//   knob     -> needle gauge
//   selector -> which of five lamps is lit
//   ARM      -> gates FIRE; FIRE -> shot counter (only while armed)
//   PIN      -> the set point the sprung lever returns to (a spec field, set per frame)
//   spinner  -> which wedge the pointer lands on

#include "toolbox.h"

#include <math.h>

static struct {
    float crank, crank_prev, crank_speed;
    float knob, selector, pin, lever;
    bool  armed;
    int   shots;
    float spin;
} LK = { .knob = 0.3f, .pin = 4.0f, .lever = 0.5f };

#define LINK_W 1.9f
#define LINK_D 0.6f
#define BOARD_Z (-LINK_D * 0.5f + 0.04f)   // the display board at the back

static SfxrPose origin(void) { return row_pose(5.2f, TABLE_Y); }

static SfxrPose on_link(float x, float y, float z)
{
    return sfxr_pose_mul(origin(), (SfxrPose){ { x, y, z }, QuaternionIdentity() });
}

// Lamps stand on their +Y: turn them to face out of the board.
static SfxrPose lamp_on_board(float x, float y)
{
    SfxrPose p = on_link(x, y, BOARD_Z + 0.01f);
    p.orientation = QuaternionMultiply(origin().orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2));
    return p;
}

static void draw_bench(void)
{
    vrui_box(on_link(0, -0.025f, 0), (Vector3){ LINK_W, 0.05f, LINK_D }, (Color){ 110, 84, 66, 255 });
    for (int i = 0; i < 4; i++)
        vrui_box(on_link((i & 1) ? LINK_W * 0.46f : -LINK_W * 0.46f, -TABLE_Y * 0.5f, (i & 2) ? LINK_D * 0.4f : -LINK_D * 0.4f),
                 (Vector3){ 0.05f, TABLE_Y - 0.05f, 0.05f }, (Color){ 90, 68, 52, 255 });
    vrui_box(on_link(0, 0.27f, -LINK_D * 0.5f + 0.02f), (Vector3){ LINK_W, 0.52f, 0.03f }, (Color){ 58, 60, 68, 255 });
    station_sign(5.2f, "Linkage bench", "controls wired to gauges, rolling counters and lamps");
}

void bench_linkage(void)
{
    draw_bench();

    // crank -> speed gauge + total counter
    VruiMechSpec s = vrui_crank_spec();
    s.size = 0.07f;
    s.label = NULL;
    vrui_rotary(VRUI_ID2(G_LINK, 1), on_link(-0.78f, 0, 0.1f), &s, &LK.crank);
    float dt = sfxr_dt();
    if (dt > 0) LK.crank_speed += ((LK.crank - LK.crank_prev) / dt - LK.crank_speed) * 0.15f;
    LK.crank_prev = LK.crank;
    vrui_gauge(VRUI_ID2(G_LINK, 20), on_link(-0.78f, 0.4f, BOARD_Z), fabsf(LK.crank_speed), 0, 2, "CRANK TURNS/S");
    vrui_odometer(VRUI_ID2(G_LINK, 21), on_link(-0.78f, 0.17f, BOARD_Z), fabsf(LK.crank) * 10.0f, 5, "TENTHS OF TURNS");

    // knob -> gauge
    s = vrui_knob_spec();
    s.detents = 11;                 // a click every 10%
    s.label = NULL;
    vrui_rotary(VRUI_ID2(G_LINK, 2), on_link(-0.5f, 0, 0.1f), &s, &LK.knob);
    vrui_gauge(VRUI_ID2(G_LINK, 22), on_link(-0.5f, 0.4f, BOARD_Z), LK.knob, 0, 1, "KNOB");

    // selector -> lamps
    s = vrui_selector_spec(5);
    s.label = NULL;
    vrui_rotary(VRUI_ID2(G_LINK, 3), on_link(-0.25f, 0, 0.1f), &s, &LK.selector);
    for (int i = 0; i < 5; i++)
        vrui_lamp(lamp_on_board(-0.37f + 0.06f * (float)i, 0.12f), (int)LK.selector == i, (Color){ 255, 190, 60, 255 },
                  TextFormat("%d", i + 1));

    // ARM gates FIRE; FIRE counts shots only while armed
    VruiRockerSpec w = vrui_rocker_spec();
    w.label = "ARM";
    vrui_rocker(VRUI_ID2(G_LINK, 4), on_link(-0.02f, 0, 0.12f), &w, &LK.armed);
    VruiPressSpec b = vrui_press_spec();
    b.label = "FIRE";
    if (vrui_press(VRUI_ID2(G_LINK, 5), on_link(0.14f, 0, 0.12f), &b, NULL).pressed) {
        if (LK.armed) LK.shots++;
        else vrui_haptic_pulse(SFXR_RIGHT, 0.2f, 0.12f, 60.0f);   // a dull "nothing happens" buzz
    }
    vrui_lamp(lamp_on_board(0.04f, 0.4f), LK.armed, (Color){ 230, 60, 50, 255 }, "ARMED");
    vrui_odometer(VRUI_ID2(G_LINK, 23), on_link(0.06f, 0.17f, BOARD_Z), (float)LK.shots, 3, "SHOTS");

    // PIN (9 holes) sets where the sprung lever returns to
    s = vrui_lever_spec();
    s.size = 0.09f;
    s.max = 8.0f;
    s.detents = 9;
    s.snap = true;
    s.label = "PIN";
    s.value_format = NULL;
    s.color = (Color){ 200, 200, 210, 255 };
    vrui_pivot(VRUI_ID2(G_LINK, 6), on_link(0.34f, 0, 0.12f), &s, &LK.pin);
    VruiMechSpec ls = vrui_lever_spec();
    ls.size = 0.16f;
    ls.spring = true;
    ls.rest = LK.pin / 8.0f;        // <- the pin's value becomes this lever's set point
    ls.label = "SPRUNG";
    ls.color = (Color){ 240, 190, 60, 255 };
    vrui_pivot(VRUI_ID2(G_LINK, 7), on_link(0.5f, 0, 0.1f), &ls, &LK.lever);
    vrui_gauge(VRUI_ID2(G_LINK, 24), on_link(0.42f, 0.4f, BOARD_Z), LK.lever, 0, 1, "LEVER");
    vrui_odometer(VRUI_ID2(G_LINK, 25), on_link(0.42f, 0.17f, BOARD_Z), LK.pin / 8.0f * 100.0f, 3, "SET POINT %");

    // spinner -> the wedge under the pointer (at the wheel's far edge)
    static const char *const WEDGE_NAMES[] = { "RED", "AMBER", "GREEN", "BLUE", "VIOLET", "WHITE" };
    s = vrui_spinner_spec(12);
    VruiMech sm = vrui_rotary(VRUI_ID2(G_LINK, 8), on_link(0.78f, 0, 0.05f), &s, &LK.spin);
    float a = fmodf(-PI / 2 + sm.position + 20.0f * PI, 2.0f * PI);   // the pointer's angle on the wheel
    int wedge = (int)(a / (2.0f * PI / 12.0f)) % 12;
    vrui_text3d(sfxr_pose_apply(origin(), (Vector3){ 0.78f, 0.3f, BOARD_Z + 0.05f }),
                TextFormat("WHEEL: %s", WEDGE_NAMES[wedge % 6]), 0.035f, RAYWHITE);
}
