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
//   9. Queued 3D drawing helpers
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
    VRUI_CAPTURE_LOOK,       // both hands, while the panel is in front of your face
                             //   (a controller test panel: look at it, use everything)
    VRUI_CAPTURE_MODAL,      // both hands, every frame the panel is shown (dialogs)
} VruiCapture;
void vrui_panel_capture(VruiCapture mode);

// Begins a panel. `pose` is updated when the player drags it by the title bar
// (keep it in a persistent variable). title == NULL: no title bar, not
// movable. Returns true when the panel is shown -- pair with vrui_panel_end().
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
    SfxrPose   part;       // world pose of the moving part: draw your own model here
} VruiMech;

// ROTARY: turns about the base's +Y.
VruiMechSpec vrui_knob_spec(void);             // bounded knob, 3/4 turn, smooth, drag around or twist
VruiMechSpec vrui_selector_spec(int positions);// rotary switch: snaps between `positions` stops
VruiMechSpec vrui_crank_spec(void);            // endless handwheel: value = turns, drag around only
VruiMechSpec vrui_spinner_spec(int pegs);      // wheel of fortune: flick it, it coasts past `pegs`
VruiMech vrui_rotary(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);
// Knob with default feel; `turns` = rotations spanning min..max; the stick
// fine-adjusts while pointing at it. true when changed.
bool vrui_knob(VruiId id, SfxrPose base, float radius, float *value, float min, float max, float turns, const char *label);

// PIVOT: swings on the base's X axis; handle up along +Y mid-swing.
VruiMechSpec vrui_lever_spec(void);            // throttle lever, 80 deg of swing, 0..1
VruiMech vrui_pivot(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);
bool vrui_lever(VruiId id, SfxrPose base, float length, float *value, const char *label);   // 0 back .. 1 forward

// LINEAR: slides along the base's X axis, centered on the base.
VruiMechSpec vrui_slider_spec(void);           // slider / fader, 0..1
VruiMechSpec vrui_plunger_spec(void);          // sprung pull handle, 0..1, returns on release
VruiMech vrui_linear(VruiId id, SfxrPose base, const VruiMechSpec *spec, float *value);
bool vrui_slider3d(VruiId id, SfxrPose base, float length, float *t, const char *label);   // t 0..1

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
    bool (*valid_target)(Vector3 target, void *user);
    void *user;
} VruiLocoConfig;

VruiLocoConfig vrui_loco_default(void);
void vrui_locomotion(const VruiLocoConfig *cfg);

// ===========================================================================
// 9. Queued 3D drawing helpers (call from logic code; drawn by vrui_draw)
// ===========================================================================

void vrui_text3d(Vector3 position, const char *text, float height_m, Color color); // billboard, faces head
void vrui_box(SfxrPose pose, Vector3 size, Color color);
void vrui_line(Vector3 a, Vector3 b, Color color);
void vrui_fade(float alpha);   // darken the whole view this frame (0..1)

#ifdef __cplusplus
}
#endif

#endif // VRUI_H
