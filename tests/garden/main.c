// tests/garden - the Garden keeps its promises (docs/DADDY_BUG_SMASHER.md): it's the
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

// The hand turned to hold a hammer turned `q`: the hammer's handle runs out
// of the thumb side of the fist (the grip pose's -Z), so the fist is the
// hammer's turn plus a quarter turn about X (docs/WIELDING.md).
static Quaternion fist(Quaternion q) { return QuaternionMultiply(q, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2)); }

// Take the hammer off its stump, 20 cm down its handle from the middle,
// with the fist lined up with it (so hand and hammer then turn together).
static void take_hammer(void)
{
    GardenState g = garden_state();
    Vector3 down = Vector3RotateByQuaternion((Vector3){ 0, -0.2f, 0 }, g.hammer.orientation);
    SfxrPose at = { stage_of(Vector3Add(g.hammer.position, down)), fist(g.hammer.orientation) };
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
// upright (-90: its head pointing straight at the bug, 68 cm out).
static SfxrPose swing_pose(Vector3 bug, float back, float deg)
{
    return (SfxrPose){ stage_of(Vector3Add(bug, (Vector3){ 0, 0, back })), fist(QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, deg * DEG2RAD)) };
}

// A real swing: the hand stays 75 cm from the bug, the head (68 cm out
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
    sfxt_hand_to(SFXR_RIGHT, swing_pose(bug, 1.2f, -90), 2.5f);   // slowly: this long hammer's head sweeps fast
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
    SfxrPose at = { stage_of(g.belt.position), fist(g.belt.orientation) };
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

// Taking the headset off mid-round pauses it: the bug beside you doesn't bite
// while you're gone, and coming back you're still paused (a menu waits).
static void pauses_when_the_headset_comes_off(void)
{
    setup();
    garden_test_start();
    Vector3 feet = sfxr_head_floor_point();
    garden_test_bug(1, feet.x + 0.5f, feet.z);
    sfxt_headset_worn(false);
    sfxt_frames(2);
    CHECK(garden_state().paused, "paused when the headset came off");
    sfxt_wait(2.0f);
    CHECK(garden_state().hp == 10, "no bites while you were away (health %d of 10)", garden_state().hp);
    sfxt_headset_worn(true);
    sfxt_wait(0.5f);
    CHECK(garden_state().paused, "still paused when you're back: the menu is waiting");
    CHECK(garden_state().hp == 10, "and still no bites");
}

// The SteamVR dashboard pauses it the same way.
static void pauses_when_the_dashboard_opens(void)
{
    setup();
    garden_test_start();
    sfxt_dashboard(true);
    sfxt_frames(2);
    CHECK(garden_state().paused, "paused while the dashboard is open");
}

static const SfxtCase CASES[] = {
    { "garden/swing-smashes-with-the-head",  swing_smashes_with_the_head,  "garden_hit_at_hand" },
    { "garden/resting-on-a-bug-does-nothing", resting_on_a_bug_does_nothing, "garden_hit_any_speed" },
    { "garden/bug-bites-once-a-second",      bug_bites_once_a_second,      "garden_bite_every_frame" },
    { "garden/hammer-rides-the-belt",        hammer_rides_the_belt,        "garden_belt_world_space" },
    { "garden/pauses-when-the-headset-comes-off", pauses_when_the_headset_comes_off, "garden_ignores_attention" },
    { "garden/pauses-when-the-dashboard-opens",   pauses_when_the_dashboard_opens,   NULL },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
