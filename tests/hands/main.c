// tests/hands - bare hands keep their promises (docs/INPUT.md, "Bare hands"):
// shapes come from the joints, a light fingertip pinch is a pinch, a pinch
// held at the edge doesn't flicker, palm-up is palm-up on both hands, and a
// hand the runtime reports only as joints still pinches, pokes and grabs.
//
// The harness builds a full 26-joint skeleton from the scripted grip pose
// and finger curls (sfxt_fingers / sfxt_shape / sfxt_pinch).
//
//   make test T=hands

#include "sfxt.h"
#include "../../sfxr/src/sfxr_internal.h"   // sfxr__hand_model: the true hand, to compare with

#include <math.h>

#define R SFXR_RIGHT
#define L SFXR_LEFT

// --- the scene: one button, one block -------------------------------------------

static SfxrPose button_at = { { 0.3f, 0.9f, -0.5f }, { 0, 0, 0, 1 } };
static SfxrPose block = { { -0.3f, 0.2f, 0.3f }, { 0, 0, 0, 1 } };   // out of the way until a case moves it
static VruiPressSpec button_s;
static bool ready;
static int presses;
static VruiGrab grab;

static void scene(void)
{
    if (!ready) { button_s = vrui_press_spec(); ready = true; }
    if (vrui_press(1, button_at, &button_s, NULL).pressed) presses++;
    grab = vrui_grab_region(2, &block, (Vector3){ 0.03f, 0.03f, 0.03f });
}

static SfxrPose pose(Vector3 p, Quaternion q) { return (SfxrPose){ p, q }; }
static Quaternion rot(Vector3 axis, float deg) { return QuaternionFromAxisAngle(axis, deg * DEG2RAD); }
#define X_AXIS ((Vector3){ 1, 0, 0 })
#define Z_AXIS ((Vector3){ 0, 0, 1 })

// A bare right hand in front of the chest, fingers forward.
static void bare_right(SfxtHandKind kind)
{
    sfxt_hand_kind(R, kind);
    sfxt_hand_set(R, pose((Vector3){ 0.2f, 1.2f, -0.35f }, QuaternionIdentity()));
    sfxt_shape(R, SFXR_SHAPE_RELAXED);
    sfxt_frames(3);
}

// --- shapes and gestures -------------------------------------------------------------

static void shapes_from_joints(void)
{
    bare_right(SFXT_BARE);
    const SfxrHandShape want[] = { SFXR_SHAPE_OPEN, SFXR_SHAPE_POINT, SFXR_SHAPE_FIST, SFXR_SHAPE_THUMBS_UP, SFXR_SHAPE_PINCH };
    for (int i = 0; i < 5; i++) {
        sfxt_shape(R, want[i]);
        sfxt_frames(5);
        CHECK(sfxr_hand(R)->shape == want[i], "made %s, read %s", sfxr_hand_shape_name(want[i]),
              sfxr_hand_shape_name(sfxr_hand(R)->shape));
    }
    CHECK(sfxr_hand(R)->curl_from_joints, "curl measured from the joints");
}

// Thumb and index tips touching with the index nearly straight: still a pinch.
static void light_pinch_is_a_pinch(void)
{
    bare_right(SFXT_BARE);
    sfxt_fingers(R, 0.1f, 0.15f, 0, 0, 0);
    sfxt_pinch(R, 1);
    sfxt_frames(5);
    CHECK(sfxr_hand_gestures(R)->pinch.down, "pinch gesture down (%.1f cm)", sfxr_hand_gestures(R)->pinch_dist[0] * 100);
    CHECK(sfxr_hand(R)->shape == SFXR_SHAPE_PINCH, "shape %s", sfxr_hand_shape_name(sfxr_hand(R)->shape));
}

