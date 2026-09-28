// rotary.c - things that turn: knob, selector, crank.

#include "scene.h"

#include <stdlib.h>
#include <string.h>

void knob_orbit_quarter_turn(void)
{
    // Drag the knob a quarter turn clockwise by moving the hand around it
    // (the wrist stays still): a lazy susan.
    Vector3 c = knob_center(KNOB_AT);
    grab_at(around(c, 0.03f, 0));
    CHECK(knob_m.held, "the knob is held after gripping at its rim");
    Orbit o = { c, 0.03f, 0, -PI / 2, 0, 0, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.6f);
    settle();
    CHECK_NEAR(knob, knob_after(90, 0.03f), 0.02f, "a quarter of its 3/4-turn travel");
    let_go();
    CHECK(!knob_m.held, "letting go releases it");
}

void knob_twist_quarter_turn(void)
{
    // Grab it dead center and twist the wrist a quarter turn clockwise.
    Vector3 c = knob_center(KNOB_AT);
    grab_at(c);
    Orbit o = { c, 0, 0, 0, 0, 0, -PI / 2, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.6f);
    settle();
    CHECK_NEAR(knob, knob_after(90, 0), 0.02f, "wrist twist turns it the same amount");
    let_go();
}

void knob_press_down_while_turning(void)
{
    // People lean on a knob while they turn it: here the hand pushes 2 cm
    // down along the axis and wobbles 8 mm in and out. Only the turn counts.
    Vector3 c = knob_center(KNOB_AT);
    grab_at(around(c, 0.03f, 0));
    Orbit o = { c, 0.03f, 0, -PI / 2, 0.02f, 0.008f, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.6f);
    settle();
    CHECK(knob_m.held, "still held after pressing down");
    CHECK_NEAR(knob, knob_after(90, 0.03f), 0.025f, "pressing down and wobbling don't change the turn");
    let_go();
}

void knob_side_push_while_twisting(void)
{
    // Twisting at the center, the hand slides 1 cm side to side across the
    // axis. Near the axis the hand's angle around it is meaningless, so it
    // must not spin the knob.
    Vector3 c = knob_center(KNOB_AT);
    grab_at(c);
    Orbit o = { c, 0, 0, 0, 0, 0, -PI / 2, 0.01f, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.8f);
    settle();
    CHECK_NEAR(knob, knob_after(90, 0), 0.03f, "only the twist counts");
    let_go();
}

void knob_grab_does_not_nudge(void)
{
    // Taking hold with a shaky hand (1.5 mm, 0.5 deg) and holding for a second
    // leaves the value exactly where it was.
    sfxt_noise(0.0015f, 0.5f, 0.01f);
    Vector3 c = knob_center(KNOB_AT);
    grab_at(around(c, 0.03f, 0));
    sfxt_wait(1.0f);
    CHECK(knob == 0.5f, "value untouched by tremor (got %.4f)", knob);
    let_go();
    CHECK(knob == 0.5f, "and by letting go (got %.4f)", knob);
}

void knob_grab_off_axis_no_jump(void)
{
    // Grabbing the rim on the far side must not make it jump.
    sfxt_noise(0, 0, 0);
    Vector3 c = knob_center(KNOB_AT);
    grab_at(around(c, 0.03f, PI / 2));
    sfxt_wait(0.3f);
    CHECK(knob == 0.5f, "no jump on grab (got %.4f)", knob);
    let_go();
}

void knob_hold_survives_drift(void)
{
    // Once held, only letting go ends it: the hand wanders 25 cm off (straight
    // out and up, so the angle around the knob doesn't change).
    Vector3 c = knob_center(KNOB_AT);
    Vector3 start = around(c, 0.03f, 0);
    grab_at(start);
    sfxt_hand_to(R, pose(add(start, v3(0.2f, 0.15f, 0)), PALM_DOWN), 0.5f);
    CHECK(knob_m.held, "still held 25 cm away");
    CHECK_NEAR(knob, 0.5f, 0.02f, "moving straight out doesn't turn it");
    let_go();
    CHECK(!knob_m.held, "released on letting go");
}

void knob_end_stop_holds(void)
{
    // Turn well past the end (180 deg from the middle), then come back 20 deg:
    // it responds at once instead of making you unwind the extra.
    Vector3 c = knob_center(KNOB_AT);
    grab_at(around(c, 0.03f, 0));
    int bumps0 = sfxt_haptic_count(R);
    Orbit o = { c, 0.03f, 0, -PI, 0, 0, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.8f);
    // (tremor at the stop may ease it back a hair)
    CHECK_NEAR(knob, 1.0f, 0.01f, "stops at the maximum");
    CHECK(sfxt_haptic_count(R) > bumps0, "the stop is felt");
    Orbit back = { c, 0.03f, -PI, -PI + 20.0f * DEG2RAD, 0, 0, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &back, 0.3f);
    settle();
    CHECK_NEAR(knob, 1.0f - 20.0f / 270.0f, 0.02f, "coming back responds immediately");
    let_go();
}

void knob_laser_orbit(void)
{
    // From a step back: point the laser at the knob, pull the trigger, and
    // swing the laser spot a quarter turn around it.
    sfxt_noise(0.0005f, 0.05f, 0.01f);
    Vector3 c = add(knob_center(KNOB_AT), v3(0, 0.0125f, 0));   // the top face
    LaserOrbit o = { v3(0.1f, 1.3f, 0.0f), c, 0.028f, 0, -PI / 2 };
    sfxt_hand_to(R, laser_orbit_path(0, &o), 0.3f);
    sfxt_frames(2);
    sfxt_trigger(R, 1.0f);
    sfxt_frames(3);
    CHECK(knob_m.held && knob_m.via_ray, "held by the laser");
    sfxt_hand_path(R, laser_orbit_path, &o, 0.6f);
    settle();
    CHECK_NEAR(knob, knob_after(90, 0.028f), 0.035f, "the laser spot drags it around");
    let_go();
}

