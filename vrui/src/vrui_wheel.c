// vrui_wheel.c - the two-handed wheel (vrui_valve): a big handwheel that
// needs both hands on its rim to turn (docs/MECHANISMS.md, "Valve").
//
// Every other mechanism has one holder, so this one keeps its own per-hand
// holds instead of vrui__handle_update: each hand takes the rim wherever it
// is, independently, and lets go independently.
//
// How it turns: every frame, each holding hand's angle round the axis is
// compared with last frame's (only the swing round the axis counts: pushing,
// pulling or lifting the rim does nothing). The wheel moves by the AVERAGE of
// the holding hands' swings. With both hands going the same way it turns with
// them; with one hand pushing and the other pulling it still turns like a
// steering wheel; and one hand slipping can't spin it on its own. With fewer
// hands than spec.hands it moves at spec.one_hand of the swing (0: not at
// all), and strains: a rough hum that grows with how hard you're trying.

#include "vrui_internal.h"

typedef struct {
    bool  hold[2];
    float last[2];      // each holding hand's angle round the axis last frame (rad)
    float slop_left;    // break-in still to absorb since the last grab (rad)
    int   last_tick;    // detent index last frame
    bool  at_stop;
} ValveState;
VRUI_STATE_FITS(ValveState);

#define WHEEL_HEIGHT 0.12f   // the wheel stands this far off its mount

