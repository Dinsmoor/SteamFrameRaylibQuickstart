// tests/attach - things that go with the player keep their promises
// (docs/ATTACHING.md): the radial menu picks what the stick points at, and
// never teleports you while you choose; a lazy-follow HUD stays put for a
// glance, comes back when you turn away, and rides along when you teleport;
// the body estimate ignores glances but follows real turns; edge arrows only
// show for things out of view; attaching keeps a thing exactly where it was.
//
//   make test T=attach

#include "sfxt.h"

#include <math.h>

static const char *const ITEMS[4] = { "up", "right", "down", "left" };
static int picked = -1, picks = 0;
static bool radial_on, follow_on;
static SfxrPose follow_pose, follow_target;
static VruiLocoConfig loco;
static bool smooth_on;
static VruiSmooth sm;
static VruiSmoothSpec sm_spec;
static SfxrPose sm_target, sm_out;

static void scene(void)
{
    if (radial_on) {
        int p = vrui_radial_menu(1, SFXR_RIGHT, &sfxr_hand(SFXR_RIGHT)->bumper, ITEMS, 4);
        if (p >= 0 || sfxr_hand(SFXR_RIGHT)->bumper.released) { picked = p; picks++; }
    }
    if (follow_on) follow_pose = vrui_follow(2, follow_target, 20.0f, 0.45f);
    if (smooth_on) sm_out = vrui_smooth_pose(&sm, sm_target, &sm_spec);
    vrui_locomotion(&loco);
}

static void setup(void)
{
    sfxt_noise(0, 0, 0);
    loco = vrui_loco_default();
    // the right hand held out in front, so the menu and the arc have a hand to use
    sfxt_hand_set(SFXR_RIGHT, (SfxrPose){ { 0.2f, 1.2f, -0.35f }, QuaternionIdentity() });
    sfxt_frames(2);
}

// Hold the bumper, tilt the stick, let go: returns what was picked.
static int choose(Vector2 stick)
{
    int before = picks;
    sfxt_button(SFXR_RIGHT, SFXR_CTL_BUMPER, true);
    sfxt_frames(3);
    sfxt_stick(SFXR_RIGHT, stick);
    sfxt_frames(6);
    sfxt_button(SFXR_RIGHT, SFXR_CTL_BUMPER, false);
    sfxt_frames(2);
    return picks > before ? picked : -99;
}

static float yaw_of(Vector3 f) { return atan2f(-f.x, -f.z); }

static float yaw_diff_deg(float a, float b)
{
    float d = a - b;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return fabsf(d) * RAD2DEG;
}

static void turn_head(float yaw_deg, float seconds)
{
    SfxrPose h = sfxt_head();
    h.orientation = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, yaw_deg * DEG2RAD);
    sfxt_head_to(h, seconds);
}

// --- radial menu -----------------------------------------------------------

// Slice 0 is the top and they go clockwise: stick right picks the second.
static void radial_picks_the_tilted_choice(void)
{
    setup();
    radial_on = true;
    int got = choose((Vector2){ 1, 0 });
    CHECK(got == 1, "stick right picked \"%s\" (want \"right\")", got >= 0 && got < 4 ? ITEMS[got] : "nothing");
    got = choose((Vector2){ 0, -1 });
    CHECK(got == 2, "stick back picked \"%s\" (want \"down\")", got >= 0 && got < 4 ? ITEMS[got] : "nothing");
}

// Choosing "up" means tilting the stick forward, which is also what aims a
// teleport. The menu owns the stick until it's back at center: no teleport,
// even when the stick is released after the button.
static void radial_never_teleports(void)
{
    setup();
    radial_on = true;
    Vector3 before = sfxr_head_floor_point();
    sfxt_button(SFXR_RIGHT, SFXR_CTL_BUMPER, true);
    sfxt_frames(3);
    sfxt_stick(SFXR_RIGHT, (Vector2){ 0, 1 });
    sfxt_frames(20);
    CHECK(vrui_input_claimed(SFXR_RIGHT), "the stick is claimed while the menu is open");
    sfxt_button(SFXR_RIGHT, SFXR_CTL_BUMPER, false);   // pick "up"...
    sfxt_frames(20);                              // ...thumb still on the tilted stick
    sfxt_stick(SFXR_RIGHT, (Vector2){ 0, 0 });    // then let go of the stick
    sfxt_frames(20);
    CHECK(picked == 0, "picked \"up\" (got %d)", picked);
    Vector3 after = sfxr_head_floor_point();
    CHECK(Vector3Distance(before, after) < 0.01f, "no teleport (moved %.2f m)", Vector3Distance(before, after));
}

// Changing your mind: back to center before letting go picks nothing.
static void radial_centered_cancels(void)
{
    setup();
    radial_on = true;
    sfxt_button(SFXR_RIGHT, SFXR_CTL_BUMPER, true);
    sfxt_frames(3);
    sfxt_stick(SFXR_RIGHT, (Vector2){ -1, 0 });
    sfxt_frames(6);
    sfxt_stick(SFXR_RIGHT, (Vector2){ 0, 0 });
    sfxt_frames(6);
    sfxt_button(SFXR_RIGHT, SFXR_CTL_BUMPER, false);
    sfxt_frames(2);
    CHECK(picks == 1 && picked == -1, "let go centered: nothing picked (got %d)", picked);
}

