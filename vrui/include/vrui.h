// vrui.h - immediate-mode VR interaction toolkit on top of sfxr + raylib.
//
// Same spirit as a desktop immediate-mode UI: each call both HANDLES INPUT and
// DRAWS, and returns what happened. Adapted to VR:
//   * Reaching things: the LASER (point + trigger), the HAND (grab with the
//     grip, poke with the controller tip or a fingertip), and HAND SHAPES
//     (an open palm, a pointing finger...). See docs/INPUT.md.
//   * 2D PANELS (buttons, sliders, lists on a floating panel) and 3D
//     MECHANISMS (knobs, levers, sliders, joysticks, buttons, switches) with
//     tested, documented behavior. See docs/MECHANISMS.md.
//
// Contents
//   1. Frame, style, drawing options
//   2. Input ownership, pull levels, haptics
//   3. Panels: 2D UI in 3D
//   4. Grabbables
//   5. Mechanisms: rotary, pivot, linear, tilt   (short forms: knob, lever, slider3d, joystick)
//   6. Press and rocker                          (short forms: push_button, switch)
//   7. Displays: gauge, odometer, lamp
//   8. Locomotion
//   9. Text and labels in the world
//  10. Things that go with the player: body, HUDs, hand menus
//  11. Smoothing and interpolation: damping, springs, easing, pose smoothers
//  12. The widget registry: widgets by name, for tests and tools
//  13. Queued 3D drawing helpers
//
// Naming: short forms are named after the THING and use default behavior
// (vrui_knob, vrui_lever...). Full forms are named after the MOTION and take
// a spec (vrui_rotary with a knob, selector, crank or spinner spec...).
//
// Frame structure (logic OUTSIDE the draw block, drawing INSIDE it):
//
//   while (sfxr_frame_begin()) {
//       vrui_begin();
//         vrui_locomotion(&loco);
//         if (vrui_panel_begin(ID_PANEL, &panel_pose, 0.5f, 0.4f, "Settings")) {
//             vrui_layout_begin(vrui_panel_content(), 8);
//             if (vrui_button(1, vrui_row(40), "Reset")) reset();
//             vrui_slider(2, vrui_row(40), "Speed", &speed, 0, 10);
//             vrui_panel_end();
//         }
//         vrui_grabbable(ID_CUBE, &cube_pose, (Vector3){ .05f, .05f, .05f }, RED);
//         if (vrui_push_button(ID_BTN, button_pose, 0.04f, RED, "GO")) go();
//         vrui_knob(ID_VOL, knob_pose, 0.035f, &volume, 0, 1, 0.75f, "VOLUME");
//       vrui_end();
//       if (sfxr_draw_begin(SKYBLUE)) { draw_world(); vrui_draw(); sfxr_draw_end(); }
//       sfxr_frame_end();
//   }
//
// IDs: every interactive widget needs a stable, unique, nonzero VruiId.
// Convention: VRUI_ID2(group, index) -- a group per screen or area, an index
// per item. Panel widgets' ids only need to be unique within their panel.
//
// Poses: a 3D control's `base` is where it's mounted; its local +Y sticks OUT
// of the surface (up for a tabletop, toward you for a wall). Panels and
// displays face their local +Z (vrui_facing() aims one at the player).

#ifndef VRUI_H
#define VRUI_H

#include <stdbool.h>
#include <stdint.h>

#include "sfxr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t VruiId;
#define VRUI_ID_NONE  ((VruiId)0)
#define VRUI_ID2(a, b) ((VruiId)(((uint32_t)(a) << 16) | ((uint32_t)(b) & 0xFFFFu)))

// ===========================================================================
// 1. Frame, style, drawing options
// ===========================================================================

void vrui_init(void);                 // after sfxr_init
void vrui_shutdown(void);
void vrui_begin(void);                // after sfxr_frame_begin, before widgets
void vrui_end(void);                  // after all widgets (resolves hover for next frame)
void vrui_draw(void);                 // inside sfxr_draw_begin/end, after your world

// How a hand takes hold of things within reach (vrui_style()->grab).
typedef enum {
    VRUI_GRAB_GRIP = 0,        // pull the grip (at the pull level in effect) -- the default
    VRUI_GRAB_CLOSE,           // close your hand around it: middle, ring and little fingers
                               //   curling in (grip sensor or hand joints). Only grabs if the
                               //   hand was open first, so arriving with a closed hand doesn't
                               //   grab whatever you touch.
    VRUI_GRAB_GRIP_OR_TRIGGER, // either button (players who reach with the trigger)
} VruiGrabStyle;

// Sounds vrui asks for (vrui_style()->sound): the app plays them, so vrui
// stays free of any audio library. docs/AUDIO.md.
typedef enum {
    VRUI_SOUND_CLICK,   // grabbed, pressed, clicked (with the click haptic)
    VRUI_SOUND_TICK,    // a detent passed (a knob's notch, a valve's quarter turn)
    VRUI_SOUND_STOP,    // an end stop hit
} VruiSound;

