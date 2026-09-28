// tests/wield - wielding keeps its promises (docs/WIELDING.md): a thing
// taken by its handle settles into the hand the way it's meant to be held,
// however it was lying; a second hand on the handle steers it; weight shows
// as lag; a sticky grip loosened slides along the handle instead of
// dropping; an anvil won't come up with one hand; a throw flies and lands.
//
//   make test T=wield

#include "sfxt.h"

#include <math.h>
#include <string.h>

#define R SFXR_RIGHT
#define L SFXR_LEFT

// --- the things -------------------------------------------------------------------------

// A sword: handle along +Y from -8 cm to +8 cm, blade on up; its edge is +X.
static VruiWieldSpec sword_s, hammer_s, spear_s, anvil_s, ball_s;
static SfxrPose sword, hammer, spear, anvil, ball;
static VruiWield sword_w, hammer_w, spear_w, anvil_w, ball_w;
static bool ready;
static bool show[5];

static void specs(void)
{
    sword_s = vrui_wield_spec(VRUI_WEIGHT_MEDIUM);
    sword_s.grip[0] = (VruiGrip){ { 0, -0.08f, 0 }, { 0, 0.08f, 0 }, { 1, 0, 0 }, 2, false };
    sword_s.ngrips = 1;
    sword_s.center = (Vector3){ 0, 0.15f, 0 };
    sword_s.box_center = (Vector3){ 0, 0.3f, 0 };
    sword_s.half = (Vector3){ 0.06f, 0.4f, 0.02f };

    // Daddy's hammer: a long shaft (0 .. 70 cm along +Y), the head at the top, face +Z
    hammer_s = vrui_wield_spec(VRUI_WEIGHT_HEAVY);
    hammer_s.grip[0] = (VruiGrip){ { 0, 0, 0 }, { 0, 0.7f, 0 }, { 0, 0, 1 }, 1, false };
    hammer_s.ngrips = 1;
    hammer_s.center = (Vector3){ 0, 0.7f, 0 };
    hammer_s.box_center = (Vector3){ 0, 0.4f, 0 };
    hammer_s.half = (Vector3){ 0.08f, 0.42f, 0.05f };

    // a 2 m staff, round: any way round in the hand
    spear_s = vrui_wield_spec(VRUI_WEIGHT_MEDIUM);
    spear_s.grip[0] = (VruiGrip){ { 0, -1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, 0, false };
    spear_s.ngrips = 1;
    spear_s.half = (Vector3){ 0.02f, 1.0f, 0.02f };

    anvil_s = vrui_wield_spec(VRUI_WEIGHT_HUGE);
    anvil_s.half = (Vector3){ 0.2f, 0.15f, 0.1f };

    ball_s = vrui_wield_spec(VRUI_WEIGHT_LIGHT);
    ball_s.half = (Vector3){ 0.04f, 0.04f, 0.04f };
}

static void scene(void)
{
    if (!ready) { specs(); ready = true; }
    if (show[0]) sword_w = vrui_wield(1, &sword, &sword_s);
    if (show[1]) hammer_w = vrui_wield(2, &hammer, &hammer_s);
    if (show[2]) spear_w = vrui_wield(3, &spear, &spear_s);
    if (show[3]) anvil_w = vrui_wield(4, &anvil, &anvil_s);
    if (show[4]) ball_w = vrui_wield(5, &ball, &ball_s);
}

static SfxrPose pose(Vector3 p, Quaternion q) { return (SfxrPose){ p, q }; }
static Vector3 v3(float x, float y, float z) { return (Vector3){ x, y, z }; }
static Vector3 axis_of(SfxrPose p) { return Vector3RotateByQuaternion((Vector3){ 0, 1, 0 }, p.orientation); }
static Vector3 hand_dir(SfxrHandId h, Vector3 local) { return Vector3RotateByQuaternion(local, sfxr_hand(h)->grip.orientation); }

// The hand to a point, turned as given, then the grip closed.
static void take_at(SfxrHandId h, Vector3 p, Quaternion q)
{
    sfxt_hand_to(h, pose(p, q), 0.3f);
    sfxt_frames(2);
    sfxt_grip(h, 1.0f);
    sfxt_frames(4);
}

// --- cases -----------------------------------------------------------------------------

static void sword_settles_into_the_hand(void)
{
    // A sword lying on its side, blade pointing right, edge down. The hand
    // comes in pointing forward: the sword turns to point where the fist
    // does, edge toward the knuckles -- not stuck at the angle it lay.
    show[0] = true;
    sfxt_noise(0, 0, 0);
    sword = pose(v3(0.2f, 0.9f, -0.4f), QuaternionFromAxisAngle(v3(0, 0, 1), -PI / 2));
    sfxt_frames(2);
    take_at(R, sword.position, QuaternionIdentity());
    sfxt_frames(30);
    CHECK(sword_w.hands == 1, "held");
    float along = Vector3DotProduct(axis_of(sword), hand_dir(R, v3(0, 0, -1)));
    CHECK(along > 0.98f, "the blade points out of the fist (%.2f)", along);
    float edge = fabsf(Vector3DotProduct(Vector3RotateByQuaternion(v3(1, 0, 0), sword.orientation), hand_dir(R, v3(0, -1, 0))));
    CHECK(edge > 0.98f, "the edge toward the knuckles (or back: it has two) (%.2f)", edge);
    float at = Vector3Distance(sword.position, sfxr_hand(R)->grip.position);
    CHECK(at < 0.02f, "held where the hand took it, the handle's middle (%.1f cm off)", at * 100);
}

static void hammer_face_forward(void)
{
    // A hammer lying shaft forward, face UP; the hand comes in knuckles
    // down. Its face has one way round: toward the knuckles, so it turns over.
    show[1] = true;
    sfxt_noise(0, 0, 0);
    hammer = pose(v3(0.2f, 0.9f, -0.4f), QuaternionFromAxisAngle(v3(1, 0, 0), -PI / 2));   // shaft forward, face up
    sfxt_frames(2);
    take_at(R, sfxr_pose_apply(hammer, v3(0, 0.1f, 0)), QuaternionIdentity());
    sfxt_frames(60);
    float face = Vector3DotProduct(Vector3RotateByQuaternion(v3(0, 0, 1), hammer.orientation), hand_dir(R, v3(0, -1, 0)));
    CHECK(face > 0.95f, "the face toward the knuckles (%.2f)", face);
}

static void second_hand_steers(void)
{
    // A staff in the right hand, then the left hand on it further along.
    // Moving the left hand sideways swings the staff's far end with it.
    show[2] = true;
    sfxt_noise(0, 0, 0);
    spear = pose(v3(0.2f, 1.0f, -0.8f), QuaternionFromAxisAngle(v3(1, 0, 0), -PI / 2));   // lying pointing forward
    sfxt_frames(2);
    take_at(R, sfxr_pose_apply(spear, v3(0, -0.5f, 0)), QuaternionIdentity());
    sfxt_frames(20);
    Vector3 far = sfxr_pose_apply(spear, v3(0, 0.5f, 0));
    take_at(L, far, QuaternionIdentity());
    CHECK(spear_w.hands == 2, "both hands on it");
    sfxt_hand_to(L, pose(Vector3Add(far, v3(0.4f, 0, 0)), QuaternionIdentity()), 0.3f);
    sfxt_frames(40);
    Vector3 ax = axis_of(spear);
    CHECK(ax.x > 0.25f, "the far end followed the left hand (axis %.2f %.2f %.2f)", ax.x, ax.y, ax.z);
    float at = Vector3Distance(sfxr_pose_apply(spear, v3(0, -0.5f, 0)), sfxr_hand(R)->grip.position);
    CHECK(at < 0.03f, "still held at the right hand (%.1f cm off)", at * 100);
}

static void weight_lags(void)
{
    // A quick half-meter move: the hammer, held at the end of its shaft,
    // trails the hand; weightless it would be exactly on it.
    show[1] = true;
    sfxt_noise(0, 0, 0);
    hammer = pose(v3(0.2f, 0.9f, -0.4f), QuaternionFromAxisAngle(v3(1, 0, 0), -PI / 2));
    sfxt_frames(2);
    take_at(R, hammer.position, QuaternionIdentity());
    sfxt_frames(60);
    SfxrPose h = sfxt_hand(R);
    sfxt_hand_to(R, pose(Vector3Add(h.position, v3(0.5f, 0, 0)), h.orientation), 0.12f);
    CHECK(hammer_w.lag > 0.05f, "it trails the hand (%.1f cm behind)", hammer_w.lag * 100);
    sfxt_frames(90);
    CHECK(hammer_w.lag < 0.01f, "and catches up (%.1f cm)", hammer_w.lag * 100);
}

static void sticky_loosened_slides(void)
{
    // The hammer in a firm grip, then the grip loosened (not let go): it
    // stays in the hand, and moving the hand along the shaft slides it
    // there -- the hammer stays put. Fully open: it drops.
    show[1] = true;
    sfxt_noise(0, 0, 0);
    hammer = pose(v3(0.2f, 1.1f, -0.4f), QuaternionFromAxisAngle(v3(1, 0, 0), -PI / 2));
    sfxt_frames(2);
    take_at(R, sfxr_pose_apply(hammer, v3(0, 0.15f, 0)), QuaternionIdentity());
    sfxt_frames(60);
    Vector3 before = hammer.position;
    sfxt_grip(R, 0.25f);   // loosened
    sfxt_frames(5);
    CHECK(hammer_w.hands == 1, "a loosened grip still holds it");
    SfxrPose h = sfxt_hand(R);
    sfxt_hand_to(R, pose(Vector3Add(h.position, Vector3Scale(hand_dir(R, v3(0, 0, -1)), 0.2f)), h.orientation), 0.5f);
    sfxt_frames(30);
    CHECK(hammer_w.sliding[R], "sliding");
    float moved = Vector3Distance(before, hammer.position);
    CHECK(moved < 0.05f, "the hand slid up the shaft; the hammer stayed (%.1f cm)", moved * 100);
    sfxt_grip(R, 0.0f);
    sfxt_frames(3);
    CHECK(hammer_w.hands == 0, "let go completely: it drops");
}

static void anvil_takes_two_hands(void)
{
    // One hand drags an anvil but can't lift it; two lift it.
    show[3] = true;
    sfxt_noise(0, 0, 0);
    anvil = pose(v3(0.1f, 0.15f, -0.5f), QuaternionIdentity());
    sfxt_frames(10);
    take_at(R, v3(0.25f, 0.2f, -0.5f), QuaternionIdentity());
    sfxt_hand_to(R, pose(v3(0.25f, 0.8f, -0.5f), QuaternionIdentity()), 0.5f);
    sfxt_frames(40);
    CHECK(anvil_w.straining && anvil.position.y < 0.22f, "one hand: it won't come up (y %.2f)", anvil.position.y);
    take_at(L, v3(-0.05f, anvil.position.y + 0.05f, -0.5f), QuaternionIdentity());
    sfxt_hand_to(L, pose(v3(-0.05f, 0.8f, -0.5f), QuaternionIdentity()), 0.5f);
    sfxt_frames(90);
    CHECK(!anvil_w.straining && anvil.position.y > 0.5f, "two hands lift it (y %.2f)", anvil.position.y);
}

static void throw_flies_and_lands(void)
{
    // A ball swung forward and let go mid-swing flies on, lands, and settles.
    show[4] = true;
    sfxt_noise(0, 0, 0);
    ball = pose(v3(0.2f, 1.0f, -0.3f), QuaternionIdentity());
    sfxt_frames(2);
    take_at(R, ball.position, QuaternionIdentity());
    sfxt_hand_to(R, pose(v3(0.2f, 1.3f, -0.9f), QuaternionIdentity()), 0.12f);
    sfxt_grip(R, 0.0f);
    sfxt_frames(2);
    CHECK(ball_w.loose && ball_w.velocity.z < -2.0f, "it leaves the hand going forward (%.1f m/s)", -ball_w.velocity.z);
    sfxt_wait(4.0f);
    CHECK(!ball_w.loose, "it comes to rest");
    CHECK(ball.position.z < -2.0f && fabsf(ball.position.y - 0.04f) < 0.02f, "on the floor, meters away (%.1f m, y %.2f)",
          -ball.position.z, ball.position.y);
}

static void laser_pulls_it_to_the_hand(void)
{
    // A sword on the floor, out of reach: point at it, grip, and it flies
    // into the hand, by its handle.
    show[0] = true;
    sfxt_noise(0, 0, 0);
    sword = pose(v3(0.3f, 0.02f, -1.5f), QuaternionFromAxisAngle(v3(0, 0, 1), -PI / 2));
    sfxt_frames(2);
    SfxrPose at = sfxt_hand(R);
    Vector3 to = Vector3Normalize(Vector3Subtract(sfxr_pose_apply(sword, v3(0, 0.3f, 0)), at.position));
    sfxt_hand_to(R, pose(at.position, QuaternionFromVector3ToVector3(v3(0, 0, -1), to)), 0.2f);
    sfxt_frames(3);
    sfxt_grip(R, 1.0f);
    sfxt_wait(0.5f);
    CHECK(sword_w.hands == 1 && sword_w.hand == R, "it flew into the hand");
    float at_hand = Vector3Distance(sword.position, sfxr_hand(R)->grip.position);
    CHECK(at_hand < 0.03f, "held by its handle (%.1f cm off)", at_hand * 100);
}

static const SfxtCase CASES[] = {
    { "wield/sword-settles-into-the-hand", sword_settles_into_the_hand, "vrui_wield_keeps_grab_angle" },
    { "wield/hammer-face-forward",         hammer_face_forward,         "vrui_wield_keeps_grab_angle" },
    { "wield/second-hand-steers",          second_hand_steers,          "vrui_wield_second_hand_ignored" },
    { "wield/weight-lags",                 weight_lags,                 "vrui_wield_weightless" },
    { "wield/sticky-loosened-slides",      sticky_loosened_slides,      "vrui_wield_not_sticky" },
    { "wield/anvil-takes-two-hands",       anvil_takes_two_hands,       NULL },
    { "wield/throw-flies-and-lands",       throw_flies_and_lands,       NULL },
    { "wield/laser-pulls-it-to-the-hand",  laser_pulls_it_to_the_hand,  NULL },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
