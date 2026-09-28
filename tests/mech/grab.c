// grab.c - grab styles.

#include "scene.h"

void grab_by_closing_hand(void)
{
    // With VRUI_GRAB_CLOSE, closing the hand around a control grabs it --
    // even for a player whose buttons need a FULL pull, a relaxed close does.
    vrui_style()->pull = SFXR_PULL_FULL;
    vrui_style()->grab = VRUI_GRAB_CLOSE;
    Vector3 c = knob_center(KNOB_AT);
    sfxt_hand_to(R, pose(add(around(c, 0.03f, 0), v3(0, 0.08f, 0)), PALM_DOWN), 0.25f);
    sfxt_hand_to(R, pose(around(c, 0.03f, 0), PALM_DOWN), 0.15f);   // arrives open
    sfxt_grip(R, 0.65f);                                              // closes, not all the way
    sfxt_frames(3);
    CHECK(knob_m.held, "closing the hand grabbed it");
    sfxt_grip(R, 0.0f);
    sfxt_frames(3);
    CHECK(!knob_m.held, "opening it lets go");
}
