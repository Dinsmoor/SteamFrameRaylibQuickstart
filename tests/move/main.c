// tests/move - moving the player keeps its promises (docs/MOVEMENT.md):
// climbing moves the world by exactly what the hand did, switching hands
// never yanks you, you come down when the ground drops away, you land on a
// ledge you pulled yourself over, stairs yes and tables no, the teleport arc
// lands on platforms, and pads snap you to their center and facing.
//
// Each case builds only the bits of the course it needs (the `F` flags and
// boxes), so one case's geometry never gets in another's way.
//
//   make test T=move

#include "sfxt.h"

#include <math.h>

// --- the course ----------------------------------------------------------------

typedef struct { Vector3 lo, hi; } Box;   // hi.y is the top you stand on
static Box boxes[8];
static int nboxes;
static VruiLocoConfig loco;
static VruiTeleportPad pads[2];
static struct { bool wall_hold, edge_bar, bars; } F;
static VruiHold wall_hold, edge_bar, bar[3];

#define WALL_HOLD_AT  ((Vector3){ 0.3f, 1.4f, -0.45f })
#define EDGE_A        ((Vector3){ -0.6f, 1.2f, -0.5f })
#define EDGE_B        ((Vector3){ 0.6f, 1.2f, -0.5f })
#define BAR_Y         2.0f
static const float BAR_Z[3] = { -0.2f, -0.8f, -1.4f };

static void add_box(Vector3 lo, Vector3 hi) { boxes[nboxes++] = (Box){ lo, hi }; }

// A ledge ahead: a block 1.2 m tall whose front face is 0.5 m in front of you.
static void add_ledge(void) { add_box((Vector3){ -1, 0, -3 }, (Vector3){ 1, 1.2f, -0.5f }); }

static float ground(Vector3 p, void *user)
{
    (void)user;
    float g = 0;
    for (int i = 0; i < nboxes; i++) {
        const Box *b = &boxes[i];
        if (p.x >= b->lo.x && p.x <= b->hi.x && p.z >= b->lo.z && p.z <= b->hi.z && b->hi.y <= p.y + 1e-4f && b->hi.y > g)
            g = b->hi.y;
    }
    return g;
}

static void scene(void)
{
    if (F.wall_hold) wall_hold = vrui_handhold(1, WALL_HOLD_AT, WALL_HOLD_AT, 0.03f, ORANGE);
    if (F.edge_bar) edge_bar = vrui_handhold(2, EDGE_A, EDGE_B, 0.025f, ORANGE);
    if (F.bars)
        for (int i = 0; i < 3; i++)
            bar[i] = vrui_handhold(10 + i, (Vector3){ -0.5f, BAR_Y, BAR_Z[i] }, (Vector3){ 0.5f, BAR_Y, BAR_Z[i] }, 0.02f, GRAY);
    vrui_locomotion(&loco);
}

static void setup(void)
{
    loco = vrui_loco_default();
    loco.ground_height = ground;
    loco.fade_seconds = 0;   // tests look at positions, not the blink
}

// The world point of a tracking-space point, and back (hands are scripted in
// tracking space; the rig moves under them while climbing).
static Vector3 stage_of(Vector3 world) { return Vector3Subtract(world, sfxr_rig_position()); }   // rig never turns here
static SfxrPose grip_at(Vector3 stage) { return (SfxrPose){ stage, QuaternionIdentity() }; }

// Put a hand on a world point and close it.
static void grab_world(SfxrHandId h, Vector3 world)
{
    sfxt_hand_to(h, grip_at(stage_of(world)), 0.2f);
    sfxt_grip(h, 1.0f);
    sfxt_frames(2);
}

// --- climbing ---------------------------------------------------------------------