void knob_laser_fast_spin_is_limited(void)
{
    // Circling the laser around the knob's axis is a tiny wrist motion but a
    // huge rotation. Two full circles in a quarter second must not spin the
    // knob: by laser it turns at most 1 turn/s, and you feel it strain.
    sfxt_noise(0.0005f, 0.05f, 0.01f);
    knob = 0.0f;
    sfxt_frames(2);
    Vector3 c = add(knob_center(KNOB_AT), v3(0, 0.0125f, 0));
    LaserOrbit o = { v3(0.1f, 1.3f, 0.0f), c, 0.028f, 0, -4.0f * PI };
    sfxt_hand_to(R, laser_orbit_path(0, &o), 0.3f);
    sfxt_frames(2);
    sfxt_trigger(R, 1.0f);
    sfxt_frames(3);
    CHECK(knob_m.held && knob_m.via_ray, "held by the laser");
    int h0 = sfxt_haptic_count(R);
    sfxt_hand_path(R, laser_orbit_path, &o, 0.25f);
    CHECK(knob <= (360.0f * 0.25f + 10.0f) / 270.0f, "at most ~90 deg in 0.25 s (got %.0f deg)", knob * 270.0f);
    CHECK(sfxt_haptic_count(R) - h0 >= 8, "strain is felt (%d haptic updates)", sfxt_haptic_count(R) - h0);
    let_go();
}

void selector_turn_snaps(void)
{
    // 30 deg per position: turning 40 deg clockwise moves exactly one
    // position, and it never rests between positions on the way.
    Vector3 c = knob_center(SELECTOR_AT);
    grab_at(around(c, 0.03f, 0));
    float worst = 0;
    for (int i = 1; i <= 40; i++) {
        Orbit o = { c, 0.03f, -(float)(i - 1) * DEG2RAD, -(float)i * DEG2RAD, 0, 0, 0, 0, PALM_DOWN };
        sfxt_hand_path(R, orbit_path, &o, 1.0f / 72.0f);
        float off = fabsf(selector - roundf(selector));
        if (off > worst) worst = off;
    }
    CHECK(worst == 0.0f, "always exactly on a position (worst %.3f off)", worst);
    CHECK(selector == 3.0f, "one position clockwise (got %.2f)", selector);
    let_go();
}

void selector_no_chatter_at_boundary(void)
{
    // A hand resting anywhere around the halfway point between two positions,
    // trembling, must never make it flicker between them. Rest at positions
    // 0.40 .. 0.70 of the way to the next stop, half a second each: across
    // the whole sweep it may change position once, never back and forth.
    sfxt_noise(0.0015f, 0.3f, 0.01f);
    Vector3 c = knob_center(SELECTOR_AT);
    grab_at(around(c, 0.03f, 0));
    // break-in: 3 mm of hand travel at the 3 cm grab radius (vrui_mech.c)
    float slop = 0.003f / 0.03f;
    float prev = 0;
    selector_changes = 0;
    for (float pos = 0.40f; pos <= 0.701f; pos += 0.05f) {
        float a = -(pos * 30.0f * DEG2RAD + slop);
        Orbit o = { c, 0.03f, prev, a, 0, 0, 0, 0, PALM_DOWN };
        sfxt_hand_path(R, orbit_path, &o, 0.1f);
        prev = a;
        sfxt_wait(0.5f);
    }
    CHECK(selector_changes <= 1, "at most one change across the boundary (got %d)", selector_changes);
    let_go();
}

void crank_two_turns(void)
{
    // Grab the handle on the rim and wind two full turns clockwise.
    Vector3 c = add(CRANK_AT.position, v3(0, 0.02f, 0));
    grab_at(around(c, 0.09f, 0));
    int ticks0 = sfxt_haptic_count(R);
    Orbit o = { c, 0.09f, 0, -4.0f * PI, 0, 0.005f, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 2.0f);
    settle();
    CHECK_NEAR(crank, 2.0f, 0.03f, "two turns");
    CHECK(sfxt_haptic_count(R) - ticks0 >= 20, "ratchet clicks felt (%d)", sfxt_haptic_count(R) - ticks0);
    let_go();
}

void knob_events_name_it(void)
{
    // The event log (SFXR_EVENTS, set by scripts/test.sh for every case) says
    // what was grabbed by name, and the value it was let go at.
    const char *path = getenv("SFXR_EVENTS");
    if (!path || !*path) { CHECK(false, "run through scripts/test.sh (it sets SFXR_EVENTS)"); return; }
    Vector3 c = knob_center(KNOB_AT);
    grab_at(around(c, 0.03f, 0));
    Orbit o = { c, 0.03f, 0, -PI / 2, 0, 0, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.6f);
    let_go();
    sfxt_frames(2);   // the log is flushed at every frame end
    char text[8192] = "";
    FILE *f = fopen(path, "r");
    if (f) { size_t n = fread(text, 1, sizeof text - 1, f); text[n] = 0; fclose(f); }
    CHECK(strstr(text, "grab       KNOB R hand") != NULL, "grab logged by name:\n%s", text);
    CHECK(strstr(text, "release    KNOB R") != NULL, "release logged");
    CHECK(strstr(text, "value      KNOB 0.") != NULL, "value at release logged");
}
