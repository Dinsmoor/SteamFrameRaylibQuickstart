// vrui_mech.c - the continuous mechanisms, by the motion they make:
//   vrui_rotary  turns about an axis   (knob, selector, crank, spinner)
//   vrui_pivot   swings on a pivot     (lever)
//   vrui_linear  slides along a line   (slider, plunger)
//   vrui_tilt    tilts two ways        (joystick)
// plus their short forms (vrui_knob, vrui_lever, vrui_slider3d, vrui_joystick).
//
// All of them share one core:
//   DRIVES       turn hand motion into motion along the mechanism's own
//                freedom (an angle about an axis, a distance along a line, a
//                tilt). Everything off that freedom is projected away.
//   VALUE MODEL  range, detents, snapping, springs, end stops, resistance,
//                and the haptics that stand in for physical feel.
//
// See vrui.h ("Mechanisms") for the rules, docs/MECHANISMS.md for what each
// control promises the player, and tests/mech/ for the proof.

#include "vrui_internal.h"

// What a mechanism remembers between frames (in its VruiItem).
typedef struct {
    float   target;          // where the hand is driving the value
    float   actual;          // where it is (follows target with weight and a speed limit)
    float   velocity;        // of `actual`, value units/s (spinners coast with it)
    float   slop_acc;        // break-in: motion accumulated since the grab
    float   twist_prev;      // rotary drive: last wrist twist
    float   orbit_prev;      // rotary drive: last angle of the hand around the axis
    float   grab_radius;     // rotary drive: hand's distance from the axis at grab (0: near it)
    float   linear_prev;     // linear drive: last position along the axis
    float   detent_pos_prev; // smooth detents: last position, in steps
    float   tension_phase;   // tension hum wobble
    Vector2 tilt_at_grab;    // joystick
    int     detent;          // snapping: current stop; smooth: the stop that last ticked
    unsigned flags;          // M_*
    float   probe_prev;      // break-switch builds only: last off-axis distance
} MechState;
VRUI_STATE_FITS(MechState);

enum { M_BROKE_IN = 1, M_AT_MIN = 2, M_AT_MAX = 4, M_TICK_ARMED = 8 };

static MechState *mech_state(VruiItem *it) { return VRUI_STATE(it, MechState); }

// Test builds only (sfxr_break.h): the off-axis motion a naive drive would
// wrongly count. Kept apart from the drives so their real math reads cleanly;
// in normal builds both are always 0.
//   naive_leak:        this frame's change (for drives that add up per-frame deltas)
//   naive_since_grab:  the change since the grab (for drives measured from the grab point)
static float naive_leak(MechState *ms, float off_axis)
{
    if (!SFXR_BREAK(vrui_mech_lateral_leak)) return 0.0f;
    float d = off_axis - ms->probe_prev;
    ms->probe_prev = off_axis;
    return d;
}

static float naive_since_grab(const MechState *ms, float off_axis)
{
    return SFXR_BREAK(vrui_mech_lateral_leak) ? off_axis - ms->probe_prev : 0.0f;
}

// ---------------------------------------------------------------------------
// Specs (the documented defaults)
// ---------------------------------------------------------------------------

static VruiMechSpec base_spec(void)
{
    VruiMechSpec s = {0};
    s.min = 0.0f;
    s.max = 1.0f;
    s.reach = 0.06f;
    s.haptic_tick = 0.12f;
    s.haptic_stop = 0.4f;
    s.haptic_strain = 0.35f;
    s.haptic_tension = 0.3f;
    s.draw = true;
    s.color = (Color){ 60, 64, 74, 255 };
    s.value_format = "%s %.2f";
    s.display_scale = 1.0f;
    return s;
}

VruiMechSpec vrui_knob_spec(void)
{
    VruiMechSpec s = base_spec();
    s.travel = 0.75f * 2.0f * PI;      // 3/4 turn end to end: no regrip needed
    s.size = 0.035f;
    s.slop = 2.0f * DEG2RAD;
    s.twist = true;
    s.max_speed = 3.0f * 2.0f * PI;      // 3 turns/s by hand
    s.far_max_speed = 1.0f * 2.0f * PI;  // 1 turn/s by laser: circling the laser can't spin it
    s.weight = 0.03f;
    s.slip = 45.0f * DEG2RAD;
    return s;
}

VruiMechSpec vrui_selector_spec(int positions)
{
    VruiMechSpec s = vrui_knob_spec();
    if (positions < 2) positions = 2;
    s.max = (float)(positions - 1);
    s.detents = positions;
    s.snap = true;
    s.travel = (float)(positions - 1) * 30.0f * DEG2RAD;   // 30 deg per position
    s.value_format = "%s %.0f";
    return s;
}

VruiMechSpec vrui_crank_spec(void)
{
    VruiMechSpec s = base_spec();
    s.endless = true;
    s.travel = 2.0f * PI;               // value counts turns
    s.detents = 12;                     // a ratchet click every 30 deg
    s.size = 0.09f;
    s.slop = 3.0f * DEG2RAD;
    s.twist = false;                    // you crank with your arm, not your wrist
    s.max_speed = 2.0f * 2.0f * PI;     // a heavy wheel: 2 turns/s at most ...
    s.far_max_speed = 0.75f * 2.0f * PI;
    s.weight = 0.08f;                   // ... that takes a moment to get going
    s.slip = 90.0f * DEG2RAD;
    s.haptic_strain = 0.45f;
    s.haptic_tick = 0.08f;
    s.value_format = "%s %.2f turns";
    return s;
}