// Pull the hand down 40 cm: the player goes up 40 cm, and the hand stays on the hold.
static void climb_pull_down_rises(void)
{
    setup();
    add_ledge();
    F.wall_hold = true;
    sfxt_frames(1);
    grab_world(SFXR_RIGHT, WALL_HOLD_AT);
    CHECK(wall_hold.held, "took hold of the wall hold");
    CHECK(vrui_climbing(), "climbing");
    sfxt_hand_to(SFXR_RIGHT, grip_at(Vector3Add(sfxt_hand(SFXR_RIGHT).position, (Vector3){ 0, -0.4f, 0 })), 0.6f);
    sfxt_frames(2);
    CHECK_NEAR(sfxr_rig_position().y, 0.4f, 0.02f, "rig height after pulling down 40 cm");
    CHECK_NEAR(Vector3Distance(sfxr_hand(SFXR_RIGHT)->grip.position, WALL_HOLD_AT), 0, 0.02f,
               "hand still on the hold (world)");
}

// A bar can be taken anywhere along it, not just its middle.
static void climb_grab_bar_anywhere(void)
{
    setup();
    add_ledge();
    F.edge_bar = true;
    sfxt_frames(1);
    grab_world(SFXR_RIGHT, (Vector3){ 0.45f, 1.2f, -0.5f });
    CHECK(edge_bar.held, "took hold of the ledge edge 45 cm from its middle");
}

// Two hands on, the newer one leads. Climb on it, then let it go: the older
// hand takes over from where it is now and the player doesn't move.
static void climb_switch_hands_no_jump(void)
{
    setup();
    add_ledge();
    F.wall_hold = true;
    F.edge_bar = true;
    sfxt_frames(1);
    grab_world(SFXR_RIGHT, WALL_HOLD_AT);
    grab_world(SFXR_LEFT, (Vector3){ -0.3f, 1.2f, -0.5f });
    CHECK(wall_hold.held && edge_bar.held, "both hands on");
    sfxt_hand_to(SFXR_LEFT, grip_at(Vector3Add(sfxt_hand(SFXR_LEFT).position, (Vector3){ 0, -0.3f, 0 })), 0.5f);
    sfxt_frames(2);
    // The right hand went up with the player, so its old grab point is 30 cm below it.
    // Hold the right hand still in tracking space while the left lets go.
    sfxt_hand_to(SFXR_RIGHT, sfxt_hand(SFXR_RIGHT), 0);
    float before = sfxr_rig_position().y;
    CHECK_NEAR(before, 0.3f, 0.02f, "left hand lifted the player");
    sfxt_grip(SFXR_LEFT, 0);
    sfxt_frames(10);
    CHECK(wall_hold.held, "right hand still on");
    CHECK_NEAR(sfxr_rig_position().y, before, 0.01f, "player height when the leading hand let go");
}

// Let go of the wall: back down to the floor.
static void climb_let_go_falls(void)
{
    setup();
    add_ledge();
    F.wall_hold = true;
    sfxt_frames(1);
    grab_world(SFXR_RIGHT, WALL_HOLD_AT);
    sfxt_hand_to(SFXR_RIGHT, grip_at(Vector3Add(sfxt_hand(SFXR_RIGHT).position, (Vector3){ 0, -0.5f, 0 })), 0.6f);
    CHECK(sfxr_rig_position().y > 0.4f, "climbed");
    sfxt_grip(SFXR_RIGHT, 0);
    sfxt_frames(5);
    CHECK(!vrui_climbing(), "not climbing");
    CHECK_NEAR(sfxr_rig_position().y, 0, 0.01f, "back on the floor after letting go");
    CHECK(!vrui_airborne(), "landed");
}

// Pull up on the ledge edge until your head is over the top, let go: you
// stand on the ledge.
static void climb_over_ledge_lands_on_top(void)
{
    setup();
    add_ledge();
    F.edge_bar = true;
    sfxt_frames(1);
    grab_world(SFXR_RIGHT, (Vector3){ 0.2f, 1.2f, -0.5f });
    Vector3 h = sfxt_hand(SFXR_RIGHT).position;
    sfxt_hand_to(SFXR_RIGHT, grip_at(Vector3Add(h, (Vector3){ 0, -0.6f, 0 })), 0.6f);   // up
    sfxt_hand_to(SFXR_RIGHT, grip_at(Vector3Add(h, (Vector3){ 0, -0.6f, 0.7f })), 0.6f); // and over
    CHECK(sfxr_head().position.z < -0.55f, "head is over the ledge (z %.2f)", sfxr_head().position.z);
    sfxt_grip(SFXR_RIGHT, 0);
    sfxt_frames(5);
    CHECK_NEAR(sfxr_rig_position().y, 1.2f, 0.01f, "standing on the ledge");
}