typedef struct {
    // panels
    float px_per_m;         // panel texel density (1000 = 1 px per mm)
    int   font_size;        // panel text size in px (multiples of 10 stay crisp with the default font)
    int   title_height;     // panel title bar height in px
    Color panel_bg, panel_border, title_bg, title_text;
    Color text, text_dim;
    Color widget, widget_hot, widget_active, accent;
    // lasers and hover
    Color laser;            // pointing at nothing
    Color laser_hit;        // on a 2D panel
    Color laser_far;        // on a physical (3D) thing
    Color cursor;
    Color prop_hover;       // tint on hovered 3D things
    // feel
    float haptic_hover;     // amplitude of the hover tick (0 disables)
    float haptic_click;     // amplitude of the grab / press click
    float haptic_scale;     // scales every vrui haptic (1 default, 0 off): comfort / accessibility
    SfxrPull pull;          // how hard trigger/grip must be pulled (default FIRM; see vrui_push_pull)
    VruiGrabStyle grab;     // how a hand takes hold of things within reach (default GRIP)
    bool  show_hints;       // say how to use whatever a hand is on ("poke it | or laser + trigger"; default on)
    // sound: called with what happened and where (world); strength 0..1.
    // NULL (the default): silent. The toolbox plays sounds made in code.
    void (*sound)(VruiSound kind, Vector3 at, float strength);
} VruiStyle;

VruiStyle *vrui_style(void);
void vrui_set_font(Font font);        // default: raylib's built-in font
void vrui_show_controllers(bool on);  // draw the controllers / hands (default on)
void vrui_controller_models(bool on); // the headset's own controller models when available (default on)
void vrui_hand_joints_always(bool on);// joints while holding controllers too (bare hands: always)

// Pose helpers
SfxrPose vrui_facing(Vector3 position, Vector3 viewer);   // +Z faces viewer, yaw only
SfxrPose vrui_in_front_of_head(float distance, float drop); // `distance` ahead, eye height - `drop`, facing you

// ===========================================================================
// 2. Input ownership, pull levels, haptics
// ===========================================================================

// Is this hand holding / dragging something, or hovering something, in vrui?
bool vrui_hand_busy(SfxrHandId hand);
bool vrui_hand_hovering(SfxrHandId hand);

// Input ownership ("who gets this controller right now?", docs/INPUT.md).
// Whatever the player is using claims that hand's controls for the frame, so
// nothing else reacts to them: pointing at a panel and pushing the stick
// scrolls the panel instead of teleporting you. Locomotion checks this;
// check it in your own code before acting on a raw button or the stick:
//
//   if (!vrui_input_claimed(SFXR_RIGHT) && sfxr_hand(SFXR_RIGHT)->a.pressed) jump();
//
// Claims made this frame or last frame count (so call order doesn't matter).
// Claim for your own contexts too (a held tool that uses the stick, a menu).
void vrui_claim_input(SfxrHandId hand);
bool vrui_input_claimed(SfxrHandId hand);

// Pull level for the widgets that follow, until the matching pop (the style's
// `pull` applies outside any push). SOFT suits delicate things, FULL
// deliberate or destructive actions. A panel uses the level at its begin.
//   vrui_push_pull(SFXR_PULL_SOFT);
//   vrui_grabbable(ID_MARBLE, &marble, half, RED);
//   vrui_pop_pull();
void vrui_push_pull(SfxrPull level);
void vrui_pop_pull(void);
SfxrPull vrui_current_pull(void);

// Haptics: one mixed channel per hand (vrui_haptics.c). Use these rather than
// sfxr_haptic(), so ticks and continuous hums don't cut each other off.
void vrui_haptic_pulse(SfxrHandId hand, float amplitude, float seconds, float frequency_hz);
void vrui_haptic_hum(SfxrHandId hand, float amplitude, float frequency_hz);   // call every frame it should last

// ===========================================================================
// 3. Panels: 2D UI in 3D
// ===========================================================================

// How a panel claims input; set before vrui_panel_begin (applies to that panel):
typedef enum {
    VRUI_CAPTURE_POINT = 0,  // default: a hand whose laser is on the panel
    VRUI_CAPTURE_LOOK,       // both hands, while you look at the panel's front (within 30 deg,
                             //   3 m; never from behind): a controller test panel, look at it
                             //   and use everything
    VRUI_CAPTURE_MODAL,      // both hands, every frame the panel is shown (dialogs)
} VruiCapture;
void vrui_panel_capture(VruiCapture mode);

// Begins a panel. `pose` is updated when the player drags it by the title bar
// (keep it in a persistent variable). title == NULL: no title bar, not
// movable. Returns true when the panel is shown -- pair with vrui_panel_end().
// A display, not a control (set before vrui_panel_begin): no laser, no
// input claims. HUDs use this, so a readout floating in front of you never
// catches the laser meant for what's behind it.
void vrui_panel_passive(void);
bool vrui_panel_begin(VruiId id, SfxrPose *pose, float width_m, float height_m, const char *title);
void vrui_panel_end(void);
Rectangle vrui_panel_content(void);   // content area in panel pixels (below the title bar)
bool vrui_panel_hovered(void);        // a laser is on the current panel
bool vrui_panel_capturing(void);      // the current panel holds both hands right now

// Simple vertical layout inside a rectangle.
void      vrui_layout_begin(Rectangle area, float spacing);
Rectangle vrui_row(float height);                             // next full-width row
void      vrui_row_cols(float height, int n, Rectangle *out); // next row split into n columns
void      vrui_space(float height);

// Widgets (rects in panel pixels; ids local to the panel). Raw raylib 2D
// drawing (DrawRectangle, DrawTextEx...) also works between begin and end.
void vrui_label(Rectangle r, const char *text);
void vrui_label_center(Rectangle r, const char *text, Color color);
bool vrui_button(VruiId id, Rectangle r, const char *text);                    // true on click
bool vrui_toggle(VruiId id, Rectangle r, const char *text, bool *value);       // true when changed
bool vrui_slider(VruiId id, Rectangle r, const char *label, float *value, float min, float max);
bool vrui_segmented(VruiId id, Rectangle r, const char *const *items, int count, int *selected);
void vrui_progress(Rectangle r, float t, Color color);
// Scrollable list: drag it (or its scrollbar) with the trigger held, or push
// the stick while pointing at it. A tap selects. true when the selection changed.
bool vrui_list(VruiId id, Rectangle r, const char *const *items, int count, int *selected, float *scroll);