VruiMechSpec vrui_spinner_spec(int pegs)
{
    VruiMechSpec s = vrui_crank_spec();
    s.detents = pegs > 0 ? pegs : 12;   // a click per peg
    s.size = 0.16f;
    s.twist = false;
    s.coast = 0.6f;                     // flick it: it coasts for a few seconds
    s.max_speed = 4.0f * 2.0f * PI;
    s.far_max_speed = 2.0f * 2.0f * PI;
    s.weight = 0.05f;
    s.haptic_tick = 0.1f;
    s.value_format = NULL;
    return s;
}

VruiMechSpec vrui_lever_spec(void)
{
    VruiMechSpec s = base_spec();
    s.travel = 80.0f * DEG2RAD;         // +-40 deg of swing
    s.size = 0.18f;
    s.reach = 0.07f;
    s.slop = 1.5f * DEG2RAD;
    s.max_speed = 240.0f * DEG2RAD;
    s.far_max_speed = 120.0f * DEG2RAD;
    s.weight = 0.04f;
    s.slip = 25.0f * DEG2RAD;
    s.color = (Color){ 210, 60, 50, 255 };
    s.value_format = "%s %.0f%%";
    s.display_scale = 100.0f;
    return s;
}

VruiMechSpec vrui_slider_spec(void)
{
    VruiMechSpec s = base_spec();
    s.size = 0.25f;
    s.travel = 0.25f;
    s.slop = 0.004f;
    s.max_speed = 1.5f;
    s.far_max_speed = 0.6f;
    s.weight = 0.03f;
    s.slip = 0.05f;
    s.color = (Color){ 200, 200, 210, 255 };
    s.value_format = "%s %.0f%%";
    s.display_scale = 100.0f;
    return s;
}

VruiMechSpec vrui_plunger_spec(void)
{
    VruiMechSpec s = vrui_slider_spec();
    s.size = 0.15f;
    s.travel = 0.15f;
    s.spring = true;
    s.rest = 0.0f;
    s.color = (Color){ 230, 170, 60, 255 };
    return s;
}

VruiMechSpec vrui_joystick_spec(void)
{
    VruiMechSpec s = base_spec();
    s.min = -1.0f;
    s.max = 1.0f;
    s.travel = 0.06f;                   // hand motion (m) for full deflection
    s.size = 0.12f;
    s.slop = 0.004f;                    // radial dead zone on the hand motion
    s.spring = true;
    s.color = (Color){ 40, 42, 48, 255 };
    s.value_format = NULL;
    return s;
}

// ---------------------------------------------------------------------------
// Value model
// ---------------------------------------------------------------------------

static float range_of(const VruiMechSpec *sp) { return sp->max - sp->min; }

static float detent_step(const VruiMechSpec *sp)
{
    if (sp->endless) return sp->detents >= 1 ? range_of(sp) / (float)sp->detents : 0.0f;
    return sp->detents >= 2 ? range_of(sp) / (float)(sp->detents - 1) : 0.0f;
}

static int nearest_detent(const VruiMechSpec *sp, float v)
{
    float step = detent_step(sp);
    return step != 0.0f ? (int)floorf((v - sp->min) / step + 0.5f) : -1;
}

static void pulse(int hand, float amp, float secs)
{
    if (hand >= 0 && amp > 0) vrui_haptic_pulse((SfxrHandId)hand, amp, secs, 0);
}

// Physical units (rad or m) per value unit.
static float per_value(const VruiMechSpec *sp)
{
    float r = fabsf(range_of(sp));
    return r > 1e-6f ? fabsf(sp->travel) / r : 1.0f;
}

// Start of a hold: continue from wherever the value is now.
static void model_grab(MechState *ms, const VruiMechSpec *sp, float value)
{
    ms->target = ms->actual = value;
    ms->velocity = 0;
    ms->slop_acc = 0;
    ms->flags = 0;
    float step = detent_step(sp);
    ms->detent_pos_prev = step != 0.0f ? (value - sp->min) / step : 0.0f;
    ms->detent = nearest_detent(sp, value);
}

// Spring tension: while a sprung control is held away from its rest point,
// a hum that gets stronger, higher and wobbles faster the further it is --
// the only way to tell the hand "this wants to go back". Strength grows with
// the SQUARE of the displacement: barely there near rest (these motors are
// strong even at low amplitude; a linear ramp felt like full power at 10%),
// building to haptic_tension at the end of travel.
static void tension_hum(MechState *ms, const VruiMechSpec *sp, float displacement01, int hand)
{
    if (!sp->spring || sp->haptic_tension <= 0 || hand < 0 || displacement01 < 0.05f) return;
    if (SFXR_BREAK(vrui_mech_no_tension)) return;
    float d = Clamp(displacement01, 0, 1);
    ms->tension_phase += sfxr_dt() * 2.0f * PI * (2.0f + 8.0f * d);   // wobble 2 -> 10 Hz
    float curve = SFXR_BREAK(vrui_mech_tension_linear) ? d : d * d;
    float amp = sp->haptic_tension * curve * (0.75f + 0.25f * sinf(ms->tension_phase));
    if (amp < 0.01f) return;
    vrui_haptic_hum((SfxrHandId)hand, amp, 60.0f + 160.0f * d);
}