// Hand over hand along three bars overhead.
static void monkey_bars_hand_over_hand(void)
{
    setup();
    F.bars = true;
    sfxt_frames(1);
    grab_world(SFXR_RIGHT, (Vector3){ 0.2f, BAR_Y, BAR_Z[0] });
    CHECK(bar[0].held, "right hand on bar 1");
    // swing forward: pull the hand back toward the body
    sfxt_hand_to(SFXR_RIGHT, grip_at(Vector3Add(sfxt_hand(SFXR_RIGHT).position, (Vector3){ 0, 0, 0.4f })), 0.5f);
    grab_world(SFXR_LEFT, (Vector3){ -0.2f, BAR_Y, BAR_Z[1] });
    CHECK(bar[1].held, "left hand on bar 2");
    sfxt_grip(SFXR_RIGHT, 0);
    sfxt_frames(2);
    sfxt_hand_to(SFXR_LEFT, grip_at(Vector3Add(sfxt_hand(SFXR_LEFT).position, (Vector3){ 0, 0, 0.5f })), 0.5f);
    grab_world(SFXR_RIGHT, (Vector3){ 0.2f, BAR_Y, BAR_Z[2] });
    CHECK(bar[2].held, "right hand on bar 3");
    CHECK(sfxr_rig_position().z < -0.8f, "moved along the bars (rig z %.2f)", sfxr_rig_position().z);
    CHECK_NEAR(sfxr_rig_position().y, 0, 0.02f, "height unchanged (arms level)");
}

// --- standing on surfaces --------------------------------------------------------

// Walking onto a 20 cm stair steps you up; walking into a 1 m table doesn't.
static void steps_yes_tables_no(void)
{
    setup();
    add_box((Vector3){ 0.5f, 0, -0.3f }, (Vector3){ 1.5f, 0.2f, 0.3f });     // stair to the right
    add_box((Vector3){ -1.5f, 0, -0.3f }, (Vector3){ -0.5f, 1.0f, 0.3f });   // table to the left
    sfxt_frames(1);
    sfxt_head_to((SfxrPose){ { -0.8f, 1.6f, 0 }, QuaternionIdentity() }, 0.5f);
    sfxt_frames(3);
    CHECK_NEAR(sfxr_rig_position().y, 0, 0.01f, "still on the floor with the head over the table");
    sfxt_head_to((SfxrPose){ { 0.8f, 1.6f, 0 }, QuaternionIdentity() }, 0.8f);
    sfxt_frames(3);
    CHECK_NEAR(sfxr_rig_position().y, 0.2f, 0.01f, "up on the stair");
}

// Standing on a block, walk past its edge: you come down.
static void walk_off_edge_falls(void)
{
    setup();
    add_box((Vector3){ -1, 0, -1 }, (Vector3){ 0.5f, 0.6f, 1 });
    sfxr_rig_set((Vector3){ 0, 0.6f, 0 }, 0);
    sfxt_frames(3);
    CHECK_NEAR(sfxr_rig_position().y, 0.6f, 0.01f, "standing on the block");
    sfxt_head_to((SfxrPose){ { 1.0f, 1.6f, 0 }, QuaternionIdentity() }, 0.6f);
    sfxt_frames(3);
    CHECK_NEAR(sfxr_rig_position().y, 0, 0.01f, "down on the floor past the edge");
}

// --- teleport ------------------------------------------------------------------------

// The right hand at the hip, pointing behind the player and 20 degrees up.
static SfxrPose aim_back(void)
{
    Quaternion q = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, PI),
                                      QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 20 * DEG2RAD));
    return (SfxrPose){ { 0.2f, 1.2f, 0.3f }, q };
}

// Where vrui's arc lands on a flat floor from `aim` (the same integration).
static Vector3 predict_landing(SfxrPose aim)
{
    Vector3 p = aim.position, v = Vector3Scale(sfxr_pose_forward(aim), 7.0f);
    for (int i = 0; i < 120; i++) {
        Vector3 np = Vector3Add(p, Vector3Scale(v, 0.025f));
        v.y -= 9.8f * 0.025f;
        if (np.y <= 0) return Vector3Lerp(p, np, p.y / (p.y - np.y));
        p = np;
    }
    return p;
}