// ===========================================================================
// 4. Grabbables
// ===========================================================================

typedef struct {
    bool       hovered;          // a hand could grab it now
    bool       grabbed;          // grabbed this frame
    bool       held;             // held (pose is being driven by the hand)
    bool       released;         // let go this frame
    SfxrHandId hand;             // valid while held / on grab / release
    Vector3    release_velocity; // m/s, world (throw it!)
    Vector3    release_angular_velocity;
} VruiGrab;

// A box you can pick up (within reach + grab, or laser + grip from afar).
// vrui_grabbable draws a colored box; vrui_grab_region lets you draw your own.
VruiGrab vrui_grabbable(VruiId id, SfxrPose *pose, Vector3 half_extents, Color color);
VruiGrab vrui_grab_region(VruiId id, SfxrPose *pose, Vector3 half_extents);

// ===========================================================================
// 5. Mechanisms: reference physical controls (docs/MECHANISMS.md)
// ===========================================================================
//
// Each mechanism is three separable things:
//   BEHAVIOR  how it moves and feels: a spec with defaults tuned for real
//             hands (vrui_knob_spec() etc.), covered by tests/mech.
//   COLLIDER  what hands and lasers can take hold of: the spec's `size` (and
//             `reach` for near grabs). Match it to your model.
//   LOOK      vrui draws a plain default unless spec.draw = false; the
//             result's `part` is the world pose of the moving part, so you can
//             draw your own model there and keep the behavior.
//
// Rules every mechanism follows, because hands never move along a perfect axis:
//   * Only motion along the mechanism's own freedom counts. Pressing down on a
//     knob, pushing a lever sideways or lifting a slider is ignored -- and is
//     never a reason to let go.
//   * Motion is relative to where you took hold: grabbing never makes it jump.
//   * A small break-in (spec.slop) at grab time, so taking hold doesn't nudge it.
//   * Holding ends only when you let go, never because the hand drifted.
//   * Near (hand) and far (laser) both work.
//   * Resistance is a speed limit and a weight; haptics carry the feel
//     (ticks, stops, strain, spring tension).
//
// Directions: rotary values increase CLOCKWISE looking down the axis (a volume
// knob seen from above); a lever's value increases as its handle is pushed
// toward the base's -Z (away from a player facing -Z); linear controls
// increase toward the base's +X; a joystick's x is +X, y is -Z.

typedef struct {
    // --- value model
    float min, max;
    int   detents;       // 0 = smooth; N >= 2: N stops from min to max (endless: N per `travel`)
    bool  snap;          // with detents: only ever rests on a stop (selector). Otherwise it
                         // moves smoothly, ticks as it passes stops, and settles on release.
    bool  spring;        // on release, glide back to `rest` (dead-man lever, joystick, plunger)
    float rest;          // may change every frame: a set point another control drives
    bool  endless;       // rotary: no end stops; the value keeps counting (crank, spinner)
    float travel;        // physical motion spanning min..max: radians (rotary, pivot) or meters (linear)
    // --- collider
    float size;          // knob/crank radius, lever length, slider length, joystick height (m)
    float reach;         // how close the hand must be for a near grab (m)
    // --- feel. The controllers can't push back: resistance is a speed limit
    // and a weight, and everything physical is told through haptics.
    float slop;          // break-in before motion counts (radians or meters)
    bool  twist;         // rotary: wrist twist drives it too (knobs yes; cranks and levers no)
    float max_speed;     // fastest it will move by hand (rad/s or m/s; 0 = no limit)
    float far_max_speed; // ... and by laser, where a small wrist flick is a big sweep
    float weight;        // inertia: seconds it takes to catch up with the hand (0 = instant)
    float slip;          // how far (rad or m) it may lag before the grip "slips" (the rest is dropped)
    float coast;         // endless rotary: after release it spins on, slowing by this friction
                         //   (1/s; 0 = stops dead). A wheel of fortune.
    float haptic_tick;   // detent tick
    float haptic_stop;   // end-stop bump
    float haptic_strain; // rough hum while it lags the hand (you're pushing too fast)
    float haptic_tension;// springs: hum while displaced, stronger and faster further from rest
    // --- hands (vrui_valve)
    int   hands;         // hands it takes to move it (1; a stuck valve: 2)
    float one_hand;      // with fewer hands on it, it moves at this fraction (0 = won't budge: it strains)
    // --- look (default drawing; ignored when draw = false)
    bool  draw;
    Color color;
    const char *label;
    const char *value_format; // printf format for label + value, e.g. "%s %.2f" (NULL: label only)
    float display_scale;      // value shown = value * display_scale (100 for percent)
} VruiMechSpec;

typedef struct {
    bool       hovered, grabbed, held, released, changed;
    SfxrHandId hand;       // valid while held / on grab / release
    bool       via_ray;    // held with the laser (far) rather than the hand
    float      value;      // current value (= *value)
    int        detent;     // index of the nearest stop, -1 when smooth
    float      position;   // rotary/pivot: angle of the moving part (rad); linear: offset from center (m)
    Vector2    tilt;       // tilt only (-1..1)
    int        holders;    // hands holding it (2 for a two-handed hold)
    SfxrPose   part;       // world pose of the moving part: draw your own model here
} VruiMech;

