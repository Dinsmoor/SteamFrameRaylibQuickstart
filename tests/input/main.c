// tests/input - raw trigger readings become the right pull-level buttons at the
// right frame (sfxr's SfxrPull, docs/MECHANISMS.md "Pulling the trigger").
//
// The curves are shaped like real Steam Frame pulls, measured from a recorded
// session: the trigger rests at exactly 0, passes half travel ~15 ms into a
// normal pull, reaches 1.0 ~30 ms in, and the hardware click only arrives at
// the very bottom.

#include "sfxt.h"

#include <math.h>
#include <stdio.h>

#define R SFXR_RIGHT

static int presses[SFXR_PULL_COUNT], releases[SFXR_PULL_COUNT];
static int first_press_frame[SFXR_PULL_COUNT];

static void scene(void)
{
    const SfxrHand *h = sfxr_hand(R);
    for (int l = 0; l < SFXR_PULL_COUNT; l++) {
        if (h->trigger_at[l].pressed) {
            if (!presses[l]) first_press_frame[l] = (int)sfxr_frame_index();
            presses[l]++;
        }
        if (h->trigger_at[l].released) releases[l]++;
    }
}

static void reset_counts(void)
{
    for (int l = 0; l < SFXR_PULL_COUNT; l++) presses[l] = releases[l] = first_press_frame[l] = 0;
}

static void firm_press_once_on_ramps(void)
{
    // Slow, normal and fast pulls all the way in and back out: exactly one
    // press and one release at every level.
    const float ms[] = { 300, 100, 50 };
    for (int i = 0; i < 3; i++) {
        reset_counts();
        sfxt_trigger_ramp(R, 1.0f, ms[i] / 1000.0f);
        sfxt_frames(10);
        sfxt_trigger_ramp(R, 0.0f, ms[i] / 1000.0f);
        sfxt_frames(10);
        for (int l = 0; l < SFXR_PULL_COUNT; l++)
            CHECK(presses[l] == 1 && releases[l] == 1, "%.0f ms ramp, level %d: %d presses, %d releases",
                  ms[i], l, presses[l], releases[l]);
    }
}

static void no_chatter_near_threshold(void)
{
    // A finger resting right at the FIRM threshold, trembling +-0.03, for two
    // seconds: it may press once, but never flickers.
    sfxt_noise(0.001f, 0.2f, 0.03f);
    sfxt_trigger(R, 0.55f);
    sfxt_wait(2.0f);
    CHECK(presses[SFXR_PULL_FIRM] <= 1, "at most one FIRM press (%d)", presses[SFXR_PULL_FIRM]);
    CHECK(releases[SFXR_PULL_FIRM] == 0, "no FIRM release while resting (%d)", releases[SFXR_PULL_FIRM]);
}

static void levels_in_order(void)
{
    // A light touch is SOFT only; half way adds FIRM; only the bottom is FULL.
    sfxt_noise(0.001f, 0.2f, 0.0f);
    const SfxrHand *h = sfxr_hand(R);
    sfxt_trigger(R, 0.3f); sfxt_frames(2);
    CHECK(h->trigger_at[SFXR_PULL_SOFT].down && !h->trigger_at[SFXR_PULL_FIRM].down, "0.3: soft only");
    sfxt_trigger(R, 0.6f); sfxt_frames(2);
    CHECK(h->trigger_at[SFXR_PULL_FIRM].down && !h->trigger_at[SFXR_PULL_FULL].down, "0.6: firm, not full");
    CHECK(h->trigger_btn.down, "trigger_btn is the FIRM level");
    sfxt_trigger(R, 0.9f); sfxt_frames(2);
    CHECK(!h->trigger_at[SFXR_PULL_FULL].down, "0.9: still not full");
    sfxt_trigger(R, 1.0f); sfxt_frames(2);
    CHECK(h->trigger_at[SFXR_PULL_FULL].down, "1.0: full");
}

static void real_pull_registers_fast(void)
{
    // A normal pull as measured on the Frame: 0 -> 0.55 in ~14 ms, 1.0 at
    // ~28 ms. FIRM must register within 2 frames of the pull starting --
    // not 40-100 ms later, when the hardware click finally arrives.
    sfxt_noise(0.001f, 0.2f, 0.0f);
    int start = (int)sfxr_frame_index();
    const float curve[] = { 0.3f, 0.62f, 1.0f, 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 6; i++) { sfxt_trigger(R, curve[i]); sfxt_frames(1); }
    CHECK(presses[SFXR_PULL_FIRM] == 1, "FIRM pressed (%d)", presses[SFXR_PULL_FIRM]);
    CHECK(first_press_frame[SFXR_PULL_FIRM] - start <= 2, "within 2 frames (took %d)",
          first_press_frame[SFXR_PULL_FIRM] - start);
}

