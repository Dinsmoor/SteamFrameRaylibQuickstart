// linear.c - sliders and plungers.

#include "scene.h"

void slider_off_center_grab(void)
{
    // Grab the handle 1.5 cm right of its center and slide 10 cm right.
    Vector3 h = add(slider_handle(SLIDER_AT, 0.5f, 0.25f), v3(0.015f, 0, 0));
    grab_at(h);
    CHECK(slider_m.held, "held");
    CHECK(slider == 0.5f, "no jump on an off-center grab (got %.4f)", slider);
    Line l = { h, { 0.1f, 0, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.5f);
    settle();
    CHECK_NEAR(slider, 0.5f + (0.1f - 0.004f) / 0.25f, 0.02f, "10 cm of 25 (less the 4 mm break-in)");
    let_go();
}

void slider_lift_and_lean_ignored(void)
{
    // The same slide while the hand lifts 3 cm and leans 2 cm across the track.
    Vector3 h = slider_handle(SLIDER_AT, 0.5f, 0.25f);
    grab_at(h);
    Line l = { h, { 0.1f, 0.03f, 0 }, { 0, 0, 0.02f } };
    sfxt_hand_path(R, line_path, &l, 0.5f);
    settle();
    CHECK_NEAR(slider, 0.5f + (0.1f - 0.004f) / 0.25f, 0.02f, "only motion along the track counts");
    let_go();
}

void slider_laser(void)
{
    // From a step back: point 1.4 cm right of the handle's center, pull the
    // trigger, sweep the spot 10 cm right.
    sfxt_noise(0.0005f, 0.05f, 0.01f);
    Vector3 h = add(slider_handle(SLIDER_AT, 0.5f, 0.25f), v3(0.014f, 0.01f, 0));
    LaserLine o = { v3(0.1f, 1.4f, -0.3f), h, { 0.1f, 0, 0 } };
    sfxt_hand_to(R, laser_line_path(0, &o), 0.3f);
    sfxt_frames(2);
    sfxt_trigger(R, 1.0f);
    sfxt_frames(3);
    CHECK(slider_m.held && slider_m.via_ray, "held by the laser");
    sfxt_hand_path(R, laser_line_path, &o, 0.5f);
    settle();
    CHECK_NEAR(slider, 0.5f + (0.1f - 0.004f) / 0.25f, 0.025f, "the spot drags it");
    let_go();
}

void plunger_pull_and_return(void)
{
    // Pull the sprung handle 9 cm, let go: it glides home.
    Vector3 h = slider_handle(PLUNGER_AT, 0.0f, 0.15f);
    grab_at(h);
    Line l = { h, { 0.09f, 0, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.4f);
    settle();
    CHECK_NEAR(plunger, (0.09f - 0.004f) / 0.15f, 0.02f, "pulled out");
    let_go();
    sfxt_wait(0.5f);
    CHECK(plunger == 0.0f, "back home half a second after letting go (at %.4f)", plunger);
}

void plunger_tension_hum(void)
{
    // Holding a sprung handle away from rest, you feel it wanting to go back:
    // a hum re-sent every frame while held out.
    Vector3 h = slider_handle(PLUNGER_AT, 0.0f, 0.15f);
    grab_at(h);
    Line l = { h, { 0.09f, 0, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.4f);
    int h0 = sfxt_haptic_count(R);
    sfxt_wait(0.5f);
    CHECK(sfxt_haptic_count(R) - h0 >= 30, "tension hum while held out (%d updates in 0.5 s)", sfxt_haptic_count(R) - h0);
    let_go();
}