// ROTARY: turns about the base's +Y.
VruiMechSpec vrui_knob_spec(void);             // bounded knob, 3/4 turn, smooth, drag around or twist
VruiMechSpec vrui_selector_spec(int positions);// rotary switch: snaps between `positions` stops
VruiMechSpec vrui_crank_spec(void);            // endless handwheel: value = turns, drag around only
VruiMechSpec vrui_spinner_spec(int pegs);      // wheel of fortune: flick it, it coasts past `pegs`
VruiMechSpec vrui_dial_spec(void);             // tuning dial 0..100 over 300 deg, ticks every 5, rests anywhere
VruiMech vrui_rotary(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);
// Knob with default feel; `turns` = rotations spanning min..max; the stick
// fine-adjusts while pointing at it. true when changed.
bool vrui_knob(VruiId id, SfxrPose base, float radius, float *value, float min, float max, float turns, const char *label);

// TWO-HANDED WHEEL: a big handwheel (a stuck valve, a ship's wheel) on the
// base's +Y axis, standing `height` 12 cm off its mount. Grab the rim
// anywhere, with each hand. It turns only while spec.hands (2) hands hold
// it; with one it strains and won't budge (spec.one_hand = 0), which the
// haptics and a hint say. It turns by the AVERAGE swing of the hands round
// the axis, so pushing with one hand and pulling with the other turns it
// like a steering wheel, and one hand slipping can't spin it. Near only:
// a laser can't put two hands on a rim.
VruiMechSpec vrui_valve_spec(void);            // 0..1 over 3 turns, a tick every quarter turn, needs both hands
VruiMech vrui_valve(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);

// KEY SWITCH: a key you carry to its slot, push in and turn, all in one
// hold. `slot` is the keyhole (+Y out of the surface); `*key` is the key's
// pose (origin at the blade's tip, +Y toward its bow): keep it in a variable,
// vrui moves it while it's held or in the slot, and leaves it where you let
// go otherwise. Bring the tip within `capture` of the slot, lined up within
// `align_deg`, and it goes in; then twist your wrist to turn it through
// `positions` stops `step_deg` apart (clockwise); at the first stop, pull
// it straight back to take it out. Near only.
typedef struct {
    int   positions;     // stops (2..): OFF, ON, ...
    float step_deg;      // between stops (default 45)
    bool  spring_last;   // the last stop springs back to the one before (an ignition's START)
    float capture;       // tip this close to the slot to go in (m, default 0.03)
    float align_deg;     // key this straight to the slot's axis (default 30)
    float pull_out;      // pull back this far at the first stop to take it out (m, default 0.04)
    bool  draw;
    Color color;         // the key
    const char *label;
    const char *const *names; // printed round the slot, one per stop (NULL: none)
} VruiKeySpec;

typedef struct {
    bool hovered, grabbed, held, released;
    bool inserted;                 // in the slot now
    bool inserted_now, removed_now;
    bool changed;                  // *position changed
    int  position;                 // the stop it's at (0 when out)
    SfxrHandId hand;
    SfxrPose key;                  // = *key
} VruiKey;

VruiKeySpec vrui_key_spec(int positions);
VruiKey vrui_key_switch(VruiId id, SfxrPose slot, const VruiKeySpec *spec, SfxrPose *key, int *position);

// PIVOT: swings on the base's X axis; handle up along +Y mid-swing.
VruiMechSpec vrui_lever_spec(void);            // throttle lever, 80 deg of swing, 0..1
VruiMech vrui_pivot(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);
bool vrui_lever(VruiId id, SfxrPose base, float length, float *value, const char *label);   // 0 back .. 1 forward

// HINGE (a pivot turned around): a door or lid, taken by its handle. `hinge`
// is on the hinge line at handle height; +Y along the hinge, +X toward the
// handle when closed, +Z out of the front (it opens toward +Z). *open 0..1.
// The result's `part` is the door: on the hinge, +X toward the handle, so
// draw your door there. A lid is a door on its side (hinge +Y horizontal).
VruiMechSpec vrui_hinge_spec(float width);     // weighty, 100 deg, `width` from hinge to handle
VruiMech vrui_hinge(VruiId id, SfxrPose hinge, const VruiMechSpec *spec, float *open);
// A plain door: hinge_bottom is the bottom of the hinge line; handle at 1 m (or mid-height).
bool vrui_door(VruiId id, SfxrPose hinge_bottom, float width, float height, float *open, const char *label);

// LINEAR: slides along the base's X axis, centered on the base.
VruiMechSpec vrui_slider_spec(void);           // slider / fader, 0..1
VruiMechSpec vrui_plunger_spec(void);          // sprung pull handle, 0..1, returns on release
VruiMech vrui_linear(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);
bool vrui_slider3d(VruiId id, SfxrPose base, float length, float *t, const char *label);   // t 0..1
// Pull cord hanging straight down from `anchor` (along its -Y): pull the handle
// 25 cm down. Returns true ONCE per full pull (past 90%); it re-arms when the
// cord comes back up past halfway. *pull 0..1 springs back on release.
VruiMechSpec vrui_pull_cord_spec(void);
bool vrui_pull_cord(VruiId id, SfxrPose anchor, float *pull, const char *label);

// TILT: a stick standing along the base's +Y; *value = tilt (x, y) in -1..1.
VruiMechSpec vrui_joystick_spec(void);         // sprung, 6 cm of hand motion for full tilt
VruiMech vrui_tilt(VruiId id, SfxrPose base, const VruiMechSpec *spec, Vector2 *value);
bool vrui_joystick(VruiId id, SfxrPose base, Vector2 *value, const char *label);

