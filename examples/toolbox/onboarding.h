// onboarding.h - "how do you like to use your hands?" A short, hands-on
// setup that teaches the controls AND quietly learns the player's habits, so
// the app fits them without a settings menu (docs/ONBOARDING.md). In the
// toolbox it's a station you start from its panel; in your app, run it
// whenever your flow wants (first launch, a settings screen, never).
//
// A template to copy, not a library: the tasks, what they observe and how
// that maps to settings are all plain code in onboarding.c.

#ifndef ONBOARDING_H
#define ONBOARDING_H

#include "sfxr.h"
#include "vrui.h"

// What the setup learned (and the player confirmed). Saved as key=value text.
typedef struct {
    SfxrHandId    dominant;        // the hand that did most of the tasks
    VruiGrabStyle grab;            // grip, or grip-or-trigger if they reached with the trigger
    SfxrPull      pull;            // SOFT if they never squeezed past ~half, else FIRM
    bool          point_to_press;  // they poke buttons with a pointing finger
    bool          knob_orbiter;    // they turn knobs by dragging around (vs twisting)
    bool          laser_first;     // they tend to point from afar rather than reach
    bool          valid;           // loaded or completed
} Prefs;

// Load saved preferences and apply them (nothing starts by itself: running
// the setup, and when, is the app's choice).
void onboarding_init(const char *prefs_path);
void onboarding_start(void);             // run the setup (tasks appear in front of the player)
// A station panel for it: explains it, shows the current preferences, Start/Stop.
void onboarding_panel(SfxrPose *pose);
bool onboarding_active(void);
// Every frame between vrui_begin() and vrui_end().
void onboarding_update(void);
const Prefs *onboarding_prefs(void);
// Push the preferences into vrui (and whatever else the app maps them to).
void onboarding_apply(const Prefs *p);

#endif