// One held frame: the hand moved the target by dv (value units). The control
// follows with its weight and top speed; if it falls more than `slip` behind,
// the grip slips (the extra is dropped, so it never keeps going after you
// stop) and you feel strain. Returns true when *value changed.
static bool model_drive(MechState *ms, const VruiMechSpec *sp, float *value, float dv, int hand, bool far)
{
    float k = per_value(sp);
    float lo = fminf(sp->min, sp->max), hi = fmaxf(sp->min, sp->max);
    float target = ms->target + dv;
    float actual = ms->actual;
    bool resist = !SFXR_BREAK(vrui_mech_no_resistance);
    // End stops hold the target too, so coming back off a stop responds at
    // once (no "unwinding" what you over-turned) -- like a real stop.
    if (!sp->endless && !SFXR_BREAK(vrui_mech_stop_unwind)) target = Clamp(target, lo, hi);
    float slipv = sp->slip / k;
    if (resist && slipv > 0) target = Clamp(target, actual - slipv, actual + slipv);
    ms->target = target;

    // follow: weight (inertia), then the speed limit
    float dt = sfxr_dt();
    float next = target;
    if (resist) {
        if (sp->weight > 0) next = actual + (target - actual) * (1.0f - expf(-dt / sp->weight));
        float ms = (far && sp->far_max_speed > 0 ? sp->far_max_speed : sp->max_speed) / k;
        if (ms > 0) next = actual + Clamp(next - actual, -ms * dt, ms * dt);
    }
    if (!sp->endless) next = Clamp(next, lo, hi);
    if (dt > 0) ms->velocity += ((next - actual) / dt - ms->velocity) * 0.4f;   // smoothed, for release
    ms->actual = next;

    // strain: the hand is ahead of the control
    float lag = fabsf(target - next) * k;
    float felt = sp->slip > 0 ? sp->slip * 0.15f : 0.05f;
    if (resist && sp->haptic_strain > 0 && lag > felt && hand >= 0)
        vrui_haptic_hum((SfxrHandId)hand, sp->haptic_strain * Clamp(lag / fmaxf(sp->slip, 1e-3f), 0.3f, 1.0f), 230.0f);

    // stops are felt when the control itself arrives there
    if (!sp->endless) {
        float margin = (hi - lo) * 0.02f;
        if (next <= lo) {
            if (!(ms->flags & M_AT_MIN)) { ms->flags |= M_AT_MIN; pulse(hand, sp->haptic_stop, 0.02f); }
        } else if (next > lo + margin) ms->flags &= ~M_AT_MIN;
        if (next >= hi) {
            if (!(ms->flags & M_AT_MAX)) { ms->flags |= M_AT_MAX; pulse(hand, sp->haptic_stop, 0.02f); }
        } else if (next < hi - margin) ms->flags &= ~M_AT_MAX;
    }

    float raw = next;
    float out = raw;
    float step = detent_step(sp);
    if (step != 0.0f) {
        float pos = (raw - sp->min) / step;
        if (sp->snap && !SFXR_BREAK(vrui_mech_no_snap)) {
            // Change position only well past the halfway point (hysteresis),
            // so a hand resting near the boundary doesn't chatter.
            int kk = ms->detent;
            if (fabsf(pos - (float)kk) > (SFXR_BREAK(vrui_selector_chatter) ? 0.5f : 0.65f)) {
                kk = (int)floorf(pos + 0.5f);
                ms->detent = kk;
                pulse(hand, sp->haptic_tick, 0.008f);
            }
            out = sp->min + (float)kk * step;
        } else {
            // Smooth motion that ticks as it passes each stop. A stop that just
            // ticked re-arms only after moving 0.2 steps away from it.
            float prev = ms->detent_pos_prev;
            int last = ms->detent;
            if (fabsf(pos - (float)last) > 0.2f) ms->flags |= M_TICK_ARMED;
            if (floorf(prev) != floorf(pos)) {
                int crossed = (int)fmaxf(floorf(prev), floorf(pos));
                if (crossed != last || (ms->flags & M_TICK_ARMED)) {
                    ms->detent = crossed;
                    ms->flags &= ~M_TICK_ARMED;
                    pulse(hand, sp->haptic_tick, 0.008f);
                }
            }
            ms->detent_pos_prev = pos;
        }
    }
    if (sp->spring) {
        // displacement as a fraction of the farthest it can get from rest
        // (half the range for a centered spring, all of it for a plunger)
        float reach = fmaxf(fabsf(sp->max - sp->rest), fabsf(sp->rest - sp->min));
        tension_hum(ms, sp, fabsf(out - sp->rest) / fmaxf(reach, 1e-6f), hand);
    }
    bool changed = out != *value;
    *value = out;
    return changed;
}

// Not held: springs glide home; smooth detented controls settle on a stop;
// coasting wheels keep spinning and slow down.
static bool model_free(MechState *ms, const VruiMechSpec *sp, float *value, bool just_released)
{
    float v = *value;
    if (sp->endless && sp->coast > 0 && fabsf(ms->velocity) > 1e-3f) {
        float dt = sfxr_dt();
        v += ms->velocity * dt;
        ms->velocity *= expf(-sp->coast * dt);
        if (fabsf(ms->velocity) < 0.02f) ms->velocity = 0;
    } else if (sp->spring && !SFXR_BREAK(vrui_mech_no_spring)) {
        float k = 1.0f - expf(-sfxr_dt() / 0.05f);
        v += (sp->rest - v) * k;
        if (fabsf(v - sp->rest) < 1e-4f * fmaxf(1.0f, fabsf(range_of(sp)))) v = sp->rest;
    } else if (just_released && detent_step(sp) != 0.0f && !sp->snap) {
        v = sp->min + (float)nearest_detent(sp, v) * detent_step(sp);
    }
    bool changed = v != *value;
    *value = v;
    return changed;
}