static void teleport(SfxrPose aim)
{
    sfxt_hand_to(SFXR_RIGHT, aim, 0.2f);
    sfxt_stick(SFXR_RIGHT, (Vector2){ 0, 1 });
    sfxt_frames(4);
    sfxt_stick(SFXR_RIGHT, (Vector2){ 0, 0 });
    sfxt_frames(3);
}

// Aimed at a raised platform, the arc lands on top of it.
static void teleport_lands_on_platform(void)
{
    setup();
    add_box((Vector3){ -2, 0, 3 }, (Vector3){ 2, 0.6f, 7 });
    sfxt_frames(1);
    teleport(aim_back());
    Vector3 feet = sfxr_head_floor_point();
    CHECK(feet.z > 3.0f, "went to the platform (z %.2f)", feet.z);
    CHECK_NEAR(feet.y, 0.6f, 0.01f, "standing on top of the platform");
}

// Landing near a pad puts you on its center, turned the way it says.
static void teleport_pad_snaps_and_faces(void)
{
    setup();
    Vector3 land = predict_landing(aim_back());
    pads[0] = (VruiTeleportPad){ Vector3Add(land, (Vector3){ 0.35f, 0, 0.25f }), 0.8f, true, 90 };
    loco.pads = pads;
    loco.npads = 1;
    sfxt_frames(1);
    teleport(aim_back());
    Vector3 feet = sfxr_head_floor_point();
    CHECK_NEAR(Vector2Distance((Vector2){ feet.x, feet.z }, (Vector2){ pads[0].center.x, pads[0].center.z }), 0, 0.01f,
               "distance from the pad center");
    Vector3 f = sfxr_pose_forward(sfxr_head());
    CHECK(f.x > 0.98f, "facing +X, the pad's yaw 90 (forward %.2f %.2f %.2f)", f.x, f.y, f.z);
}

// pads_only: landing away from every pad is not a valid target.
static void teleport_pads_only_rejects_elsewhere(void)
{
    setup();
    Vector3 land = predict_landing(aim_back());
    pads[0] = (VruiTeleportPad){ Vector3Add(land, (Vector3){ 3, 0, 0 }), 0.5f, false, 0 };
    loco.pads = pads;
    loco.npads = 1;
    loco.pads_only = true;
    sfxt_frames(1);
    teleport(aim_back());
    Vector3 feet = sfxr_head_floor_point();
    CHECK(Vector3Length(feet) < 0.05f, "stayed put (at %.2f %.2f %.2f)", feet.x, feet.y, feet.z);
}

static const SfxtCase CASES[] = {
    { "move/climb-pull-down-rises",          climb_pull_down_rises,          "vrui_climb_fixed_world" },
    { "move/climb-grab-bar-anywhere",        climb_grab_bar_anywhere,        "vrui_handhold_point_only" },
    { "move/climb-switch-hands-no-jump",     climb_switch_hands_no_jump,     "vrui_climb_no_reanchor" },
    { "move/climb-let-go-falls",             climb_let_go_falls,             "vrui_loco_no_fall" },
    { "move/climb-over-ledge-lands-on-top",  climb_over_ledge_lands_on_top,  "vrui_loco_no_mantle" },
    { "move/monkey-bars-hand-over-hand",     monkey_bars_hand_over_hand,     "vrui_climb_fixed_world" },
    { "move/steps-yes-tables-no",            steps_yes_tables_no,            "vrui_loco_step_any_height" },
    { "move/walk-off-edge-falls",            walk_off_edge_falls,            "vrui_loco_no_fall" },
    { "move/teleport-lands-on-platform",     teleport_lands_on_platform,     "vrui_loco_arc_floor_only" },
    { "move/teleport-pad-snaps-and-faces",   teleport_pad_snaps_and_faces,   "vrui_loco_no_pad_snap" },
    { "move/teleport-pads-only-rejects-elsewhere", teleport_pads_only_rejects_elsewhere, "vrui_loco_pads_only_ignored" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
