// vrui_display.c - mechanical displays: read-only indicators that show a
// value the way a physical instrument would. They take no input (no laser,
// no claims) -- pair them with any control or app value:
//
//   vrui_gauge(ID_RPM, pose, crank_speed, 0, 3, "TURNS/S");
//   vrui_odometer(ID_COUNT, pose, total_turns, 5, "TOTAL");
//   vrui_lamp(pose, armed, GREEN, "ARMED");

#include "vrui_internal.h"

typedef struct {
    bool  started;
    float shown, velocity;
} NeedleState;
VRUI_STATE_FITS(NeedleState);

// A needle with a little mass: it swings to the value with a slight
// overshoot and settles, instead of snapping.
static float smooth_needle(VruiItem *it, float target, float tau)
{
    NeedleState *ns = VRUI_STATE(it, NeedleState);
    if (!ns->started) { ns->shown = target; ns->velocity = 0; ns->started = true; }
    float dt = sfxr_dt();
    float w = 2.0f * PI / fmaxf(tau * 4.0f, 1e-3f);                               // natural frequency
    float acc = w * w * (target - ns->shown) - 2.0f * 0.6f * w * ns->velocity;   // damping ratio 0.6
    ns->velocity += acc * dt;
    ns->shown += ns->velocity * dt;
    return ns->shown;
}

void vrui_gauge(VruiId id, SfxrPose pose, float value, float min, float max, const char *label)
{
    VruiItem *it = vrui__item(id);
    const float R = 0.06f, sweep = 270.0f * DEG2RAD;
    float t = max != min ? (value - min) / (max - min) : 0;
    float shown = smooth_needle(it, Clamp(t, -0.03f, 1.03f), 0.08f);   // pegs just past the ends
    // face in the pose's XY plane, looking along +Z
    Vector3 back = sfxr_pose_apply(pose, (Vector3){ 0, 0, -0.012f });
    SfxrPose face = { pose.position, QuaternionMultiply(pose.orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2)) };
    vrui__cylinder(back, pose.position, R * 1.08f, (Color){ 40, 42, 48, 255 });
    vrui__cylinder(back, sfxr_pose_apply(pose, (Vector3){ 0, 0, 0.001f }), R, (Color){ 225, 222, 210, 255 });
    vrui__ring(face, R * 1.02f, (Color){ 20, 20, 24, 255 });
    for (int i = 0; i <= 10; i++) {
        float a = sweep * 0.5f - sweep * (float)i / 10.0f + PI / 2;   // 0 at lower left, clockwise
        Vector3 d = { cosf(a), sinf(a), 0 };
        float in = i % 5 == 0 ? 0.78f : 0.86f;
        vrui_line(sfxr_pose_apply(pose, (Vector3){ d.x * R * in, d.y * R * in, 0.003f }),
                  sfxr_pose_apply(pose, (Vector3){ d.x * R * 0.95f, d.y * R * 0.95f, 0.003f }), (Color){ 30, 30, 34, 255 });
    }
    float a = sweep * 0.5f - sweep * shown + PI / 2;
    Vector3 tip = sfxr_pose_apply(pose, (Vector3){ cosf(a) * R * 0.9f, sinf(a) * R * 0.9f, 0.006f });
    Vector3 tail = sfxr_pose_apply(pose, (Vector3){ -cosf(a) * R * 0.15f, -sinf(a) * R * 0.15f, 0.006f });
    vrui__cylinder(tail, tip, 0.0018f, (Color){ 200, 40, 30, 255 });
    vrui__sphere(sfxr_pose_apply(pose, (Vector3){ 0, 0, 0.006f }), 0.005f, (Color){ 30, 30, 34, 255 });
    // labels are printed on the instrument's plane (a facing label would poke through a tilted panel)
    if (label) vrui_text_at(sfxr_pose_mul(pose, (SfxrPose){ { 0, -R * 1.4f, 0.003f }, QuaternionIdentity() }), label, 0.0252f, C.style.text);
}