// Break-in: motion right after taking hold is ignored until it adds up to
// `slop`, so the act of grabbing never nudges the control. After that every
// bit of motion counts (the slop itself is dropped, so there's no jump).
static float break_in(MechState *ms, float d, float slop)
{
    if ((ms->flags & M_BROKE_IN) || SFXR_BREAK(vrui_mech_no_slop)) return d;
    ms->slop_acc += d;
    if (fabsf(ms->slop_acc) <= slop) return 0.0f;
    ms->flags |= M_BROKE_IN;
    return ms->slop_acc - copysignf(slop, ms->slop_acc);
}

// ---------------------------------------------------------------------------
// Drives
// ---------------------------------------------------------------------------

static float wrap_pi(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

// Angle of q's rotation around `axis` (swing-twist decomposition).
static float twist_about(Quaternion q, Vector3 axis)
{
    float d = q.x * axis.x + q.y * axis.y + q.z * axis.z;
    return wrap_pi(2.0f * atan2f(d, q.w));
}

// Signed angle of v around `axis` (counter-clockwise looking down the axis),
// against a fixed reference direction; only differences are used.
static float angle_around(Vector3 v, Vector3 axis)
{
    Vector3 ref = fabsf(axis.y) < 0.9f ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 };
    ref = Vector3Normalize(Vector3Subtract(ref, Vector3Scale(axis, Vector3DotProduct(ref, axis))));
    Vector3 ref2 = Vector3CrossProduct(axis, ref);
    return atan2f(Vector3DotProduct(v, ref2), Vector3DotProduct(v, ref));
}

// Rotary break-in: the spec's angle, or 3 mm of hand travel at the radius the
// hand took hold at, whichever is larger -- tremor moves the hand, and near
// the axis a millimeter is a big angle.
#define ROTARY_SLOP_TRAVEL 0.003f
static float rotary_slop(const VruiMechSpec *sp, const MechState *ms)
{
    return ms->grab_radius > 1e-3f ? fmaxf(sp->slop, ROTARY_SLOP_TRAVEL / ms->grab_radius) : sp->slop;
}

// ROTARY DRIVE: this frame's rotation (rad, counter-clockwise about `axis`).
//
//   ORBIT  the angle of the hand (or laser spot) around the axis -- dragging
//          it around like a lazy susan. Only the hand's position in the plane
//          of rotation counts: pushing down along the axis or leaning on it
//          sideways (radially) changes nothing.
//   TWIST  the wrist's rotation about the axis (twist_ok). Tilting the wrist
//          (swing) is ignored.
//
// Orbit is trusted in proportion to the distance from the axis (it's noise at
// the center). When orbit and twist agree, the larger wins -- grabbing a
// knob's rim and twisting moves the hand a little and the wrist a lot, while
// dragging it around moves the hand a lot -- otherwise they blend.
static float rotary_drive(VruiItem *it, MechState *ms, const VruiHandle *hd, Vector3 center, Vector3 axis,
                          float radius, bool twist_ok, Vector3 *spot, bool *have_spot)
{
    const SfxrHand *hand = sfxr_hand((SfxrHandId)hd->hand);
    Quaternion q = hd->mode == VRUI_MODE_HAND ? hand->grip.orientation : hand->aim.orientation;
    Vector3 p;
    bool have_p = vrui__drag_point(hd->hand, hd->mode, center, axis, &p);
    Vector3 r = have_p ? Vector3Subtract(p, center) : (Vector3){ 0 };
    r = Vector3Subtract(r, Vector3Scale(axis, Vector3DotProduct(r, axis)));
    float orbit_r = Vector3Length(r);
    float orbit = angle_around(r, axis);
    if (hd->grabbed) it->rel.orientation = q;
    float tw = twist_about(QuaternionMultiply(q, QuaternionInvert(it->rel.orientation)), axis);
    *have_spot = have_p;
    *spot = Vector3Add(center, r);
    float leak = naive_leak(ms, have_p ? Vector3DotProduct(Vector3Subtract(p, center), axis) : 0.0f) / fmaxf(radius, 0.01f);
    if (hd->grabbed) {
        // Orbit only counts from a quarter radius out (see w below); closer in
        // the twist carries it and the spec's angle is the right break-in.
        ms->grab_radius = orbit_r > radius * 0.25f ? orbit_r : 0.0f;
        ms->twist_prev = tw;
        ms->orbit_prev = orbit;
        if (SFXR_BREAK(vrui_mech_absolute)) ms->twist_prev = ms->orbit_prev = 0;
        return 0.0f;
    }

    float d_tw = wrap_pi(tw - ms->twist_prev), d_orbit = wrap_pi(orbit - ms->orbit_prev);
    ms->twist_prev = tw;
    ms->orbit_prev = orbit;
    float w = have_p ? Clamp((orbit_r - radius * 0.25f) / (radius * 0.5f), 0.0f, 1.0f) : 0.0f;
    w = w * w * (3.0f - 2.0f * w);
    // test builds: break switches
    if (SFXR_BREAK(vrui_rotary_orbit_everywhere)) w = 1.0f;
    if (SFXR_BREAK(vrui_rotary_twist_only)) w = 0.0f;
    if (SFXR_BREAK(vrui_rotary_no_twist)) d_tw = 0.0f;

    float d;
    float o = w * d_orbit;
    if (!twist_ok) d = o;
    else if (o * d_tw > 0.0f) d = fabsf(o) > fabsf(d_tw) ? o : d_tw;
    else d = o + (1.0f - w) * d_tw;
    return d + leak;
}