// --- lazy follow -------------------------------------------------------------

static void follow_start(void)
{
    setup();
    follow_on = true;
    follow_target = vrui_in_front_of_head(1.0f, 0.3f);
    sfxt_frames(2);
}

// A glance (10 degrees) leaves it where it is; turning away (60 degrees)
// brings it back in front within about half a second.
static void follow_stays_then_comes_back(void)
{
    follow_start();
    Vector3 start = follow_pose.position;
    turn_head(10, 0.2f);
    for (int i = 0; i < 72; i++) { sfxt_frames(1); follow_target = vrui_in_front_of_head(1.0f, 0.3f); }
    CHECK(Vector3Distance(follow_pose.position, start) < 0.005f, "a glance doesn't move it (moved %.3f m)",
          Vector3Distance(follow_pose.position, start));
    turn_head(60, 0.3f);
    for (int i = 0; i < 90; i++) { sfxt_frames(1); follow_target = vrui_in_front_of_head(1.0f, 0.3f); }
    CHECK(Vector3Distance(follow_pose.position, follow_target.position) < 0.05f, "back in front after turning away (%.2f m off)",
          Vector3Distance(follow_pose.position, follow_target.position));
}

// A teleport moves the rig 3 m: the HUD comes along in the same frame instead
// of swooping after you.
static void follow_rides_the_rig(void)
{
    follow_start();
    sfxt_frames(10);
    SfxrPose before = follow_pose;
    sfxr_rig_move((Vector3){ 0, 0, -3 });
    follow_target = vrui_in_front_of_head(1.0f, 0.3f);
    sfxt_frames(1);
    float moved = Vector3Distance(follow_pose.position, before.position);
    CHECK_NEAR(moved, 3.0f, 0.02f, "HUD moved with the rig");
}

// --- the body estimate ---------------------------------------------------------

static void body_ignores_glances_follows_turns(void)
{
    setup();
    float start = yaw_of(sfxr_pose_forward(vrui_body()));
    turn_head(30, 0.2f);
    sfxt_frames(16);
    float after_glance = yaw_of(sfxr_pose_forward(vrui_body()));
    CHECK(yaw_diff_deg(after_glance, start) < 8.0f, "a 30 degree glance turns the body %.1f degrees (want < 8)",
          yaw_diff_deg(after_glance, start));
    turn_head(120, 0.3f);
    sfxt_frames(24);
    float head = yaw_of(sfxr_pose_forward(sfxr_head()));
    float body = yaw_of(sfxr_pose_forward(vrui_body()));
    CHECK(yaw_diff_deg(head, body) <= 46.0f, "a real turn drags the body along (%.1f degrees behind the head)",
          yaw_diff_deg(head, body));
    sfxt_wait(5.0f);   // keep looking that way: the body comes round
    body = yaw_of(sfxr_pose_forward(vrui_body()));
    CHECK(yaw_diff_deg(head, body) < 5.0f, "after a while the body faces where you look (%.1f off)", yaw_diff_deg(head, body));
}

// --- edge arrows and attaching ---------------------------------------------------

static void arrow_only_out_of_view(void)
{
    setup();
    SfxrPose h = sfxr_head();
    bool ahead = vrui_offscreen_arrow(Vector3Add(h.position, (Vector3){ 0.2f, 0, -3 }), "ahead", WHITE);
    bool behind = vrui_offscreen_arrow(Vector3Add(h.position, (Vector3){ 0, 0, 3 }), "behind", WHITE);
    CHECK(!ahead, "no arrow for a target in view");
    CHECK(behind, "an arrow for a target behind you");
}

// Attaching where it is keeps it exactly there, and it then rides along.
static void attach_keeps_the_pose(void)
{
    SfxrPose parent = { { 1, 2, 3 }, QuaternionFromEuler(0.3f, 1.1f, -0.2f) };
    SfxrPose child = { { 0.5f, 1.0f, -2.0f }, QuaternionFromEuler(-0.7f, 0.2f, 0.4f) };
    SfxrPose local = sfxr_pose_relative(parent, child);
    SfxrPose back = sfxr_pose_mul(parent, local);
    CHECK(Vector3Distance(back.position, child.position) < 1e-4f, "same place after attaching");
    Quaternion a = back.orientation, b = child.orientation;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    CHECK(fabsf(dot) > 0.99999f, "same orientation after attaching");
    SfxrPose moved = { Vector3Add(parent.position, (Vector3){ 0, 0, -1 }), parent.orientation };
    SfxrPose rode = sfxr_pose_mul(moved, local);
    CHECK_NEAR(rode.position.z, child.position.z - 1.0f, 1e-4f, "rides along when the parent moves");
}

