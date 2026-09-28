// tilt.c - joysticks.

#include "scene.h"

void joystick_tilt_and_return(void)
{
    // Push the ball 3 cm right, let go: it springs back to center.
    Vector3 b = stick_ball();
    grab_at(b);
    Line l = { b, { 0.03f, 0, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.3f);
    sfxt_frames(2);
    CHECK_NEAR(stick.x, (0.03f - 0.004f) / 0.06f, 0.03f, "tilted right");
    CHECK_NEAR(stick.y, 0.0f, 0.03f, "not forward/back");
    let_go();
    sfxt_wait(0.5f);
    CHECK(stick.x == 0.0f && stick.y == 0.0f, "centered again (%.3f, %.3f)", stick.x, stick.y);
}

void joystick_push_down_ignored(void)
{
    // The same push while pressing 3 cm down on the stick.
    Vector3 b = stick_ball();
    grab_at(b);
    Line l = { b, { 0.03f, -0.03f, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.3f);
    sfxt_frames(2);
    CHECK_NEAR(stick.x, (0.03f - 0.004f) / 0.06f, 0.03f, "pressing down doesn't change the tilt");
    let_go();
}