static float wrap_pi(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

// Angle of a point round the wheel's axis (clockwise looking down +Y, 0 at
// -Z, like every rotary), and how far it is from the axis.
static float angle_round(SfxrPose plane, Vector3 p, float *radial, float *height)
{
    Vector3 l = sfxr_pose_apply_inv(plane, p);
    *radial = sqrtf(l.x * l.x + l.z * l.z);
    *height = l.y;
    return atan2f(l.x, -l.z);
}

VruiMech vrui_valve(VruiId id, SfxrPose base, const VruiMechSpec *sp, float *value)
{
    VruiMech m = { 0 };
    m.hand = SFXR_RIGHT;
    VruiItem *it = vrui__item(id);
    vrui__name(id, sp->label);
    ValveState *vs = VRUI_STATE(it, ValveState);
    const float R = sp->size, range = sp->max - sp->min, dt = sfxr_dt();
    SfxrPose plane = sfxr_pose_mul(base, (SfxrPose){ { 0, WHEEL_HEIGHT, 0 }, QuaternionIdentity() });
    vrui__hint(id, sp->hands >= 2 ? "grab the rim with BOTH hands, turn it like a wheel" : "grab the rim, turn it");

    // --- holds, one per hand
    float ang[2] = { 0 }, radial[2] = { 0 }, hgt;
    bool fresh[2] = { false, false };
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        ang[h] = angle_round(plane, hand->grip.position, &radial[h], &hgt);
        float rim = sqrtf((radial[h] - R) * (radial[h] - R) + hgt * hgt);   // distance to the rim
        if (vs->hold[h]) {
            if (hand->active && vrui__grab_down(h)) { vrui__grab_offer(h, id, 0); continue; }
            vs->hold[h] = false;
            if (C.grab_active[h] == id) C.grab_active[h] = VRUI_ID_NONE;
            sfxr_event("release", "%s %s", vrui__who(id), h ? "R" : "L");
            m.released = true;
            m.hand = (SfxrHandId)h;
            continue;
        }
        if (!hand->active) continue;
        if (rim < sp->reach) vrui__grab_offer(h, id, rim);
        if (!vrui__grab_hot(h, id)) continue;
        m.hovered = true;
        if (vrui__grab_pressed(h) && vrui__hand_free(h)) {
            vs->hold[h] = true;
            vs->last[h] = ang[h];
            vs->slop_left = sp->slop;
            fresh[h] = true;
            C.grab_active[h] = id;
            sfxr_event("grab", "%s %s hand", vrui__who(id), h ? "R" : "L");
            vrui__click_pulse(h);
            m.grabbed = true;
            m.hand = (SfxrHandId)h;
        }
    }

    // --- how far the hands swung round the axis this frame
    int n = 0;
    float sum = 0;
    for (int h = 0; h < 2; h++) {
        if (!vs->hold[h]) continue;
        n++;
        m.hand = (SfxrHandId)h;
        // near the hub the angle means nothing: a hand there adds no swing
        float d = fresh[h] || radial[h] < 0.3f * R ? 0.0f : wrap_pi(ang[h] - vs->last[h]);
        vs->last[h] = ang[h];
        sum += d;
    }
    float swing = 0, strain = 0;
    if (n > 0) {
        float avg = SFXR_BREAK(vrui_valve_sum_not_average) ? sum : sum / (float)n;
        bool enough = n >= sp->hands || SFXR_BREAK(vrui_valve_one_hand_turns);
        swing = enough ? avg : avg * sp->one_hand;
        if (!enough) strain = fabsf(avg) * (1.0f - sp->one_hand);
    }
    if (vs->slop_left > 0 && swing != 0) {   // break-in after each grab
        float take = fminf(fabsf(swing), vs->slop_left);
        vs->slop_left -= take;
        swing -= copysignf(take, swing);
    }
    float lim = sp->max_speed * dt;
    if (sp->max_speed > 0 && fabsf(swing) > lim) {   // too fast: the rest slips
        strain += fabsf(swing) - lim;
        swing = copysignf(lim, swing);
    }

    // --- the value, end stops, ticks
    float before = *value;
    float v = *value + (sp->travel > 0 ? swing / sp->travel * range : 0);
    bool stop = false;
    if (v > sp->max) { v = sp->max; stop = swing > 0; }
    if (v < sp->min) { v = sp->min; stop = swing < 0; }
    *value = v;
    float step = sp->detents >= 2 ? range / (float)(sp->detents - 1) : 0;
    int tick = step > 0 ? (int)floorf((v - sp->min) / step + 0.5f) : -1;
    for (int h = 0; h < 2; h++) {
        if (!vs->hold[h]) continue;
        if (tick != vs->last_tick && tick >= 0 && sp->haptic_tick > 0) vrui_haptic_pulse((SfxrHandId)h, sp->haptic_tick, 0.012f, 0);
        if (stop && !vs->at_stop && sp->haptic_stop > 0) vrui_haptic_pulse((SfxrHandId)h, sp->haptic_stop, 0.04f, 0);
        float effort = dt > 0 ? strain / dt : 0;   // rad/s you're trying to move it by
        if (effort > 0.05f && sp->haptic_strain > 0)
            vrui_haptic_hum((SfxrHandId)h, sp->haptic_strain * Clamp(0.3f + effort / 3.0f, 0, 1), 60.0f);
    }
    vs->last_tick = tick;
    vs->at_stop = stop || (vs->at_stop && (v <= sp->min || v >= sp->max));
    if (v != before) m.changed = true;
    if (m.released && !vs->hold[0] && !vs->hold[1]) sfxr_event("value", "%s = %.3f", vrui__who(id), v);

    m.holders = n;
    m.held = n > 0;
    m.value = v;
    m.detent = tick;
    m.position = -(v - sp->min) / (range != 0 ? range : 1) * sp->travel;
    m.part = sfxr_pose_mul(plane, (SfxrPose){ { 0 }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, m.position) });

    // Holding it with one hand when it needs two: say so, next to the wheel.
    if (n > 0 && n < sp->hands && sp->one_hand <= 0)
        vrui_tag(sfxr_pose_apply(plane, (Vector3){ 0, 0.06f, 0 }), "won't budge: use both hands", 0.018f, RAYWHITE,
                 (Color){ 120, 30, 30, 220 });

    if (sp->draw) {
        Color col = vrui__hover_tint(sp->color, m.hovered, m.held);
        const int SEG = 20;
        for (int i = 0; i < SEG; i++) {   // the rim, as a ring of short thick segments
            float a0 = (float)i / SEG * 2.0f * PI, a1 = (float)(i + 1) / SEG * 2.0f * PI;
            vrui__cylinder(sfxr_pose_apply(m.part, (Vector3){ R * sinf(a0), 0, -R * cosf(a0) }),
                           sfxr_pose_apply(m.part, (Vector3){ R * sinf(a1), 0, -R * cosf(a1) }), 0.014f, col);
        }
        for (int i = 0; i < 4; i++) {     // spokes (diagonal, leaving room for the words)
            float a = (float)i * PI * 0.5f + PI * 0.25f;
            vrui__cylinder(m.part.position, sfxr_pose_apply(m.part, (Vector3){ R * sinf(a), 0, -R * cosf(a) }), 0.008f, col);
        }
        // Needs both hands: say so before anyone tries. Two hand-sized grip
        // pads on opposite sides of the rim (they don't turn: they show where
        // hands go), amber until a hand holds that side, then green; and the
        // words across the wheel's face.
        if (sp->hands >= 2) {
            bool side[2] = { false, false };   // 0: -X, 1: +X
            for (int h = 0; h < 2; h++) {
                if (!vs->hold[h]) continue;
                float x = sfxr_pose_apply_inv(plane, sfxr_hand((SfxrHandId)h)->grip.position).x;
                side[x > 0] = true;
            }
            for (int k = 0; k < 2; k++) {
                SfxrPose pad = sfxr_pose_mul(plane, (SfxrPose){ { k ? R : -R, 0, 0 }, QuaternionIdentity() });
                vrui_box(pad, (Vector3){ 0.035f, 0.035f, 0.1f }, side[k] ? (Color){ 90, 220, 110, 230 } : (Color){ 245, 180, 50, 230 });
            }
            SfxrPose face = sfxr_pose_mul(plane, (SfxrPose){ { 0, 0.02f, 0 }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -PI / 2) });
            int n_on = (int)side[0] + (int)side[1];
            vrui_text_at(sfxr_pose_mul(face, (SfxrPose){ { 0, R * 0.5f, 0 }, QuaternionIdentity() }), "BOTH", R * 0.16f, RAYWHITE);
            vrui_text_at(sfxr_pose_mul(face, (SfxrPose){ { 0, -R * 0.5f, 0 }, QuaternionIdentity() }), n_on == 1 ? "HANDS (1 of 2)" : "HANDS",
                         R * 0.16f, RAYWHITE);
        }
        vrui__cylinder(base.position, m.part.position, 0.02f, (Color){ 70, 72, 80, 255 });   // the stem
        vrui__sphere(m.part.position, 0.03f, (Color){ 70, 72, 80, 255 });
        if (sp->label) {
            const char *txt = sp->value_format ? TextFormat(sp->value_format, sp->label, v * sp->display_scale) : sp->label;
            vrui__label(plane, (Vector3){ 0, 0.05f, R + 0.05f }, txt);
        }
    }
    vrui__report(id, "valve", base, m.part, v, true);
    return m;
}