// LINEAR DRIVE: this frame's motion (m) along `axis`. Motion across the axis
// (lifting, pushing sideways) is projected away. With the laser, the spot is
// taken on the plane that contains the axis and faces the laser.
static float linear_drive(MechState *ms, const VruiHandle *hd, Vector3 origin, Vector3 axis)
{
    Vector3 n;
    if (hd->mode == VRUI_MODE_HAND) n = (Vector3){ 0, 1, 0 };
    else {
        Vector3 dir = vrui__hand_ray(hd->hand).direction;
        n = Vector3CrossProduct(axis, Vector3CrossProduct(dir, axis));
        if (Vector3Length(n) < 1e-3f) return 0.0f;
        n = Vector3Normalize(n);
    }
    Vector3 p;
    if (!vrui__drag_point(hd->hand, hd->mode, origin, n, &p)) return 0.0f;
    Vector3 rel = Vector3Subtract(p, origin);
    float s = Vector3DotProduct(rel, axis);
    float leak = naive_leak(ms, Vector3Length(Vector3Subtract(rel, Vector3Scale(axis, s))));
    if (hd->grabbed) { ms->linear_prev = SFXR_BREAK(vrui_mech_absolute) ? 0.0f : s; return 0.0f; }
    float d = s - ms->linear_prev;
    ms->linear_prev = s;
    return d + leak;
}

static const char *value_label(const VruiMechSpec *sp, float v)
{
    if (!sp->label) return NULL;
    if (!sp->value_format) return sp->label;
    return TextFormat(sp->value_format, sp->label, v * sp->display_scale);
}

// Thumbstick fine-adjust while pointing at a control (not holding it).
static bool stick_adjust(VruiId id, MechState *ms, const VruiMechSpec *sp, float *value)
{
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        if (!vrui__ray_hot(h, id) || fabsf(hand->stick.x) < 0.3f) continue;
        float span = sp->endless ? 1.0f : fabsf(range_of(sp));
        ms->target = ms->actual = *value;   // continue from whatever the app set
        return model_drive(ms, sp, value, hand->stick.x * span * 0.25f * sfxr_dt(), h, false);
    }
    return false;
}

// "How do I use this?" -- every mechanism takes a grab or the laser.
static void mech_hint(VruiId id, const VruiMechSpec *sp)
{
    vrui__name(id, sp->label);
    vrui__hint(id, TextFormat("%s | or laser + trigger%s", vrui__grab_words(), vrui__pull_suffix()));
}

static VruiMech result_from(const VruiHandle *hd, float value)
{
    VruiMech m = {0};
    m.hovered = hd->hovered;
    m.grabbed = hd->grabbed;
    m.held = hd->held;
    m.released = hd->released;
    m.hand = hd->hand >= 0 ? (SfxrHandId)hd->hand : SFXR_LEFT;
    m.via_ray = hd->hand >= 0 && hd->mode != VRUI_MODE_HAND;
    m.value = value;
    m.detent = -1;
    return m;
}

// Near-grab distances, limited to the spec's reach (-1: out of reach).
static void reach_limit(float prox[2], float reach)
{
    for (int h = 0; h < 2; h++) if (prox[h] > reach) prox[h] = -1;
}

// ---------------------------------------------------------------------------
// Rotary: knob, selector, crank (axis = base +Y)
// ---------------------------------------------------------------------------