// ===========================================================================
// 6. Press and rocker: things you poke (fingertip / controller tip) or click
//    with the laser. A press counts only when the tip arrives from above --
//    brushing past from the side does nothing -- and once pressing, the tip
//    may slide well past the rim without letting go.
// ===========================================================================

typedef struct {
    bool hovered, down, pressed, released;
    bool changed;        // latching button / rocker: the bool flipped
    float depth;         // 0..1 how far in
    SfxrPose part;       // world pose of the cap / rocker: draw your own model here
} VruiPress;

typedef struct {
    float radius;        // cap radius (collider) (m)
    float travel;        // press depth (m)
    float press_at, release_at; // fractions of travel (hysteresis)
    float slide;         // while pressed, the tip may wander this many radii from center
    bool  latching;      // push-on / push-off (*latched flips on each press)
    bool  require_point; // only a pointing index finger presses it (SFXR_SHAPE_POINT)
    bool  draw;
    Color color;
    const char *label;
} VruiPressSpec;

VruiPressSpec vrui_press_spec(void);
VruiPress vrui_press(VruiId id, SfxrPose base, const VruiPressSpec *spec, bool *latched);   // latched: NULL unless latching
bool vrui_push_button(VruiId id, SfxrPose base, float radius, Color color, const char *label);   // true on press

typedef struct {
    Vector3 half;        // collider half extents (m); the rocker sits on base +Y
    float   exit_margin; // after a poke flips it, the tip must back out this far before it can flip again
    bool    draw;
    const char *label;
} VruiRockerSpec;

VruiRockerSpec vrui_rocker_spec(void);
VruiPress vrui_rocker(VruiId id, SfxrPose base, const VruiRockerSpec *spec, bool *on);
bool vrui_switch(VruiId id, SfxrPose base, bool *on, const char *label);   // true when flipped

// ===========================================================================
// 7. Displays: mechanical indicators (read-only; no laser, no input)
// ===========================================================================

void vrui_gauge(VruiId id, SfxrPose pose, float value, float min, float max, const char *label);  // needle dial, faces +Z
void vrui_odometer(VruiId id, SfxrPose pose, float value, int digits, const char *label);         // rolling drums, faces +Z
void vrui_lamp(SfxrPose pose, bool on, Color color, const char *label);                           // stands on +Y

// ===========================================================================
// 8. Locomotion (teleport + snap turn by default; only unclaimed hands)
// ===========================================================================
// vrui_locomotion is the one place that moves the player (the rig). Besides
// the stick (teleport, turning, walking) it handles, when you use them:
//   * SURFACES: ground_height says how high the ground is anywhere, so the
//     teleport arc lands on platforms, walking in your room steps you up onto
//     low things (step_height) and off edges (you fall, see `fall`).
//   * PADS: fixed teleport destinations that snap you to their center, and
//     optionally turn you to face a set direction.
//   * CLIMBING: while a hand holds a handhold (vrui_handhold), that hand stays
//     put and the WORLD moves instead: pull down to rise, push to move away.
// docs/MOVEMENT.md explains each, with the examples in the toolbox's
// Movement yard (examples/toolbox/yard.c).

// A fixed teleport destination. Landing within `radius` of `center` (and
// within 0.5 m of its height) snaps you to the center.
typedef struct {
    Vector3 center;             // on the surface
    float   radius;             // meters
    bool    face;               // also turn the player to face yaw_deg
    float   yaw_deg;            // 0 = -Z, positive = to the right (clockwise seen from above)
} VruiTeleportPad;

// How the player comes down when the ground drops away (walked off an edge,
// let go of a handhold).
typedef enum {
    VRUI_FALL_BLINK,            // a short fade, and you are on the ground (default: comfortable)
    VRUI_FALL_DROP,             // real falling under gravity (lively, can be sickening)
} VruiFall;

typedef struct {
    bool  teleport;             // stick forward: aim arc, release: jump (default on)
    float teleport_max_dist;    // meters (default 8)
    bool  snap_turn;            // stick left/right: snap turn (default on)
    float snap_angle_deg;       // default 30
    bool  smooth_turn;          // instead of snap
    float smooth_turn_deg_s;    // default 90
    bool  smooth_move;          // left stick moves (head-relative); disables teleport on the left hand
    float move_speed;           // m/s (default 2)
    float fade_seconds;         // blink on teleport/snap (default 0.08; 0 = off)
    float floor_y;              // teleport onto this height (default 0)
    // Optional: reject targets (walls, water...). NULL = anywhere on the floor.
    // Pads are always valid (they are the level designer's decision).
    bool (*valid_target)(Vector3 target, void *user);
    void *user;

    // Optional surfaces: the top of the highest walkable surface at or below
    // `p` (NULL: flat floor at floor_y everywhere). When set, the player steps
    // up onto anything up to step_height and falls off edges.
    float (*ground_height)(Vector3 p, void *user);
    float step_height;          // default 0.35 (a stair; higher is a wall or a table)
    VruiFall fall;              // default VRUI_FALL_BLINK
    float gravity;              // VRUI_FALL_DROP, m/s^2 (default 9.8)

    // Optional teleport pads. pads_only: nothing else is a valid target.
    const VruiTeleportPad *pads;
    int   npads;
    bool  pads_only;

    // Optional walls: how deep `p` is inside something solid (0 = in the
    // open). Nothing can stop a player walking in their room, so when their
    // head goes into a wall the view fades out instead (seeing the inside of
    // things is disorienting, and the fade says "not this way"). Stick
    // walking stops at walls (sliding along them), and teleports never land
    // you inside one.
    float (*solid_depth)(Vector3 p, void *user);
} VruiLocoConfig;

