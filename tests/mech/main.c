// tests/mech - the reference mechanisms keep their promises (docs/MECHANISMS.md).
//
// Every mechanism sits on a virtual table at a fixed spot (scene.c); scripted
// hands grab and move them the way real hands do: never along a perfect axis,
// with a little tremor (the harness adds 1 mm / 0.2 deg / 0.01 noise by
// default). The cases live in one file per mechanism family. Each names the
// break switch that must make it fail (sfxr_break.h), so the runner can prove
// the case detects what it claims.
//
//   make test T=mech                       all of these
//   make test T=mech/knob-orbit-quarter-turn

#include "scene.h"

static const SfxtCase CASES[] = {
    { "mech/knob-orbit-quarter-turn",        knob_orbit_quarter_turn,        "vrui_rotary_twist_only" },
    { "mech/knob-twist-quarter-turn",        knob_twist_quarter_turn,        "vrui_rotary_no_twist" },
    { "mech/knob-press-down-while-turning",  knob_press_down_while_turning,  "vrui_mech_lateral_leak" },
    { "mech/knob-side-push-while-twisting",  knob_side_push_while_twisting,  "vrui_rotary_orbit_everywhere" },
    { "mech/knob-grab-does-not-nudge",       knob_grab_does_not_nudge,       "vrui_mech_no_slop" },
    { "mech/knob-grab-off-axis-no-jump",     knob_grab_off_axis_no_jump,     "vrui_mech_absolute" },
    { "mech/knob-hold-survives-drift",       knob_hold_survives_drift,       "vrui_hold_breaks_on_drift" },
    { "mech/knob-end-stop-holds",            knob_end_stop_holds,            "vrui_mech_stop_unwind" },
    { "mech/knob-laser-orbit",               knob_laser_orbit,               "vrui_rotary_twist_only" },
    { "mech/knob-laser-fast-spin-is-limited", knob_laser_fast_spin_is_limited, "vrui_mech_no_resistance" },
    { "mech/plunger-tension-gentle-near-rest", plunger_tension_gentle_near_rest, "vrui_mech_tension_linear" },
    { "mech/plunger-tension-hum",            plunger_tension_hum,            "vrui_mech_no_tension" },
    { "mech/sprung-lever-returns-to-set-point", sprung_lever_returns_to_set_point, "vrui_mech_no_spring" },
    { "mech/selector-turn-snaps",            selector_turn_snaps,            "vrui_mech_no_snap" },
    { "mech/selector-no-chatter-at-boundary", selector_no_chatter_at_boundary, "vrui_selector_chatter" },
    { "mech/crank-two-turns",                crank_two_turns,                "vrui_rotary_twist_only" },
    { "mech/lever-push-forward",             lever_push_forward,             "vrui_rotary_twist_only" },
    { "mech/lever-sideways-push-ignored",    lever_sideways_push_ignored,    "vrui_mech_lateral_leak" },
    { "mech/lever-grab-no-jump",             lever_grab_no_jump,             "vrui_mech_absolute" },
    { "mech/slider-off-center-grab",    slider_off_center_grab,    "vrui_mech_absolute" },
    { "mech/slider-lift-and-lean-ignored",    slider_lift_and_lean_ignored,    "vrui_mech_lateral_leak" },
    { "mech/slider-laser",              slider_laser,              "vrui_mech_absolute" },
    { "mech/plunger-pull-and-return",        plunger_pull_and_return,        "vrui_mech_no_spring" },
    { "mech/joystick-tilt-and-return",       joystick_tilt_and_return,       "vrui_mech_no_spring" },
    { "mech/joystick-push-down-ignored",     joystick_push_down_ignored,     "vrui_mech_lateral_leak" },
    { "mech/button-press-from-above",        button_press_from_above,        NULL },
    { "mech/button-side-brush-does-nothing", button_side_brush_does_nothing, "vrui_button_side_entry" },
    { "mech/button-press-then-slide",        button_press_then_slide,        "vrui_button_no_slide" },
    { "mech/button-point-to-press",         button_point_to_press,          "vrui_button_ignores_shape" },
    { "mech/grab-by-closing-hand",           grab_by_closing_hand,           "vrui_grab_style_ignored" },
    { "mech/switch-poke-flips-once",         switch_poke_flips_once,         "vrui_switch_no_exit_margin" },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