void vrui_odometer(VruiId id, SfxrPose pose, float value, int digits, const char *label)
{
    // Rolling number drums like a car's mileage counter: the last drum turns
    // continuously, each drum to its left turns only while the one to its
    // right rolls from 9 to 0 (the carry). Drawn into a small passive panel
    // so the digits really roll through the window.
    if (digits < 1) digits = 1;
    if (digits > 9) digits = 9;
    const float cell_w = 0.028f, cell_h = 0.042f;
    SfxrPose p = pose;
    C.next_passive = true;
    if (!vrui_panel_begin(id, &p, cell_w * (float)digits, cell_h, NULL)) return;
    int w_px = C.p.w_px, h_px = C.p.h_px;
    int cw = w_px / digits;
    int fs = (int)(h_px * 0.8f);
    ClearBackground((Color){ 18, 18, 20, 255 });
    float v = fmaxf(value, 0.0f);
    for (int i = 0; i < digits; i++) {           // i = 0 is the rightmost drum
        float place = powf(10.0f, (float)i);
        float drum = floorf(v / place);                       // whole turns of this drum
        float below = fmodf(v, place);                        // what the drums to the right show
        float roll = i == 0 ? v - floorf(v) : fmaxf(0.0f, below - (place - 1.0f));   // carry in the last unit
        float pos = fmodf(drum, 10.0f) + roll;                // 0..10, fractional while rolling
        int x = w_px - (i + 1) * cw;
        vrui__panel_scissor(x + 2, 2, cw - 4, h_px - 4);
        DrawRectangle(x + 2, 0, cw - 4, h_px, (Color){ 235, 232, 222, 255 });
        for (int k = -1; k <= 1; k++) {
            int d = ((int)floorf(pos) + k + 10) % 10;
            float y = (float)h_px * 0.5f - (float)fs * 0.5f + ((float)k - (pos - floorf(pos))) * (float)h_px;
            const char *txt = TextFormat("%d", d);
            Vector2 sz = MeasureTextEx(C.font, txt, (float)fs, 1);
            DrawTextEx(C.font, txt, (Vector2){ (float)x + (float)cw * 0.5f - sz.x * 0.5f, y }, (float)fs, 1, (Color){ 20, 20, 24, 255 });
        }
        EndScissorMode();
        // drum shading: darker toward the top and bottom edges, like a cylinder
        DrawRectangleGradientV(x + 2, 0, cw - 4, h_px / 4, (Color){ 0, 0, 0, 160 }, (Color){ 0, 0, 0, 0 });
        DrawRectangleGradientV(x + 2, h_px * 3 / 4, cw - 4, h_px / 4, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 160 });
    }
    vrui_panel_end();
    SfxrPose bezel = { sfxr_pose_apply(pose, (Vector3){ 0, 0, -0.008f }), pose.orientation };
    vrui_box(bezel, (Vector3){ cell_w * (float)digits + 0.012f, cell_h + 0.012f, 0.014f }, (Color){ 40, 42, 48, 255 });
    if (label) vrui_text_at(sfxr_pose_mul(pose, (SfxrPose){ { 0, -cell_h, 0.003f }, QuaternionIdentity() }), label, 0.0252f, C.style.text);
}

void vrui_lamp(SfxrPose pose, bool on, Color color, const char *label)
{
    Vector3 c = sfxr_pose_apply(pose, (Vector3){ 0, 0.01f, 0 });
    vrui__cylinder(pose.position, sfxr_pose_apply(pose, (Vector3){ 0, 0.008f, 0 }), 0.022f, (Color){ 40, 42, 48, 255 });
    if (on) {
        vrui__sphere(c, 0.016f, ColorBrightness(color, 0.3f));
        vrui__sphere(c, 0.026f, ColorAlpha(color, 0.25f));   // glow
    } else {
        vrui__sphere(c, 0.016f, ColorBrightness(color, -0.7f));
    }
    if (label) vrui_text3d(sfxr_pose_apply(pose, (Vector3){ 0, 0.05f, 0 }), label, 0.021f, C.style.text);
}