VruiLocoConfig vrui_loco_default(void);
// Call after the frame's handholds (they report which hands are climbing).
void vrui_locomotion(const VruiLocoConfig *cfg);

// A handhold: something to climb on. a..b is a bar (a rung, a monkey bar, a
// ledge's edge); a == b is a single hold (a rock). Take hold with the hand
// (the grab style in effect, see vrui_style()->grab); the laser never climbs.
// Drawn as a rod or ball of `radius` in `color`.
typedef struct {
    bool hovered, grabbed, held, released;
    SfxrHandId hand;            // the holding hand (when held/released)
} VruiHold;
VruiHold vrui_handhold(VruiId id, Vector3 a, Vector3 b, float radius, Color color);
bool     vrui_climbing(void);   // a hand is on a handhold: the rig follows it
bool     vrui_airborne(void);   // falling, or just let go and not landed yet

// ===========================================================================
// 9. Text and labels in the world (vrui_label.c)
// ===========================================================================
//
// Pick by who reads it, and from where:
//   vrui_text3d   floats and turns to face you (yaw only): names over things.
//   vrui_text_at  printed at a pose, facing its +Z: signs, plaques, the words
//                 on a control. Seen from behind, it isn't drawn at all.
//   vrui_tag      vrui_text3d on a dark plate, readable over any background.
//   vrui_callout  points AT something: a dot on `anchor`, a leader line up
//                 `lift_m`, and a tag. It keeps its apparent size as you step
//                 back (up to 5 cm letters) and hides past 8 m.
//   vrui_sign     a board with a title and lines of text (station signs).
// Text is left-aligned inside its block, and the block is centered on the
// position; "\n" starts a new line.
//
// How big: text reads comfortably at about 1 degree tall on the Frame, which
// is 1.75 cm per meter of distance: vrui_text_height(distance, 1.0f).
//
// Labels on moving things: every call takes a position or pose worked out
// THIS frame, so a label follows whatever you compute it from:
//   vrui_callout(sfxr_pose_apply(cube_pose, (Vector3){ 0, 0.05f, 0 }), "cube", 0.12f, WHITE);
// (docs/ATTACHING.md; the toolbox's "Attach & label" bench shows each kind.)

void    vrui_text3d(Vector3 position, const char *text, float height_m, Color color);   // billboard
void    vrui_text_at(SfxrPose pose, const char *text, float height_m, Color color);    // fixed, faces +Z
void    vrui_tag(Vector3 position, const char *text, float height_m, Color color, Color plate);
void    vrui_callout(Vector3 anchor, const char *text, float lift_m, Color color);
void    vrui_sign(SfxrPose pose, float width_m, const char *title, const char *body, Color board);   // pose = middle of the face
Vector2 vrui_text_size(const char *text, float height_m);    // meters (width, height)
float   vrui_text_height(float distance_m, float degrees);   // height that looks `degrees` tall from that far

// ===========================================================================
// 10. Things that go with the player: body, HUDs, hand menus (vrui_attach.c,
//     vrui_menu.c). The patterns are in docs/ATTACHING.md; the toolbox's
//     "Menus & HUD" station has one of each to try.
// ===========================================================================
//
// Anything can ride along with the player by working out its pose from one
// of these every frame:
//   the head      sfxr_head()           a visor HUD (keep it small and low)
//   a hand        sfxr_hand(h)->grip    a wrist watch, a tablet in your hand
//   the body      vrui_body()           a belt, holsters, a display at your waist
//   the rig       sfxr_rig_position()   things that travel with you (a follow HUD)

// The player's estimated body: on the floor under the head, facing where the
// head has been facing (it lags head turns under 45 degrees, drifts after
// you, and snap turns carry it). +Y up, -Z forward. For a belt at the waist:
//   sfxr_pose_mul(vrui_body(), (SfxrPose){ { 0, 0.6f * vrui_eye_height(), 0 }, QuaternionIdentity() })
SfxrPose vrui_body(void);
float    vrui_eye_height(void);         // eyes above the floor you stand on (m)

// Lazy follow ("tag-along"): returns where a HUD should be this frame. It
// stays put while `target` is within max_deg of it (seen from your eyes) and
// 30 cm in depth; once it's further off it glides to the target in about
// glide_s seconds, then rests again. It rides with the rig, so teleports and
// snap turns don't make it swoop. One id per follower.
SfxrPose vrui_follow(VruiId id, SfxrPose target, float max_deg, float glide_s);

// An arrow at the edge of your view pointing toward `target` when it is out
// of view (more than 30 degrees off where you look); nothing when it's in
// view. Returns true when drawn. For "bug behind you", "your ball is there".
bool vrui_offscreen_arrow(Vector3 target, const char *label, Color color);

// Draw over the world (no depth test) whatever is queued between these:
// HUDs, pointers, menus that must not disappear into a wall.
void vrui_on_top_begin(void);
void vrui_on_top_end(void);

// Wash the whole view with a color this frame (0..1): a damage flash, a
// "you can't go there" hint. Use it briefly and gently.
void vrui_tint(Color color, float alpha);

// Radial (pie) menu on one controller: hold `hold` (e.g. &hand->secondary),
// tilt the stick toward a choice, let go of the button to pick it. Returns
// the index picked (once, on release) or -1. Letting go with the stick
// centered cancels. The hand's stick is claimed while it is open.
int  vrui_radial_menu(VruiId id, SfxrHandId hand, const SfxrButton *hold, const char *const *items, int count);
bool vrui_radial_open(VruiId id);