// --- smoothing ---------------------------------------------------------------------

static void smooth_start(VruiSmoothMode mode)
{
    setup();
    sm_spec = vrui_smooth_spec(mode);
    sm_target = (SfxrPose){ { 0, 1.2f, -0.5f }, QuaternionIdentity() };
    smooth_on = true;
    sfxt_frames(3);
}

// Lag: after one halflife, half the gap is closed.
static void smooth_lag_halflife(void)
{
    smooth_start(VRUI_SMOOTH_LAG);
    sm_target.position.x += 1.0f;
    sfxt_frames((int)roundf(sm_spec.halflife * 72.0f));
    CHECK_NEAR(sm_out.position.x, 0.5f, 0.08f, "half way after one halflife");
    sfxt_wait(1.0f);
    CHECK_NEAR(sm_out.position.x, 1.0f, 0.005f, "arrived");
}

// Spring (default damping 0.35) overshoots a step, then settles.
static void smooth_spring_overshoots(void)
{
    smooth_start(VRUI_SMOOTH_SPRING);
    sm_target.position.x += 1.0f;
    float peak = 0;
    for (int i = 0; i < 72; i++) { sfxt_frames(1); peak = fmaxf(peak, sm_out.position.x); }
    CHECK(peak > 1.15f, "overshot to %.2f", peak);
    sfxt_wait(3.0f);
    CHECK_NEAR(sm_out.position.x, 1.0f, 0.01f, "settled");
}

// Heavy: a 1 m jump is taken at no more than max_speed.
static void smooth_heavy_speed_limited(void)
{
    smooth_start(VRUI_SMOOTH_HEAVY);
    sm_target.position.x += 1.0f;
    sfxt_frames(1);
    CHECK(sm_out.position.x <= sm_spec.max_speed / 72.0f + 1e-4f, "one frame: moved %.3f m (limit %.3f)", sm_out.position.x,
          sm_spec.max_speed / 72.0f);
    sfxt_wait(1.5f);
    CHECK_NEAR(sm_out.position.x, 1.0f, 0.01f, "gets there");
}

// Steady: a 3 mm, 8 Hz tremble is mostly gone; a fast move comes through.
static void smooth_steady_quiets_tremble(void)
{
    smooth_start(VRUI_SMOOTH_STEADY);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 144; i++) {
        sm_target.position.x = 0.003f * sinf((float)i / 72.0f * 8.0f * 2.0f * PI);
        sfxt_frames(1);
        if (i > 72) { lo = fminf(lo, sm_out.position.x); hi = fmaxf(hi, sm_out.position.x); }
    }
    CHECK((hi - lo) < 0.003f, "tremble 6 mm peak to peak in, %.1f mm out", (hi - lo) * 1000);
    sm_target.position.x = 0.5f;   // a quick, big move
    sfxt_frames(9);
    CHECK(sm_out.position.x > 0.4f, "a fast move comes through in 1/8 s (%.2f of 0.5 m)", sm_out.position.x);
}

// Riding the rig: a teleport carries the smoothed thing along in the same
// frame, instead of it lagging across the world after you.
static void smooth_rides_the_rig(void)
{
    smooth_start(VRUI_SMOOTH_LAG);
    sfxt_frames(10);
    Vector3 before = sm_out.position;
    sfxr_rig_move((Vector3){ 0, 0, -3 });
    sm_target.position.z -= 3;   // the hand moved with the rig
    sfxt_frames(1);
    CHECK_NEAR(Vector3Distance(sm_out.position, before), 3.0f, 0.01f, "moved with the rig");
}

static const SfxtCase CASES[] = {
    { "attach/radial-picks-the-tilted-choice",  radial_picks_the_tilted_choice,  "vrui_radial_angle_from_x" },
    { "attach/radial-never-teleports",          radial_never_teleports,          "vrui_radial_no_claim" },
    { "attach/radial-centered-cancels",         radial_centered_cancels,         "vrui_radial_keeps_last" },
    { "attach/follow-stays-then-comes-back",    follow_stays_then_comes_back,    NULL },
    { "attach/follow-rides-the-rig",            follow_rides_the_rig,            "vrui_follow_world_space" },
    { "attach/body-ignores-glances-follows-turns", body_ignores_glances_follows_turns, "vrui_body_follows_head" },
    { "attach/arrow-only-out-of-view",          arrow_only_out_of_view,          "vrui_arrow_ignores_view" },
    { "attach/attach-keeps-the-pose",           attach_keeps_the_pose,           NULL },
    { "attach/smooth-lag-halflife",             smooth_lag_halflife,             NULL },
    { "attach/smooth-spring-overshoots",        smooth_spring_overshoots,        NULL },
    { "attach/smooth-heavy-speed-limited",      smooth_heavy_speed_limited,      NULL },
    { "attach/smooth-steady-quiets-tremble",    smooth_steady_quiets_tremble,    NULL },
    { "attach/smooth-rides-the-rig",            smooth_rides_the_rig,            "vrui_smooth_world_space" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
