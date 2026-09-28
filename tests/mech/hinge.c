// hinge.c - a door on its hinge, and a pull cord.

#include "scene.h"

void door_pull_open(void)
{
    // Take the handle and walk it 45 degrees round the hinge, toward you.
    grab_at(door_handle());
    CHECK(door_m.held, "the handle is held");
    Orbit o = { DOOR_HINGE.position, DOOR_W, 0, -45 * DEG2RAD, 0, 0, 0, 0, PALM_DOWN };
    sfxt_hand_path(R, orbit_path, &o, 0.8f);
    sfxt_wait(0.5f);   // it's heavy: let it catch up
    CHECK_NEAR(door, (45.0f - 1.0f) / 100.0f, 0.04f, "open 45 of its 100 degrees (less the 1 degree break-in)");
    CHECK_NEAR(door_m.position, door * 100.0f * DEG2RAD, 0.01f, "part swing matches the value");
    let_go();
}

void door_lean_on_handle_ignored(void)
{
    // Hold the handle and lean on it: 10 cm down along the hinge and back up.
    grab_at(door_handle());
    Line l = { door_handle(), { 0, -0.10f, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.5f);
    settle();
    CHECK(door < 0.02f, "still closed after leaning down on the handle (%.3f)", door);
    let_go();
}

void cord_fires_once_per_pull(void)
{
    // Pull all the way, jiggle at the bottom, let go, pull again: two fires.
    grab_at(cord_handle(0));
    CHECK(sfxt_frame() > 0, "started");
    Line l = { cord_handle(0), { 0, -0.26f, 0 }, { 0, 0, 0 } };
    sfxt_hand_path(R, line_path, &l, 0.5f);
    settle();
    CHECK(cord_fires == 1, "one fire from one pull (%d, pull %.2f)", cord_fires, cord);
    for (int i = 0; i < 6; i++) {   // up to 20 % back and down again, three times
        Vector3 h = sfxt_hand(R).position;
        sfxt_hand_to(R, pose(add(h, v3(0, (i & 1) ? -0.05f : 0.05f, 0)), PALM_DOWN), 0.15f);
    }
    settle();
    CHECK(cord_fires == 1, "jiggling at the bottom doesn't fire again (%d)", cord_fires);
    let_go();
    sfxt_wait(0.6f);
    CHECK(cord < 0.05f, "springs back up (%.2f)", cord);
    grab_at(cord_handle(0));
    l.from = cord_handle(0);
    sfxt_hand_path(R, line_path, &l, 0.5f);
    settle();
    CHECK(cord_fires == 2, "the second pull fires again (%d)", cord_fires);
    let_go();
}