// ===========================================================================
// 11. Smoothing and interpolation (vrui_smooth.c, docs/SMOOTHING.md)
// ===========================================================================
//
// Frame-rate independent: the same settings look the same at 72, 90 or 144 Hz
// (a bare "x = lerp(x, target, 0.1)" per frame does not).

// Exponential smoothing: half the remaining gap closes every `halflife`
// seconds (0 = snap). The simplest "lag behind a little".
float      vrui_damp(float current, float target, float halflife, float dt);
Vector3    vrui_damp3(Vector3 current, Vector3 target, float halflife, float dt);
Quaternion vrui_dampq(Quaternion current, Quaternion target, float halflife, float dt);
// Springs: `frequency_hz` is how fast it swings; `damping` 1 settles fastest
// without overshoot, below 1 it overshoots and wobbles. Keep x and v between
// frames.
void vrui_spring(float *x, float *v, float target, float frequency_hz, float damping, float dt);
void vrui_spring3(Vector3 *x, Vector3 *v, Vector3 target, float frequency_hz, float damping, float dt);
void vrui_springq(Quaternion *q, Vector3 *angular_velocity, Quaternion target, float frequency_hz, float damping, float dt);
// Speed limits: move at most max_step (meters) / turn at most max_radians.
Vector3    vrui_move_toward3(Vector3 current, Vector3 target, float max_step);
Quaternion vrui_turn_toward(Quaternion current, Quaternion target, float max_radians);
// Easing curves for tweens: t 0..1 in, 0..1 out (BACK and ELASTIC overshoot).
typedef enum { VRUI_EASE_LINEAR, VRUI_EASE_SMOOTH, VRUI_EASE_IN, VRUI_EASE_OUT, VRUI_EASE_IN_OUT,
               VRUI_EASE_BACK, VRUI_EASE_ELASTIC, VRUI_EASE_BOUNCE, VRUI_EASE_COUNT } VruiEase;
float vrui_ease(VruiEase ease, float t);

// A pose smoother: how a held thing (a weapon, a tool) follows the hand or
// bone it's attached to. Keep a VruiSmooth per thing; each frame pass the
// pose it's attached to and draw it where this returns.
//   SNAP    exactly on it: precise, weightless
//   LAG     trails a little (halflife): softer, a touch of weight
//   SPRING  overshoots and wobbles (frequency, damping): floppy, cartoony
//   HEAVY   pulled firmly, but speed-limited (max_speed, max_turn_deg): a
//           flick can't whip it round, a committed swing carries it
//   STEADY  a jitter filter: still hands stop trembling, fast motion has no
//           lag (min_cutoff, beta): aiming, drawing, pointing
// with_player (default true): the smoother rides the rig, so a teleport or
// snap turn doesn't smear the thing across the world; turn it off for things
// attached to the world (a character's hand bone).
typedef enum { VRUI_SMOOTH_SNAP, VRUI_SMOOTH_LAG, VRUI_SMOOTH_SPRING, VRUI_SMOOTH_HEAVY, VRUI_SMOOTH_STEADY,
               VRUI_SMOOTH_COUNT } VruiSmoothMode;
typedef struct {
    VruiSmoothMode mode;
    float halflife;          // LAG (s)
    float frequency, damping;// SPRING (Hz, ratio)
    float max_speed;         // HEAVY (m/s)
    float max_turn_deg;      // HEAVY (deg/s)
    float min_cutoff, beta;  // STEADY: cutoff when still (Hz), and how fast it opens up with speed
    bool  with_player;
} VruiSmoothSpec;
typedef struct {
    bool init;
    SfxrPose pose, raw, rig;
    Vector3 vel, ang_vel;
    float speed_r;
} VruiSmooth;
VruiSmoothSpec vrui_smooth_spec(VruiSmoothMode mode);   // tested defaults
SfxrPose       vrui_smooth_pose(VruiSmooth *s, SfxrPose target, const VruiSmoothSpec *spec);
void           vrui_smooth_reset(VruiSmooth *s, SfxrPose pose);   // jump there (e.g. on grabbing something new)
const char    *vrui_smooth_name(VruiSmoothMode mode);            // "Snap", "Lag"...

// ===========================================================================
// 12. The widget registry: widgets by name (vrui_registry.c, docs/TESTING.md)
// ===========================================================================
//
// Every widget reports each frame where it is and what it's set to, under a
// readable name: the id's group name + "." + its label, lower-case, "_" for
// spaces, anything from "(" on dropped. VRUI_ID2(G_TABLE, 1) labelled "SKY" in
// group "table" is "table.sky"; a panel's widgets are "<title>.<text>"
// ("toolbox.reset_blocks"). Tests find targets by name, so moving a bench
// doesn't break them; a target may name a part: "table.sky/handle" is the
// lever's moving part, "table.sky" (or "/base") where it's mounted.

