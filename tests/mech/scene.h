// scene.h - the test scene every tests/mech file shares: where each
// mechanism sits, its state, and the scripted hand paths (grab, orbit, slide,
// poke) the cases are built from.

#ifndef MECH_SCENE_H
#define MECH_SCENE_H

#include "sfxt.h"

#include <math.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Where everything is, and its state (scene.c)
// ---------------------------------------------------------------------------

#define TABLE_Y 0.9f
static inline SfxrPose at(float x, float z) { return (SfxrPose){ { x, TABLE_Y, z }, { 0, 0, 0, 1 } }; }

#define KNOB_AT     at(0.0f, -0.5f)
#define SELECTOR_AT at(0.3f, -0.5f)
#define CRANK_AT    at(-0.4f, -0.5f)
#define LEVER_AT    at(0.65f, -0.5f)
#define SLIDER_AT   at(0.0f, -0.9f)
#define PLUNGER_AT  at(0.5f, -0.9f)
#define STICK_AT    at(-0.5f, -0.9f)
#define BUTTON_AT   at(1.0f, -0.5f)
#define SWITCH_AT   at(1.0f, -0.9f)
#define SPRUNG_AT   at(0.8f, -1.25f)
// A door off the table to the left (hinge at handle height, opening toward
// the player) and a pull cord hanging to the right.
#define DOOR_HINGE  ((SfxrPose){ { -1.2f, 1.0f, -0.6f }, { 0, 0, 0, 1 } })
#define DOOR_W      0.6f
#define CORD_ANCHOR ((SfxrPose){ { 1.6f, 1.6f, -0.5f }, { 0, 0, 0, 1 } })
// A two-handed valve lying flat on the table (axis up), and a key switch
// with its key standing upright beside it, tip down.
#define VALVE_AT    at(-0.9f, -1.3f)
#define KEY_SLOT_AT at(1.45f, -0.9f)
#define KEY_REST    ((SfxrPose){ { 1.45f, 1.0f, -1.2f }, { 0, 0, 0, 1 } })

enum { ID_KNOB = 1, ID_SELECTOR, ID_CRANK, ID_LEVER, ID_SLIDER, ID_PLUNGER, ID_STICK, ID_BUTTON, ID_SWITCH, ID_SPRUNG,
       ID_DOOR, ID_CORD, ID_VALVE, ID_KEY };
extern VruiMechSpec valve_s;
extern float valve;
extern VruiMech valve_m;
extern VruiKeySpec key_s;
extern SfxrPose key_pose;
extern int key_pos;
extern VruiKey key_r;
extern VruiMechSpec door_s;
extern float door, cord;
extern VruiMech door_m;
extern int cord_fires;

extern VruiMechSpec knob_s, selector_s, crank_s, lever_s, slider_s, plunger_s, stick_s, sprung_s;
extern float sprung, set_point;
extern VruiMech sprung_m;
extern VruiPressSpec button_s;
extern VruiRockerSpec switch_s;
extern float knob, selector, crank, lever, slider, plunger;
extern Vector2 stick;
extern bool sw;
extern VruiMech knob_m, selector_m, crank_m, lever_m, slider_m, plunger_m, stick_m;
extern VruiPress button_p, switch_p;
extern int button_presses, button_releases, switch_flips, selector_changes;
extern bool specs_ready;

void scene(void);

// ---------------------------------------------------------------------------
// Hand paths and helpers
// ---------------------------------------------------------------------------

#define R SFXR_RIGHT
static const Quaternion PALM_DOWN = { -0.7071068f, 0, 0, 0.7071068f };   // pointing straight down

static inline SfxrPose pose(Vector3 p, Quaternion q) { return (SfxrPose){ p, q }; }
static inline Vector3 v3(float x, float y, float z) { return (Vector3){ x, y, z }; }
static inline Vector3 add(Vector3 a, Vector3 b) { return Vector3Add(a, b); }
// Point on a circle around `c` (vertical axis), angle a (rad, counter-clockwise from +X seen from above).
static inline Vector3 around(Vector3 c, float r, float a) { return v3(c.x + r * cosf(a), c.y, c.z - r * sinf(a)); }
// Let a weighted control catch up with the hand after a motion.
static inline void settle(void) { sfxt_frames(10); }
// Knob top center (the rotary collider is centered half its height up).
static inline Vector3 knob_center(SfxrPose base) { return add(base.position, v3(0, 0.0125f, 0)); }
static inline Vector3 lever_pivot(void) { return add(LEVER_AT.position, v3(0, 0.02f, 0)); }
static inline Vector3 slider_handle(SfxrPose base, float t, float len) { return add(base.position, v3((t - 0.5f) * len, 0.018f, 0)); }
static inline Vector3 stick_ball(void) { return add(STICK_AT.position, v3(0, 0.015f + 0.12f, 0)); }
static inline Vector3 door_handle(void) { return add(DOOR_HINGE.position, v3(DOOR_W, 0, 0)); }   // closed
static inline Vector3 cord_handle(float pull) { return add(CORD_ANCHOR.position, v3(0, -(0.35f + pull * 0.25f), 0)); }
// A point on the valve's rim, `deg` clockwise (seen from above) from its far (-Z) side.
static inline Vector3 valve_rim(float deg)
{
    float a = deg * DEG2RAD, r = 0.2f;
    return add(VALVE_AT.position, v3(r * sinf(a), 0.12f, -r * cosf(a)));
}
static inline Vector3 key_bow(SfxrPose key) { return sfxr_pose_apply(key, v3(0, 0.06f, 0)); }
static inline Vector3 cap_top(void) { return add(BUTTON_AT.position, v3(0, 0.02f, 0)); }
// A fingertip at `p`, pointing straight down.
static inline SfxrPose tip_at(Vector3 p) { return sfxt_tip_pose(p, v3(0, -1, 0)); }