VruiMech vrui_rotary(VruiId id, SfxrPose base, const VruiMechSpec *sp, float *value)
{
    VruiItem *it = vrui__item(id);
    MechState *ms = mech_state(it);
    float R = sp->size;
    float height = sp->endless ? 0.02f : 0.025f;
    Vector3 axis = Vector3RotateByQuaternion((Vector3){ 0, 1, 0 }, base.orientation);
    SfxrPose body = base;
    body.position = sfxr_pose_apply(base, (Vector3){ 0, height * 0.5f, 0 });
    Vector3 half = { R, height * 0.5f, R };
    float ray[2], prox[2];
    vrui__ray_box_all(body, half, ray);
    vrui__prox_box_all(body, half, prox);
    reach_limit(prox, sp->reach);
    VruiHandle hd = vrui__handle_update(id, it, ray, prox);
    mech_hint(id, sp);

    bool changed = false, have_spot = false;
    Vector3 spot = body.position;
    if (hd.grabbed) model_grab(ms, sp, *value);
    if (hd.held) {
        // Measured on the top face: that's where the laser spot is. (For the
        // hand any point on the axis gives the same angle.)
        Vector3 top_c = sfxr_pose_apply(base, (Vector3){ 0, height, 0 });
        float d = rotary_drive(it, ms, &hd, top_c, axis, R, sp->twist, &spot, &have_spot);
        d = break_in(ms, d, rotary_slop(sp, ms));
        // clockwise (negative about +Y) increases the value
        changed = model_drive(ms, sp, value, -d * range_of(sp) / sp->travel, hd.hand, hd.mode != VRUI_MODE_HAND);
    } else {
        changed = stick_adjust(id, ms, sp, value);
        changed |= model_free(ms, sp, value, hd.released);
    }

    VruiMech m = result_from(&hd, *value);
    if (hd.released) sfxr_event("value", "%s %.3f", vrui__who(id), *value);
    m.changed = changed;
    m.detent = nearest_detent(sp, *value);
    m.position = -(*value - sp->min) / range_of(sp) * sp->travel;
    if (sp->endless) m.position = wrap_pi(m.position);
    Vector3 top = sfxr_pose_apply(base, (Vector3){ 0, height, 0 });
    m.part.position = top;
    m.part.orientation = QuaternionMultiply(base.orientation, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, m.position));

    if (sp->draw) {
        Vector3 bot = sfxr_pose_apply(base, (Vector3){ 0, 0.001f, 0 });
        Color col = vrui__hover_tint(sp->color, hd.hovered, hd.held);
        if (sp->endless && sp->coast > 0) {
            // wheel of fortune: coloured wedges, a peg per detent, a fixed pointer
            static const Color wedge[6] = { { 220, 70, 60, 255 }, { 240, 170, 60, 255 }, { 90, 190, 100, 255 },
                                            { 70, 150, 230, 255 }, { 160, 100, 220, 255 }, { 230, 230, 235, 255 } };
            int n = sp->detents > 0 ? sp->detents : 12;
            for (int i = 0; i < n; i++) {
                float a = (float)i / (float)n * 2.0f * PI;
                SfxrPose w = { sfxr_pose_apply(m.part, (Vector3){ cosf(a + PI / n) * R * 0.5f, -0.004f, sinf(a + PI / n) * R * 0.5f }),
                               QuaternionMultiply(m.part.orientation, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -(a + PI / n))) };
                vrui_box(w, (Vector3){ R * 0.95f, 0.006f, R * 2.0f * PI / (float)n * 0.5f }, vrui__hover_tint(wedge[i % 6], hd.hovered, hd.held));
                Vector3 peg = sfxr_pose_apply(m.part, (Vector3){ cosf(a) * R, 0, sinf(a) * R });
                vrui__cylinder(peg, Vector3Add(peg, Vector3Scale(axis, 0.015f)), 0.0035f, (Color){ 230, 230, 235, 255 });
            }
            vrui__ring(m.part, R, (Color){ 30, 30, 34, 255 });
            vrui__cylinder(bot, top, 0.012f, (Color){ 40, 42, 48, 255 });
            // the pointer stays put at the wheel's -Z edge (the far side)
            SfxrPose ptr = { sfxr_pose_apply(base, (Vector3){ 0, height + 0.012f, -R - 0.012f }), base.orientation };
            vrui_box(ptr, (Vector3){ 0.012f, 0.012f, 0.03f }, (Color){ 30, 30, 34, 255 });
        } else if (sp->endless) {
            // handwheel: rim, spokes and a handle on the rim
            vrui__ring(m.part, R, col);
            vrui__ring(m.part, R * 0.97f, col);
            for (int i = 0; i < 3; i++) {
                float a = (float)i * 2.0f * PI / 3.0f;
                Vector3 e = sfxr_pose_apply(m.part, (Vector3){ cosf(a) * R, 0, sinf(a) * R });
                vrui__cylinder(top, e, 0.004f, col);
            }
            vrui__cylinder(bot, top, 0.012f, col);
            Vector3 hb = sfxr_pose_apply(m.part, (Vector3){ R, 0, 0 });
            Vector3 ht = sfxr_pose_apply(m.part, (Vector3){ R, 0.05f, 0 });
            vrui__cylinder(hb, ht, 0.01f, vrui__hover_tint((Color){ 200, 170, 90, 255 }, hd.hovered, hd.held));
        } else {
            vrui__cylinder(bot, top, R, col);
            SfxrPose ind = { sfxr_pose_apply(m.part, (Vector3){ 0, 0.001f, -R * 0.55f }), m.part.orientation };
            vrui_box(ind, (Vector3){ R * 0.15f, 0.004f, R * 0.8f }, C.style.accent);
            // scale: one mark per detent, or 11 marks across the range
            int marks = sp->detents >= 2 ? sp->detents : 11;
            for (int i = 0; i < marks; i++) {
                float a = -((float)i / (float)(marks - 1)) * sp->travel;
                if (a < -2.0f * PI + 0.01f) break;
                Quaternion rq = QuaternionMultiply(base.orientation, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, a));
                Vector3 p0 = Vector3Add(bot, Vector3RotateByQuaternion((Vector3){ 0, 0, -R * 1.15f }, rq));
                Vector3 p1 = Vector3Add(bot, Vector3RotateByQuaternion((Vector3){ 0, 0, -R * (sp->detents >= 2 ? 1.5f : 1.35f) }, rq));
                vrui_line(p0, p1, sp->detents >= 2 && i == m.detent ? C.style.accent : C.style.text_dim);
            }
        }
        if (hd.held && have_spot) vrui_line(top, spot, C.style.accent);   // the "crank arm" you're turning it with
        vrui__label(base, (Vector3){ 0, height + 0.05f, 0 }, value_label(sp, *value));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Pivot: levers (swings on base X; handle up along +Y in the middle of its swing)
// ---------------------------------------------------------------------------