void vrui_group_name(unsigned group, const char *name);   // e.g. vrui_group_name(G_TABLE, "table")
void vrui_name_widget(VruiId id, const char *label);      // for widgets without a label (a grab region)
// A named spot that isn't a widget (a character, a target on the table):
// tests and tools find it by name like one ("voice.bug_2"), kind "mark".
void vrui_mark(VruiId id, const char *label, SfxrPose pose);
typedef struct {
    char name[48];          // "table.sky"
    char label[24];         // as the event log shows it ("SKY")
    char kind[12];          // rotary, pivot, linear, tilt, hinge, press, rocker, grab, hold, valve, key, panel, button, toggle, slider...
    VruiId id;
    SfxrPose pose;          // where it's mounted
    SfxrPose part;          // its moving part (handle, cap...), or the same as pose
    float value;            // its value (a bool is 0 / 1; a press: how far in)
    bool has_value;
} VruiWidgetInfo;
int                   vrui_widget_count(void);            // last frame's widgets
const VruiWidgetInfo *vrui_widget_at(int i);
const VruiWidgetInfo *vrui_find_widget(const char *target, SfxrPose *pose);   // NULL if not seen last frame

// ===========================================================================
// 13. Queued 3D drawing helpers (call from logic code; drawn by vrui_draw)
// ===========================================================================

void vrui_box(SfxrPose pose, Vector3 size, Color color);
void vrui_line(Vector3 a, Vector3 b, Color color);
void vrui_fade(float alpha);   // darken the whole view this frame (0..1)
float vrui_faded(void);        // how dark the view is this frame so far (0..1): pause, mute...

// ===========================================================================
// 14. Wielding: things held by their handles, with weight (vrui_wield.c,
//     docs/WIELDING.md). Modeled on Blade & Sorcery's.
// ===========================================================================
//
// A sword, a hammer, a spear, a brick. Take it by a handle and it settles
// into your hand the way it's meant to be held -- blade up, hammer face
// forward -- wherever along the handle you took it, whatever angle it was
// lying at. Then:
//   * a second hand on a handle holds it too, and steers it (two_handed);
//     without two_handed the other hand takes it over
//   * sticky: it stays in your hand until you let go of the grip completely,
//     and LOOSENING the grip (not letting go) slides your hand along the
//     handle -- choke up on a spear, slide down a hammer's shaft
//   * weight: it follows your hand through springs set by its mass and how
//     far from its balance point you hold it: a feather is on your hand, a
//     hammer held at the end of its shaft swings behind your wrist, and an
//     anvil takes two hands to lift (lift_hands)
//   * laser + grip pulls a dropped thing back into your hand (pull)
//   * let go and it's loose: it falls, tumbles, bounces and settles on
//     whatever spec.ground says is below it; thrown, it keeps your swing
//     (capped by max_throw: you can't throw an anvil)
//
// Frames: the thing's own frame is yours to choose; a handle is a segment
// in it. Your hand holds a handle the way a fist holds a rod: the handle
// runs out of the thumb side (the grip pose's -Z), and `face` -- a hammer's
// striking face, a blade's edge -- points where your knuckles do (-Y).

typedef struct {
    Vector3 a, b;        // the handle, from a to b in the thing's frame (a == b: a single point, held along +Y)
    Vector3 face;        // the thing's direction that goes where your knuckles point
    int     rolls;       // ways round it sits in the hand: 1 (face forward only), 2 (forward or back), 0 (any: a round staff)
    bool    reversible;  // may be held upside down (a dagger's icepick grip): taken the way it lies
} VruiGrip;

typedef enum { VRUI_WEIGHT_FEATHER, VRUI_WEIGHT_LIGHT, VRUI_WEIGHT_MEDIUM, VRUI_WEIGHT_HEAVY, VRUI_WEIGHT_HUGE,
               VRUI_WEIGHT_COUNT } VruiWeight;

typedef struct {
    float   mass;        // kg (0: weightless -- exactly on the hand)
    Vector3 center;      // balance point (center of mass), own frame
    Vector3 box_center, half;   // collider, own frame: the laser, the hand's reach when there are no handles, and the ground
    VruiGrip grip[4];
    int     ngrips;      // 0: no handles -- held anywhere, the way you took it
    float   reach;       // how near a hand must be to a handle to take it (m)
    bool    two_handed;  // a second hand may hold it too
    int     lift_hands;  // hands it takes to lift it (an anvil: 2); with fewer it drags
    bool    sticky;      // held until the grip is fully let go
    bool    slide;       // (sticky) a loosened grip slides along the handle
    bool    pull;        // laser + grip pulls it to your hand
    // loose
    float   gravity;     // m/s/s (9.8; a balloon: negative)
    float   drag;        // air drag, 1/s (a feather: 4)
    float   bounce;      // 0..1
    float   max_throw;   // m/s: the fastest it can leave your hand
    float (*ground)(Vector3 at);   // the surface height under a point (NULL: the floor, y = 0)
} VruiWieldSpec;

VruiWieldSpec vrui_wield_spec(VruiWeight weight);   // tested defaults for a thing of that weight
const char   *vrui_weight_name(VruiWeight weight);   // "feather", "light"...

typedef struct {
    SfxrPose   pose;             // = *pose: draw it here
    Vector3    velocity, angular_velocity;
    int        hands;            // hands on it: 0, 1, 2
    SfxrHandId hand;             // the hand in charge (hands > 0), or that last held it
    bool       hovered;          // a hand could take it (or pull it) now
    bool       grabbed, released;// this frame: the first hand took it / the last let go
    bool       loose;            // falling, tumbling or sliding to rest
    bool       pulling;          // flying to a hand (laser pull)
    bool       sliding[2];       // that hand is sliding along the handle
    bool       straining;        // fewer hands on it than it takes to lift
    float      lag;              // how far behind the hands it is (m): the weight showing
} VruiWield;

VruiWield vrui_wield(VruiId id, SfxrPose *pose, const VruiWieldSpec *spec);
void      vrui_wield_drop(VruiId id);   // let go with every hand (it falls)

#ifdef __cplusplus
}
#endif

#endif // VRUI_H
