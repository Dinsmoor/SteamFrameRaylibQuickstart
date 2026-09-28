// press.c - push buttons and rocker switches.

#include "scene.h"

void button_press_from_above(void)
{
    // Poke straight down 1 cm into the cap and back out: one press.
    Vector3 top = cap_top();
    sfxt_hand_to(R, tip_at(add(top, v3(0, 0.03f, 0))), 0.3f);
    sfxt_hand_to(R, tip_at(add(top, v3(0, -0.01f, 0))), 0.2f);
    CHECK(button_p.down, "down while pressed in");
    sfxt_hand_to(R, tip_at(add(top, v3(0, 0.03f, 0))), 0.2f);
    CHECK(button_presses == 1 && button_releases == 1, "one press, one release (%d, %d)", button_presses, button_releases);
}

void button_side_brush_does_nothing(void)
{
    // A tip sweeping in from the side at cap height (reaching past it for
    // something else) must not press it.
    Vector3 top = cap_top();
    sfxt_hand_to(R, tip_at(add(top, v3(-0.08f, -0.008f, 0))), 0.3f);
    sfxt_hand_to(R, tip_at(add(top, v3(0.08f, -0.008f, 0))), 0.5f);
    sfxt_hand_to(R, tip_at(add(top, v3(0.08f, 0.05f, 0))), 0.2f);
    CHECK(button_presses == 0, "no press from a side brush (%d)", button_presses);
}

void button_press_then_slide(void)
{
    // Press, then let the finger slide 5 cm sideways while still pushing (it
    // happens): the press holds until the finger lifts.
    Vector3 top = cap_top();
    sfxt_hand_to(R, tip_at(add(top, v3(0, 0.03f, 0))), 0.3f);
    sfxt_hand_to(R, tip_at(add(top, v3(0, -0.01f, 0))), 0.2f);
    sfxt_hand_to(R, tip_at(add(top, v3(0.05f, -0.01f, 0))), 0.4f);
    CHECK(button_p.down, "still down after sliding 5 cm");
    CHECK(button_releases == 0, "not released during the slide");
    sfxt_hand_to(R, tip_at(add(top, v3(0.05f, 0.05f, 0))), 0.2f);
    CHECK(button_presses == 1 && button_releases == 1, "exactly one press (%d, %d)", button_presses, button_releases);
}

void button_point_to_press(void)
{
    // A point-to-press button ignores a fist bumping into it, and presses
    // for a pointing finger (read from the touch sensors).
    sfxt_grip(R, 0.8f);                           // fist: grip, index on trigger, thumb on stick
    sfxt_touch(R, SFXR_CTL_TRIGGER, true);
    sfxt_touch(R, SFXR_CTL_STICK, true);
    poke_point_button();
    CHECK(point_presses == 0, "a fist doesn't press it (%d)", point_presses);
    sfxt_touch(R, SFXR_CTL_TRIGGER, false);        // lift the index: pointing
    sfxt_frames(5);
    poke_point_button();
    CHECK(point_presses == 1, "a pointing finger does (%d)", point_presses);
    sfxt_grip(R, 0.0f);
    sfxt_touch(R, SFXR_CTL_STICK, false);
}

void switch_poke_flips_once(void)
{
    // Poke the switch and let the fingertip tremble on its edge for a second:
    // one poke, one flip.
    Vector3 top = add(SWITCH_AT.position, v3(0, 0.024f + 0.01f, 0));   // poke volume top
    sfxt_hand_to(R, tip_at(add(top, v3(0, 0.05f, 0))), 0.3f);
    sfxt_hand_to(R, tip_at(add(top, v3(0, -0.003f, 0))), 0.2f);
    Jitter j = { top };
    sfxt_hand_path(R, switch_jitter_path, &j, 1.0f);
    sfxt_hand_to(R, tip_at(add(top, v3(0, 0.06f, 0))), 0.2f);
    CHECK(switch_flips == 1, "flipped exactly once (%d)", switch_flips);
    CHECK(sw, "and it's on");
}
