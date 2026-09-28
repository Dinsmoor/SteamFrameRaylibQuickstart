// scene.c - one of each mechanism on a virtual table, default specs.

#include "scene.h"

VruiMechSpec knob_s, selector_s, crank_s, lever_s, slider_s, plunger_s, stick_s, sprung_s;
float sprung = 0.5f, set_point = 0.5f;   // the sprung lever returns to set_point
VruiMech sprung_m;
VruiPressSpec button_s, pointbtn_s;
int point_presses;
VruiRockerSpec switch_s;
float knob = 0.5f, selector = 2.0f, crank = 0.0f, lever = 0.5f, slider = 0.5f, plunger = 0.0f;
Vector2 stick;
bool sw;
VruiMech knob_m, selector_m, crank_m, lever_m, slider_m, plunger_m, stick_m;
VruiPress button_p, switch_p;
int button_presses, button_releases, switch_flips, selector_changes;
bool specs_ready;

void scene(void)
{
    if (!specs_ready) {
        knob_s = vrui_knob_spec();
        selector_s = vrui_selector_spec(5);
        crank_s = vrui_crank_spec();
        lever_s = vrui_lever_spec();
        slider_s = vrui_slider_spec();
        plunger_s = vrui_plunger_spec();
        stick_s = vrui_joystick_spec();
        button_s = vrui_press_spec();
        switch_s = vrui_rocker_spec();
        pointbtn_s = vrui_press_spec();
        pointbtn_s.require_point = true;
        sprung_s = vrui_lever_spec();
        sprung_s.spring = true;
        specs_ready = true;
    }
    knob_m = vrui_rotary(ID_KNOB, KNOB_AT, &knob_s, &knob);
    float sel_before = selector;
    selector_m = vrui_rotary(ID_SELECTOR, SELECTOR_AT, &selector_s, &selector);
    if (selector != sel_before) selector_changes++;
    crank_m = vrui_rotary(ID_CRANK, CRANK_AT, &crank_s, &crank);
    lever_m = vrui_pivot(ID_LEVER, LEVER_AT, &lever_s, &lever);
    slider_m = vrui_linear(ID_SLIDER, SLIDER_AT, &slider_s, &slider);
    plunger_m = vrui_linear(ID_PLUNGER, PLUNGER_AT, &plunger_s, &plunger);
    stick_m = vrui_tilt(ID_STICK, STICK_AT, &stick_s, &stick);
    button_p = vrui_press(ID_BUTTON, BUTTON_AT, &button_s, NULL);
    if (button_p.pressed) button_presses++;
    if (button_p.released) button_releases++;
    switch_p = vrui_rocker(ID_SWITCH, SWITCH_AT, &switch_s, &sw);
    if (switch_p.changed) switch_flips++;
    if (vrui_press(ID_POINTBTN, POINTBTN_AT, &pointbtn_s, NULL).pressed) point_presses++;
    sprung_s.rest = set_point;   // read one value, feed it into another control's spec
    sprung_m = vrui_pivot(ID_SPRUNG, SPRUNG_AT, &sprung_s, &sprung);
}