static void shapes_from_touch_sensors(void)
{
    // The Frame's touch sensors say where each finger is. Grip held, index
    // resting on the trigger, thumb on the stick: a fist. Lift the index off
    // the trigger: pointing. Lift the thumb instead: thumbs up. Let go of
    // everything: an open hand. (Shapes switch after 3 steady frames.)
    const SfxrHand *h = sfxr_hand(R);
    sfxt_noise(0.001f, 0.2f, 0.0f);
    sfxt_grip(R, 0.8f);
    sfxt_touch(R, SFXR_CTL_TRIGGER, true);
    sfxt_touch(R, SFXR_CTL_STICK, true);
    sfxt_frames(5);
    CHECK(h->shape == SFXR_SHAPE_FIST, "fist (got %s)", sfxr_hand_shape_name(h->shape));
    sfxt_touch(R, SFXR_CTL_TRIGGER, false);
    sfxt_frames(5);
    CHECK(h->shape == SFXR_SHAPE_POINT, "point (got %s)", sfxr_hand_shape_name(h->shape));
    sfxt_touch(R, SFXR_CTL_TRIGGER, true);
    sfxt_touch(R, SFXR_CTL_STICK, false);
    sfxt_frames(5);
    CHECK(h->shape == SFXR_SHAPE_THUMBS_UP, "thumbs up (got %s)", sfxr_hand_shape_name(h->shape));
    sfxt_grip(R, 0.0f);
    sfxt_touch(R, SFXR_CTL_TRIGGER, false);
    sfxt_frames(5);
    CHECK(h->shape == SFXR_SHAPE_OPEN, "open (got %s)", sfxr_hand_shape_name(h->shape));
}

// The same, as SteamVR reports Frame controllers: with a skeleton whose
// index finger never straightens. The touch sensors still decide: lift the
// index off the trigger and it's a point.
static void frame_point_from_touch(void)
{
    const SfxrHand *h = sfxr_hand(R);
    sfxt_hand_kind(R, SFXT_FRAME_SKELETON);
    sfxt_noise(0.001f, 0.2f, 0.0f);
    sfxt_grip(R, 0.8f);
    sfxt_touch(R, SFXR_CTL_TRIGGER, false);
    sfxt_touch(R, SFXR_CTL_STICK, true);
    sfxt_frames(5);
    CHECK(sfxr_hand_joints(R)->valid, "SteamVR's skeleton is there");
    CHECK(h->shape == SFXR_SHAPE_POINT, "index off the trigger, grip held: point (got %s)", sfxr_hand_shape_name(h->shape));
}

// Holding a Frame controller, the poke point is the controller's tip (where
// the laser starts), not SteamVR's poke pose 12.5 cm under the grip.
static void frame_poke_at_the_tip(void)
{
    sfxt_hand_kind(R, SFXT_FRAME_SKELETON);
    sfxt_noise(0, 0, 0);
    sfxt_frames(3);
    const SfxrHand *h = sfxr_hand(R);
    float d = Vector3Distance(h->poke.position, h->aim.position);
    CHECK(d < 0.02f, "poke at the tip: %.1f cm from it", d * 100.0f);
}

static const SfxtCase CASES[] = {
    { "input/frame-point-from-touch",      frame_point_from_touch,     "sfxr_shapes_from_controller_skeleton" },
    { "input/frame-poke-at-the-tip",       frame_poke_at_the_tip,      "sfxr_poke_from_runtime" },
    { "input/shapes-from-touch-sensors",  shapes_from_touch_sensors,  "sfxr_shapes_from_values_only" },
    { "input/firm-press-once-on-ramps",   firm_press_once_on_ramps,   NULL },
    { "input/no-chatter-near-threshold",  no_chatter_near_threshold,  "sfxr_no_hysteresis" },
    { "input/levels-in-order",            levels_in_order,            NULL },
    { "input/real-pull-registers-fast",   real_pull_registers_fast,   NULL },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