// Pinch, then hold the fingers 2.2 to 3 cm apart (past the closing distance,
// short of the opening one) with a tremor: one press, no release.
static void pinch_no_flicker_at_the_edge(void)
{
    bare_right(SFXT_BARE);
    sfxt_fingers(R, 0.2f, 0.3f, 0, 0, 0);
    sfxt_frames(2);
    float open = sfxr_hand_gestures(R)->pinch_dist[0];
    // the harness moves the thumb tip in a straight line: distance is linear in the amount
    #define PINCH_FOR(d) ((open - (d)) / (open - 0.005f))
    int pressed = 0, released = 0;
    sfxt_pinch(R, PINCH_FOR(0.015f));
    for (int i = 0; i < 60; i++) {
        if (i >= 5) sfxt_pinch(R, PINCH_FOR((i / 3) % 2 ? 0.022f : 0.030f));
        sfxt_frames(1);
        pressed += sfxr_hand_gestures(R)->pinch.pressed;
        released += sfxr_hand_gestures(R)->pinch.released;
    }
    CHECK(pressed == 1 && released == 0, "one press, no release (%d, %d)", pressed, released);
    sfxt_pinch(R, PINCH_FOR(0.045f));
    sfxt_frames(2);
    CHECK(!sfxr_hand_gestures(R)->pinch.down, "opens at 4.5 cm");
}

// Palm up reads as up and palm down as down, on both hands (the hands are
// mirror images: a sign error shows up here).
static void palm_up_both_hands(void)
{
    for (int h = 0; h < 2; h++) {
        SfxrHandId id = h ? R : L;
        float s = h ? 1.0f : -1.0f;   // the right palm faces the grip's -X, the left one +X
        sfxt_hand_kind(id, SFXT_BARE);
        sfxt_shape(id, SFXR_SHAPE_OPEN);
        Vector3 at = { 0.25f * s, 1.0f, -0.35f };
        sfxt_hand_set(id, pose(at, rot(Z_AXIS, -90 * s)));   // turn the palm to the sky
        sfxt_frames(3);
        const SfxrHandGestures *g = sfxr_hand_gestures(id);
        CHECK(g->palm_up, "%s palm up (normal %.2f %.2f %.2f)", h ? "right" : "left", g->palm_normal.x, g->palm_normal.y,
              g->palm_normal.z);
        sfxt_hand_set(id, pose(at, rot(Z_AXIS, 90 * s)));    // and to the floor
        sfxt_frames(3);
        CHECK(!g->palm_up, "%s palm down is not up", h ? "right" : "left");
        CHECK(g->palm_normal.y < -0.9f, "%s palm normal points down (%.2f)", h ? "right" : "left", g->palm_normal.y);
    }
}

// --- hands the runtime reports only as joints ------------------------------------

// Aim with the steady ray at a button 80 cm away and pinch: it's pressed.
static void joint_only_hand_pinch_clicks(void)
{
    bare_right(SFXT_BARE_JOINTS_ONLY);
    sfxt_fingers(R, 0.2f, 0.3f, 0, 0, 0);
    sfxt_frames(3);
    const SfxrHandGestures *g = sfxr_hand_gestures(R);
    CHECK(g->valid, "joints tracked");
    Vector3 dir = sfxr_pose_forward(g->ray);
    button_at = pose(Vector3Add(g->ray.position, Vector3Scale(dir, 0.8f)),
                     QuaternionFromVector3ToVector3((Vector3){ 0, 1, 0 }, Vector3Negate(dir)));
    sfxt_frames(3);
    CHECK(sfxr_hand(R)->active, "the hand is active");
    CHECK(sfxr_hand(R)->source == SFXR_SOURCE_HAND, "as a bare hand");
    sfxt_pinch(R, 1);
    sfxt_frames(5);
    sfxt_pinch(R, 0);
    sfxt_frames(5);
    CHECK(presses == 1, "one press from one pinch (%d)", presses);
}

// Point, and push the index fingertip down onto a button.
static void joint_only_fingertip_pokes(void)
{
    bare_right(SFXT_BARE_JOINTS_ONLY);
    Quaternion down = rot(X_AXIS, -70);   // fingers pointing down and forward
    sfxt_hand_set(R, pose((Vector3){ 0.3f, 1.1f, -0.4f }, down));
    sfxt_shape(R, SFXR_SHAPE_POINT);
    sfxt_frames(5);
    // where the fingertip is relative to the scripted grip (what the test moves)
    Vector3 tip_off = Vector3Subtract(sfxr_hand_gestures(R)->index_tip, sfxt_hand(R).position);
    Vector3 top = Vector3Add(button_at.position, (Vector3){ 0, 0.02f, 0 });
    sfxt_hand_to(R, pose(Vector3Subtract(Vector3Add(top, (Vector3){ 0, 0.04f, 0 }), tip_off), down), 0.3f);
    sfxt_hand_to(R, pose(Vector3Subtract(Vector3Add(top, (Vector3){ 0, -0.01f, 0 }), tip_off), down), 0.3f);
    sfxt_hand_to(R, pose(Vector3Subtract(Vector3Add(top, (Vector3){ 0, 0.05f, 0 }), tip_off), down), 0.3f);
    CHECK(presses == 1, "one press from the fingertip (%d)", presses);
}