VruiMech vrui_pivot(VruiId id, SfxrPose base, const VruiMechSpec *sp, float *value)
{
    VruiItem *it = vrui__item(id);
    MechState *ms = mech_state(it);
    float L = sp->size, half_swing = sp->travel * 0.5f;
    Vector3 axis = Vector3RotateByQuaternion((Vector3){ 1, 0, 0 }, base.orientation);
    Vector3 pivot = sfxr_pose_apply(base, (Vector3){ 0, 0.02f, 0 });
    float t = range_of(sp) != 0 ? Clamp((*value - sp->min) / range_of(sp), 0, 1) : 0;
    float angle = half_swing - t * sp->travel;   // +: handle toward +Z
    Quaternion rot = QuaternionMultiply(base.orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, angle));
    Vector3 knob = Vector3Add(pivot, Vector3RotateByQuaternion((Vector3){ 0, L, 0 }, rot));
    const float knob_r = 0.03f;

    SfxrPose knob_pose = { knob, rot };
    Vector3 knob_half = { knob_r, knob_r, knob_r };
    float ray[2], prox[2];
    vrui__ray_box_all(knob_pose, knob_half, ray);
    vrui__prox_box_all(knob_pose, knob_half, prox);
    reach_limit(prox, sp->reach);
    VruiHandle hd = vrui__handle_update(id, it, ray, prox);
    mech_hint(id, sp);

    bool changed = false, have_spot = false;
    Vector3 spot = knob;
    if (hd.grabbed) model_grab(ms, sp, *value);
    if (hd.held) {
        // A lever is a rotary joint grabbed far from its axis: pure orbit.
        // Pushing it sideways (along the pivot axis) is projected away.
        float d = rotary_drive(it, ms, &hd, pivot, axis, L, false, &spot, &have_spot);
        d = break_in(ms, d, rotary_slop(sp, ms));
        changed = model_drive(ms, sp, value, -d * range_of(sp) / sp->travel, hd.hand, hd.mode != VRUI_MODE_HAND);
    } else {
        changed = model_free(ms, sp, value, hd.released);
    }

    VruiMech m = result_from(&hd, *value);
    if (hd.released) sfxr_event("value", "%s %.3f", vrui__who(id), *value);
    m.changed = changed;
    m.detent = nearest_detent(sp, *value);
    t = range_of(sp) != 0 ? Clamp((*value - sp->min) / range_of(sp), 0, 1) : 0;
    m.position = half_swing - t * sp->travel;
    m.part.orientation = QuaternionMultiply(base.orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, m.position));
    m.part.position = pivot;

    if (sp->draw) {
        Vector3 k = Vector3Add(pivot, Vector3RotateByQuaternion((Vector3){ 0, L, 0 }, m.part.orientation));
        SfxrPose housing = base;
        housing.position = sfxr_pose_apply(base, (Vector3){ 0, 0.012f, 0 });
        vrui_box(housing, (Vector3){ 0.06f, 0.024f, 0.12f }, (Color){ 55, 58, 66, 255 });
        vrui__cylinder(pivot, k, 0.008f, (Color){ 180, 184, 190, 255 });
        vrui__sphere(k, knob_r, vrui__hover_tint(sp->color, hd.hovered, hd.held));
        vrui__label(base, (Vector3){ 0, L + 0.1f, 0 }, value_label(sp, *value));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Linear: sliders, plungers (along base X, centered on base)
// ---------------------------------------------------------------------------

VruiMech vrui_linear(VruiId id, SfxrPose base, const VruiMechSpec *sp, float *value)
{
    VruiItem *it = vrui__item(id);
    MechState *ms = mech_state(it);
    float L = sp->size;
    Vector3 axis = Vector3RotateByQuaternion((Vector3){ 1, 0, 0 }, base.orientation);
    float t = range_of(sp) != 0 ? Clamp((*value - sp->min) / range_of(sp), 0, 1) : 0;
    SfxrPose hp = { sfxr_pose_apply(base, (Vector3){ (t - 0.5f) * L, 0.018f, 0 }), base.orientation };
    Vector3 hhalf = { 0.015f, 0.015f, 0.025f };
    float ray[2], prox[2];
    vrui__ray_box_all(hp, hhalf, ray);
    vrui__prox_box_all(hp, hhalf, prox);
    reach_limit(prox, sp->reach);
    VruiHandle hd = vrui__handle_update(id, it, ray, prox);
    mech_hint(id, sp);

    bool changed = false;
    if (hd.grabbed) model_grab(ms, sp, *value);
    if (hd.held) {
        // measured from a fixed point on the track (not the moving handle)
        Vector3 track = sfxr_pose_apply(base, (Vector3){ 0, 0.018f, 0 });
        float d = linear_drive(ms, &hd, track, axis);
        d = break_in(ms, d, sp->slop);
        changed = model_drive(ms, sp, value, d * range_of(sp) / sp->travel, hd.hand, hd.mode != VRUI_MODE_HAND);
    } else {
        changed = model_free(ms, sp, value, hd.released);
    }

    VruiMech m = result_from(&hd, *value);
    if (hd.released) sfxr_event("value", "%s %.3f", vrui__who(id), *value);
    m.changed = changed;
    m.detent = nearest_detent(sp, *value);
    t = range_of(sp) != 0 ? Clamp((*value - sp->min) / range_of(sp), 0, 1) : 0;
    m.position = (t - 0.5f) * L;
    m.part = (SfxrPose){ sfxr_pose_apply(base, (Vector3){ m.position, 0.018f, 0 }), base.orientation };

    if (sp->draw) {
        SfxrPose track = base;
        track.position = sfxr_pose_apply(base, (Vector3){ 0, 0.004f, 0 });
        vrui_box(track, (Vector3){ L + 0.03f, 0.008f, 0.02f }, (Color){ 45, 48, 55, 255 });
        SfxrPose fill = base;
        fill.position = sfxr_pose_apply(base, (Vector3){ (t - 1.0f) * L * 0.5f, 0.0085f, 0 });
        if (t > 0.001f) vrui_box(fill, (Vector3){ t * L, 0.002f, 0.008f }, C.style.accent);
        vrui_box(m.part, Vector3Scale(hhalf, 2), vrui__hover_tint(sp->color, hd.hovered, hd.held));
        vrui__label(base, (Vector3){ 0, 0.07f, 0 }, value_label(sp, *value));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Tilt: joysticks (stands along base +Y; tilt x toward +X, y toward -Z)
// ---------------------------------------------------------------------------

#define STICK_MAX_TILT (25.0f * DEG2RAD)

VruiMech vrui_tilt(VruiId id, SfxrPose base, const VruiMechSpec *sp, Vector2 *value)
{
    VruiItem *it = vrui__item(id);
    MechState *ms = mech_state(it);
    float H = sp->size;
    Vector3 pivot = sfxr_pose_apply(base, (Vector3){ 0, 0.015f, 0 });
    Quaternion tilt_q = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 0, 0, 1 }, -value->x * STICK_MAX_TILT),
                                           QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -value->y * STICK_MAX_TILT));
    Quaternion rot = QuaternionMultiply(base.orientation, tilt_q);
    Vector3 top = Vector3Add(pivot, Vector3RotateByQuaternion((Vector3){ 0, H, 0 }, rot));
    const float ball_r = 0.022f;
    SfxrPose ball = { top, rot };
    Vector3 bhalf = { ball_r, ball_r, ball_r };
    float ray[2], prox[2];
    vrui__ray_box_all(ball, bhalf, ray);
    vrui__prox_box_all(ball, bhalf, prox);
    reach_limit(prox, sp->reach);
    VruiHandle hd = vrui__handle_update(id, it, ray, prox);
    mech_hint(id, sp);

    bool changed = false;
    Vector3 up = Vector3RotateByQuaternion((Vector3){ 0, 1, 0 }, base.orientation);
    if (hd.held) {
        Vector3 p;
        // Only motion across the stick counts; pushing down on it doesn't.
        if (vrui__drag_point(hd.hand, hd.mode, top, up, &p)) {
            Vector3 l = sfxr_pose_apply_inv(base, p);
            if (hd.grabbed) {
                it->rel.position = l;   // where the hand took hold, in the base's frame
                ms->tilt_at_grab = *value;
                ms->flags = 0;
            }
            float dx = l.x - it->rel.position.x, dz = l.z - it->rel.position.z;
            float lift = sfxr_pose_apply_inv(base, sfxr_hand((SfxrHandId)hd.hand)->grip.position).y;
            if (hd.grabbed) ms->probe_prev = lift;
            dx += naive_since_grab(ms, lift);
            float mag = sqrtf(dx * dx + dz * dz);
            // radial dead zone on the hand motion (the break-in)
            float k = mag > sp->slop ? (mag - sp->slop) / mag : 0.0f;
            Vector2 raw = { ms->tilt_at_grab.x + dx * k / sp->travel, ms->tilt_at_grab.y - dz * k / sp->travel };
            float len = sqrtf(raw.x * raw.x + raw.y * raw.y);
            if (len >= 1.0f) {
                raw.x /= len; raw.y /= len;
                if (!(ms->flags & M_AT_MAX)) { ms->flags |= M_AT_MAX; pulse(hd.hand, sp->haptic_stop * 0.5f, 0.015f); }
            } else if (len < 0.95f) ms->flags &= ~M_AT_MAX;
            tension_hum(ms, sp, fminf(len, 1.0f), hd.hand);
            changed = raw.x != value->x || raw.y != value->y;
            *value = raw;
        }
    } else if (sp->spring && !SFXR_BREAK(vrui_mech_no_spring)) {
        float k = 1.0f - expf(-sfxr_dt() / 0.05f);
        Vector2 v = { value->x * (1 - k), value->y * (1 - k) };
        if (fabsf(v.x) < 1e-4f && fabsf(v.y) < 1e-4f) v = (Vector2){ 0, 0 };
        changed = v.x != value->x || v.y != value->y;
        *value = v;
    }

    VruiMech m = result_from(&hd, 0.0f);
    m.changed = changed;
    m.tilt = *value;
    tilt_q = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 0, 0, 1 }, -value->x * STICK_MAX_TILT),
                                QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -value->y * STICK_MAX_TILT));
    m.part = (SfxrPose){ pivot, QuaternionMultiply(base.orientation, tilt_q) };

    if (sp->draw) {
        Vector3 t2 = Vector3Add(pivot, Vector3RotateByQuaternion((Vector3){ 0, H, 0 }, m.part.orientation));
        SfxrPose boot = base;
        boot.position = sfxr_pose_apply(base, (Vector3){ 0, 0.008f, 0 });
        vrui_box(boot, (Vector3){ 0.07f, 0.016f, 0.07f }, (Color){ 55, 58, 66, 255 });
        vrui__cylinder(pivot, t2, 0.009f, sp->color);
        vrui__sphere(t2, ball_r, vrui__hover_tint((Color){ 200, 50, 50, 255 }, hd.hovered, hd.held));
        if (sp->label)
            vrui__label(base, (Vector3){ 0, H + 0.08f, 0 }, TextFormat("%s %+.2f %+.2f", sp->label, value->x, value->y));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Short forms: named after the thing, default behavior
// ---------------------------------------------------------------------------

bool vrui_knob(VruiId id, SfxrPose base, float radius, float *value, float min, float max, float turns, const char *label)
{
    VruiMechSpec s = vrui_knob_spec();
    s.size = radius;
    s.min = min;
    s.max = max;
    s.travel = turns * 2.0f * PI;
    s.label = label;
    return vrui_rotary(id, base, &s, value).changed;
}

bool vrui_lever(VruiId id, SfxrPose base, float length, float *value, const char *label)
{
    VruiMechSpec s = vrui_lever_spec();
    s.size = length;
    s.label = label;
    return vrui_pivot(id, base, &s, value).changed;
}

bool vrui_slider3d(VruiId id, SfxrPose base, float length, float *t, const char *label)
{
    VruiMechSpec s = vrui_slider_spec();
    s.size = length;
    s.travel = length;
    s.label = label;
    return vrui_linear(id, base, &s, t).changed;
}

bool vrui_joystick(VruiId id, SfxrPose base, Vector2 *value, const char *label)
{
    VruiMechSpec s = vrui_joystick_spec();
    s.label = label;
    return vrui_tilt(id, base, &s, value).changed;
}
