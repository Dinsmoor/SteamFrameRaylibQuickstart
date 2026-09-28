// tests/garden - the Garden keeps its promises (docs/GARDEN.md): it's the
// swing of the hammer's HEAD that smashes a bug, a hammer resting on a bug
// does nothing, a bug bites once a second (not every frame), and a hammer
// put on your belt goes where you go. The toolbox's own garden code is
// compiled in unchanged (the toolbox_*.c files here just include it).
//
//   make test T=garden

#include "sfxt.h"
#include "../../examples/toolbox/garden.h"

#include <math.h>

static VruiLocoConfig loco;

static void scene(void) { garden_update(&loco); }

static Vector3 stage_of(Vector3 world) { return Vector3Subtract(world, sfxr_rig_position()); }   // the rig doesn't turn here

static void setup(void)
{
    sfxt_noise(0, 0, 0);
    loco = vrui_loco_default();
    garden_enter();
    garden_test_feel(VRUI_SMOOTH_SNAP);   // the hammer exactly on the hand: these cases test the game, not the smoothing
    sfxt_frames(3);
    CHECK(garden_active(), "in the garden (are the models in examples/toolbox/resources/garden?)");
}

// Take the hammer from the gardener, 20 cm down its handle from the middle,
// with the hand turned the way the hammer is (so hand and hammer then turn
// together).
static void take_hammer(void)
{
    GardenState g = garden_state();
    Vector3 down = Vector3RotateByQuaternion((Vector3){ 0, -0.2f, 0 }, g.hammer.orientation);
    SfxrPose at = { stage_of(Vector3Add(g.hammer.position, down)), g.hammer.orientation };
    sfxt_hand_to(SFXR_RIGHT, at, 0.3f);
    at.position = stage_of(Vector3Add(garden_state().hammer.position, down));   // he sways a little
    sfxt_hand_set(SFXR_RIGHT, at);
    sfxt_frames(2);
    sfxt_grip(SFXR_RIGHT, 1.0f);
    sfxt_frames(3);
    g = garden_state();
    CHECK(g.hammer_at == 1, "the hammer is in your hand (it's %d)", g.hammer_at);
    CHECK(g.round == 1, "taking it starts the round");
}

// The hand held `back` behind a bug, the hammer turned `deg` about X from
// upright (-90: its head pointing straight at the bug, 57 cm out).
static SfxrPose swing_pose(Vector3 bug, float back, float deg)
{
    return (SfxrPose){ stage_of(Vector3Add(bug, (Vector3){ 0, 0, back })), QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, deg * DEG2RAD) };
}

// A real swing: the hand stays 75 cm from the bug, the head (57 cm out
// along the handle) whips down through it.
static void swing_smashes_with_the_head(void)
{
    setup();
    take_hammer();
    Vector3 feet = sfxr_head_floor_point();
    garden_test_bug(0, feet.x, feet.z - 1.6f);
    sfxt_frames(1);
    Vector3 bug = garden_state().first_bug;
    sfxt_hand_to(SFXR_RIGHT, swing_pose(bug, 0.75f, 20), 2.5f);   // slowly into position
    CHECK(garden_state().score == 0, "nothing smashed on the way into position");
    sfxt_hand_to(SFXR_RIGHT, swing_pose(bug, 0.75f, -160), 0.25f);
    sfxt_frames(5);
    GardenState g = garden_state();
    CHECK(g.score == 1, "the head smashed the bug (score %d, %d bugs left)", g.score, g.bugs);
}

// Resting the head on a bug, however long, does nothing: it takes a swing.
static void resting_on_a_bug_does_nothing(void)
{
    setup();
    take_hammer();
    Vector3 feet = sfxr_head_floor_point();
    garden_test_bug(0, feet.x, feet.z - 1.6f);   // a red one: one hit would do it
    sfxt_frames(1);
    Vector3 bug = garden_state().first_bug;
    sfxt_hand_to(SFXR_RIGHT, swing_pose(bug, 1.2f, -90), 0.8f);
    sfxt_hand_to(SFXR_RIGHT, swing_pose(garden_state().first_bug, 0.57f, -90), 1.5f);   // slowly in
    sfxt_wait(1.0f);
    GardenState g = garden_state();
    CHECK(g.score == 0 && g.bugs == 1, "still there after being leant on (score %d)", g.score);
}

// A bug at your feet bites once, then backs off: one bite in half a second.
static void bug_bites_once_a_second(void)
{
    setup();
    garden_test_start();
    Vector3 feet = sfxr_head_floor_point();
    garden_test_bug(1, feet.x + 0.5f, feet.z);   // blue: 1 damage a bite
    sfxt_wait(0.6f);
    GardenState g = garden_state();
    CHECK(g.hp == 9, "one bite in 0.6 s (health %d of 10)", g.hp);
}

// On your hip it rides your belt: walk (or teleport) and it comes too.
static void hammer_rides_the_belt(void)
{
    setup();
    take_hammer();
    GardenState g = garden_state();
    SfxrPose at = { stage_of(g.belt.position), g.belt.orientation };
    at.position = Vector3Add(at.position, Vector3RotateByQuaternion((Vector3){ 0, -0.2f, 0 }, g.belt.orientation));
    sfxt_hand_to(SFXR_RIGHT, at, 0.5f);
    sfxt_grip(SFXR_RIGHT, 0);
    sfxt_frames(3);
    g = garden_state();
    CHECK(g.hammer_at == 2, "let go at the hip: on the belt (it's %d)", g.hammer_at);
    Vector3 before = g.hammer.position;
    sfxr_rig_move((Vector3){ 2, 0, 0 });
    sfxt_frames(3);
    g = garden_state();
    CHECK(fabsf(g.hammer.position.x - before.x - 2.0f) < 0.1f, "moved 2 m with you (moved %.2f m)", g.hammer.position.x - before.x);
}

static const SfxtCase CASES[] = {
    { "garden/swing-smashes-with-the-head",  swing_smashes_with_the_head,  "garden_hit_at_hand" },
    { "garden/resting-on-a-bug-does-nothing", resting_on_a_bug_does_nothing, "garden_hit_any_speed" },
    { "garden/bug-bites-once-a-second",      bug_bites_once_a_second,      "garden_bite_every_frame" },
    { "garden/hammer-rides-the-belt",        hammer_rides_the_belt,        "garden_belt_world_space" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