// Close the hand around a block and carry it.
static void joint_only_fist_grabs(void)
{
    bare_right(SFXT_BARE_JOINTS_ONLY);
    sfxt_shape(R, SFXR_SHAPE_OPEN);
    sfxt_frames(3);
    block.position = sfxr_hand(R)->grip.position;   // the block right in the open hand
    sfxt_frames(2);
    sfxt_shape(R, SFXR_SHAPE_FIST);
    sfxt_frames(5);
    CHECK(grab.held, "the fist holds the block");
    Vector3 before = block.position;
    sfxt_hand_to(R, pose(Vector3Add(sfxt_hand(R).position, (Vector3){ 0, 0.2f, 0 }), sfxt_hand(R).orientation), 0.4f);
    CHECK_NEAR(block.position.y - before.y, 0.2f, 0.02f, "block carried up");
    sfxt_shape(R, SFXR_SHAPE_OPEN);
    sfxt_frames(5);
    CHECK(!grab.held, "opening the hand lets go");
}

// Holding Frame controllers, SteamVR's skeleton (built from the touch
// sensors) has the thumb mirrored across the controller. sfxr reflects it
// back: the thumb drawn is where the hand's thumb is, lifted or resting.
static void frame_thumb_on_the_right_side(void)
{
    sfxt_hand_kind(R, SFXT_FRAME_SKELETON);
    SfxrPose g = pose((Vector3){ 0.2f, 1.1f, -0.3f }, rot(X_AXIS, -20));
    sfxt_hand_set(R, g);
    const float lifted[5] = { 0, 0.95f, 0.8f, 0.8f, 0.8f };   // (SteamVR's index is always curled)
    sfxt_fingers(R, lifted[0], lifted[1], lifted[2], lifted[3], lifted[4]);
    sfxt_frames(3);
    const SfxrHandJoints *j = sfxr_hand_joints(R);
    CHECK(j->valid && j->source == SFXR_SOURCE_CONTROLLER, "a skeleton from the controller");
    SfxrHandJoints truth;
    sfxr__hand_model(1, sfxr_hand(R)->grip, lifted, 0, &truth);   // the hand as it is (the rig doesn't move here)
    float off = Vector3Distance(j->joint[SFXR_JOINT_THUMB_TIP].position, truth.joint[SFXR_JOINT_THUMB_TIP].position);
    CHECK(off < 0.002f, "the thumb tip is where the thumb is (%.1f mm off)", off * 1000.0f);
    float fing = Vector3Distance(j->joint[SFXR_JOINT_INDEX_TIP].position, truth.joint[SFXR_JOINT_INDEX_TIP].position);
    CHECK(fing < 0.002f, "the fingers are left as reported (%.1f mm off)", fing * 1000.0f);
}

static const SfxtCase CASES[] = {
    { "hands/frame-thumb-on-the-right-side",   frame_thumb_on_the_right_side,   "sfxr_thumb_as_reported" },
    { "hands/shapes-from-joints",            shapes_from_joints,            "sfxr_shapes_from_values_only" },
    { "hands/light-pinch-is-a-pinch",        light_pinch_is_a_pinch,        "sfxr_pinch_shape_from_curl_only" },
    { "hands/pinch-no-flicker-at-the-edge",  pinch_no_flicker_at_the_edge,  "sfxr_pinch_no_hysteresis" },
    { "hands/palm-up-both-hands",            palm_up_both_hands,            "sfxr_palm_normal_flipped" },
    { "hands/joint-only-hand-pinch-clicks",  joint_only_hand_pinch_clicks,  "sfxr_no_joint_hands" },
    { "hands/joint-only-fingertip-pokes",    joint_only_fingertip_pokes,    "sfxr_no_joint_hands" },
    { "hands/joint-only-fist-grabs",         joint_only_fist_grabs,         "sfxr_no_joint_hands" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