typedef struct {
    Vector3 c;          // rotation center
    float r;            // radius the hand orbits at
    float a0, a1;       // start / end angle
    float down, wobble; // press down along the axis (m); radial wobble amplitude (m)
    float twist;        // wrist twist (rad) about +Y, added along the way
    float slide;        // straight side push through the center (m, back and forth)
    Quaternion q0;
} Orbit;

typedef struct { Vector3 eye, c; float r, a0, a1; } LaserOrbit;

typedef struct { Vector3 pivot; float L, a0, a1; Vector3 drift; } LeverPath;

typedef struct { Vector3 from; Vector3 move; Vector3 drift; } Line;

typedef struct { Vector3 eye, from, move; } LaserLine;

typedef struct { Vector3 top; } Jitter;

static inline void grab_at(Vector3 p)
{
    sfxt_hand_to(R, pose(add(p, v3(0, 0.08f, 0)), PALM_DOWN), 0.25f);
    sfxt_hand_to(R, pose(p, PALM_DOWN), 0.15f);
    sfxt_grip(R, 1.0f);
    sfxt_frames(3);
}

static inline void let_go(void)
{
    sfxt_grip(R, 0.0f);
    sfxt_trigger(R, 0.0f);
    sfxt_frames(3);
}

// where the hand takes a knob or selector to drag it round: near its rim
#define RIM (knob_s.size * 0.85f)

static inline float knob_after(float deg, float grip_r)
{
    float slop_deg = grip_r > knob_s.size * 0.25f ? fmaxf(2.0f, 0.003f / grip_r * RAD2DEG) : 2.0f;
    return 0.5f + (deg - slop_deg) / 270.0f;
}

static inline SfxrPose orbit_path(float t, void *u)
{
    const Orbit *o = u;
    float a = o->a0 + (o->a1 - o->a0) * t;
    float r = o->r + o->wobble * sinf(t * 6.0f * PI);
    Vector3 p = around(o->c, r, a);
    p.y -= o->down * t;
    p.x += o->slide * sinf(t * 4.0f * PI);
    Quaternion q = QuaternionMultiply(QuaternionFromAxisAngle(v3(0, 1, 0), o->twist * t), o->q0);
    return pose(p, q);
}

static inline SfxrPose laser_orbit_path(float t, void *u)
{
    const LaserOrbit *o = u;
    Vector3 spot = around(o->c, o->r, o->a0 + (o->a1 - o->a0) * t);
    SfxrPose p = sfxt_tip_pose(o->eye, Vector3Subtract(spot, o->eye));
    p.position = o->eye;
    return p;
}

static inline SfxrPose lever_path(float t, void *u)
{
    const LeverPath *l = u;
    float a = l->a0 + (l->a1 - l->a0) * t;
    Vector3 p = add(l->pivot, v3(0, l->L * cosf(a), l->L * sinf(a)));
    return pose(add(p, Vector3Scale(l->drift, t)), PALM_DOWN);
}

static inline SfxrPose line_path(float t, void *u)
{
    const Line *l = u;
    Vector3 p = add(add(l->from, Vector3Scale(l->move, t)), Vector3Scale(l->drift, sinf(t * PI)));
    return pose(p, PALM_DOWN);
}

static inline SfxrPose laser_line_path(float t, void *u)
{
    const LaserLine *o = u;
    Vector3 spot = add(o->from, Vector3Scale(o->move, t));
    SfxrPose p = sfxt_tip_pose(o->eye, Vector3Subtract(spot, o->eye));
    p.position = o->eye;
    return p;
}

static inline SfxrPose switch_jitter_path(float t, void *u)
{
    const Jitter *j = u;
    // bob 4 mm in and out across the switch's top surface, 6 times
    return tip_at(add(j->top, v3(0, 0.004f * sinf(t * 12.0f * PI), 0)));
}

// ---------------------------------------------------------------------------
// The cases (one file per mechanism family)
// ---------------------------------------------------------------------------
void knob_orbit_quarter_turn(void);
void knob_events_name_it(void);
void door_pull_open(void);
void door_lean_on_handle_ignored(void);
void cord_fires_once_per_pull(void);
void knob_twist_quarter_turn(void);
void knob_press_down_while_turning(void);
void knob_side_push_while_twisting(void);
void knob_grab_does_not_nudge(void);
void knob_grab_off_axis_no_jump(void);
void knob_hold_survives_drift(void);
void knob_end_stop_holds(void);
void knob_laser_orbit(void);
void knob_laser_fast_spin_is_limited(void);
void plunger_tension_hum(void);
void plunger_tension_gentle_near_rest(void);
void sprung_lever_returns_to_set_point(void);
void selector_turn_snaps(void);
void selector_no_chatter_at_boundary(void);
void crank_two_turns(void);
void lever_push_forward(void);
void lever_sideways_push_ignored(void);
void lever_grab_no_jump(void);
void slider_off_center_grab(void);
void slider_lift_and_lean_ignored(void);
void slider_laser(void);
void plunger_pull_and_return(void);
void joystick_tilt_and_return(void);
void joystick_push_down_ignored(void);
void button_press_from_above(void);
void button_side_brush_does_nothing(void);
void button_press_then_slide(void);
void grab_by_closing_hand(void);
void switch_poke_flips_once(void);
void valve_two_hands_turn_it(void);
void valve_one_hand_wont_budge(void);
void key_insert_then_turn(void);
void key_sideways_does_not_go_in(void);
void key_pull_out_only_at_off(void);
void key_start_springs_back(void);

#endif
