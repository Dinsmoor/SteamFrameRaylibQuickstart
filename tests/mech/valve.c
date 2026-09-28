// valve.c - the two-handed valve, and the key switch.

#include "scene.h"

#define L SFXR_LEFT

// Both hands on the rim, opposite each other, then both walk a quarter turn
// round it (clockwise from above), a frame at a time.
static void two_hand_turn(float from_l, float from_r, float deg, float seconds, bool left_too)
{
    int n = (int)(seconds * 72.0f);
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n;
        if (left_too) sfxt_hand_set(L, pose(valve_rim(from_l + deg * t), PALM_DOWN));
        sfxt_hand_set(R, pose(valve_rim(from_r + deg * t), PALM_DOWN));
        sfxt_frames(1);
    }
}

static void grab_rim(bool left_too)
{
    if (left_too) sfxt_hand_set(L, pose(valve_rim(0), PALM_DOWN));
    sfxt_hand_set(R, pose(valve_rim(180), PALM_DOWN));
    sfxt_frames(3);
    if (left_too) sfxt_grip(L, 1.0f);
    sfxt_grip(R, 1.0f);
    sfxt_frames(3);
}

void valve_two_hands_turn_it(void)
{
    grab_rim(true);
    CHECK(valve_m.holders == 2, "both hands on the rim (%d)", valve_m.holders);
    two_hand_turn(0, 180, 90, 1.0f, true);
    settle();
    // a quarter of one turn, of the three it takes to open fully, less the break-in
    CHECK_NEAR(valve, (90.0f - 2.0f) / 1080.0f, 0.008f, "a quarter turn opens it by 1/12");
    sfxt_grip(L, 0);
    let_go();
}

void valve_one_hand_wont_budge(void)
{
    grab_rim(false);
    CHECK(valve_m.holders == 1, "one hand on the rim");
    sfxt_haptic_reset(R);
    two_hand_turn(0, 180, 60, 0.8f, false);
    settle();
    CHECK(valve < 0.001f, "one hand can't turn it (%.3f)", valve);
    CHECK(sfxt_haptic_max(R) > 0.2f, "it strains against your hand (haptic %.2f)", sfxt_haptic_max(R));
    let_go();
}

// ---------------------------------------------------------------------------

// Pick the key up by its bow, then carry it so its tip is `above` over the slot.
static void key_to_slot(float above)
{
    grab_at(key_bow(key_pose));
    CHECK(key_r.held, "the key is in hand");
    Vector3 tip = key_r.key.position;
    Vector3 target = add(KEY_SLOT_AT.position, v3(0, above, 0));
    SfxrPose h = sfxt_hand(R);
    sfxt_hand_to(R, pose(add(h.position, Vector3Subtract(target, tip)), h.orientation), 0.6f);
    sfxt_frames(3);
}

static void twist(float deg_clockwise, float seconds)
{
    SfxrPose h = sfxt_hand(R);
    h.orientation = QuaternionMultiply(QuaternionFromAxisAngle(v3(0, 1, 0), -deg_clockwise * DEG2RAD), h.orientation);
    sfxt_hand_to(R, h, seconds);
    sfxt_frames(3);
}

void key_insert_then_turn(void)
{
    key_to_slot(0.005f);
    CHECK(key_r.inserted, "the key went in");
    twist(45, 0.5f);
    CHECK(key_pos == 1, "turned to ON without letting go (at %d)", key_pos);
    let_go();
    sfxt_wait(0.3f);
    CHECK(key_r.inserted && key_pos == 1, "stays in, at ON, after letting go (%d)", key_pos);
}

void key_sideways_does_not_go_in(void)
{
    grab_at(key_bow(key_pose));
    twist(0, 0);
    // roll the hand 90 degrees: the key now lies on its side
    SfxrPose h = sfxt_hand(R);
    h.orientation = QuaternionMultiply(QuaternionFromAxisAngle(v3(0, 0, 1), 90 * DEG2RAD), h.orientation);
    sfxt_hand_to(R, h, 0.3f);
    sfxt_frames(2);
    Vector3 tip = key_r.key.position;
    h = sfxt_hand(R);
    sfxt_hand_to(R, pose(add(h.position, Vector3Subtract(add(KEY_SLOT_AT.position, v3(0, 0.005f, 0)), tip)), h.orientation), 0.6f);
    sfxt_frames(3);
    CHECK(!key_r.inserted, "a key held on its side doesn't go in");
    let_go();
}

void key_pull_out_only_at_off(void)
{
    key_to_slot(0.005f);
    CHECK(key_r.inserted, "the key went in");
    twist(45, 0.4f);
    SfxrPose h = sfxt_hand(R);
    sfxt_hand_to(R, pose(add(h.position, v3(0, 0.07f, 0)), h.orientation), 0.3f);
    sfxt_frames(2);
    CHECK(key_r.inserted, "at ON, pulling doesn't take it out");
    sfxt_hand_to(R, h, 0.3f);
    twist(-45, 0.4f);
    CHECK(key_pos == 0, "back to OFF (%d)", key_pos);
    h = sfxt_hand(R);
    sfxt_hand_to(R, pose(add(h.position, v3(0, 0.07f, 0)), h.orientation), 0.3f);
    sfxt_frames(2);
    CHECK(!key_r.inserted && key_r.held, "at OFF, pulling back takes it out, still in your hand");
    let_go();
}

void key_start_springs_back(void)
{
    key_to_slot(0.005f);
    twist(90, 0.6f);
    CHECK(key_pos == 2, "turned to START (%d)", key_pos);
    let_go();
    sfxt_wait(0.5f);
    CHECK(key_pos == 1, "let go at START: springs back to ON (%d)", key_pos);
}
