// pivot.c - levers, sprung levers and set points.

#include "scene.h"

void lever_push_forward(void)
{
    // Push the handle 30 deg forward (away from you) along its arc.
    LeverPath l = { lever_pivot(), 0.18f, 0, -30.0f * DEG2RAD, { 0, 0, 0 } };
    grab_at(lever_path(0, &l).position);
    CHECK(lever_m.held, "the handle is held");
    sfxt_hand_path(R, lever_path, &l, 0.5f);
    settle();
    CHECK_NEAR(lever, 0.5f + 30.0f / 80.0f, 0.03f, "30 of 80 degrees");
    let_go();
}

void lever_sideways_push_ignored(void)
{
    // The same push while the hand drifts 4 cm sideways (along the pivot) and
    // 2 cm down: a lever only swings one way, so that drift is ignored.
    LeverPath l = { lever_pivot(), 0.18f, 0, -30.0f * DEG2RAD, { 0.04f, -0.02f, 0 } };
    grab_at(lever_path(0, &l).position);
    sfxt_hand_path(R, lever_path, &l, 0.5f);
    settle();
    CHECK_NEAR(lever, 0.5f + 30.0f / 80.0f, 0.03f, "sideways drift doesn't move it");
    let_go();
}

void lever_grab_no_jump(void)
{
    // A lever sitting far forward, grabbed and held: it stays put.
    sfxt_noise(0, 0, 0);
    lever = 0.9f;
    sfxt_frames(2);
    float a = 40.0f * DEG2RAD - 0.9f * 80.0f * DEG2RAD;
    LeverPath l = { lever_pivot(), 0.18f, a, a, { 0, 0, 0 } };
    grab_at(lever_path(0, &l).position);
    sfxt_wait(0.3f);
    CHECK(lever == 0.9f, "no jump on grab (got %.4f)", lever);
    let_go();
}

void sprung_lever_returns_to_set_point(void)
{
    // The set point comes from elsewhere (a pin, a knob, the app): pull the
    // lever back, let go, and it returns to wherever the set point is now.
    set_point = 0.8f;
    sfxt_wait(0.5f);   // it glides to the new set point on its own
    CHECK_NEAR(sprung, 0.8f, 0.005f, "follows a moved set point while free");
    Vector3 pivot = add(SPRUNG_AT.position, v3(0, 0.02f, 0));
    float a = 40.0f * DEG2RAD - 0.8f * 80.0f * DEG2RAD;
    LeverPath lp = { pivot, 0.18f, a, a + 30.0f * DEG2RAD, { 0, 0, 0 } };
    grab_at(lever_path(0, &lp).position);
    sfxt_hand_path(R, lever_path, &lp, 0.4f);
    settle();
    CHECK(sprung < 0.5f, "pulled back (%.2f)", sprung);
    let_go();
    sfxt_wait(0.5f);
    CHECK_NEAR(sprung, 0.8f, 0.005f, "back at the set point");
}
