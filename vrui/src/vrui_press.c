// vrui_press.c - things you press: push buttons (vrui_press) and rocker
// switches (vrui_rocker), poked with a fingertip / controller tip or pressed
// with the laser, plus their short forms (vrui_push_button, vrui_switch).

#include "vrui_internal.h"

// ---------------------------------------------------------------------------
// Press: push buttons
// ---------------------------------------------------------------------------

typedef struct {
    bool     down;
    unsigned armed;   // bit h: hand h's tip arrived over the cap from above
    float    depth;   // eased visual depth (m)
} PressState;
VRUI_STATE_FITS(PressState);

#define CAP_H 0.02f

VruiPressSpec vrui_press_spec(void)
{
    VruiPressSpec s = {0};
    s.radius = 0.035f;
    s.travel = 0.012f;
    s.press_at = 0.6f;
    s.release_at = 0.3f;
    s.slide = 2.0f;
    s.draw = true;
    s.color = (Color){ 220, 60, 50, 255 };
    return s;
}

VruiPress vrui_press(VruiId id, SfxrPose base, const VruiPressSpec *sp, bool *latched)
{
    PressState *st = VRUI_STATE(vrui__item(id), PressState);
    VruiPress res = {0};
    float depth = 0.0f;
    bool was = st->down;

    // Poke with the tip. A hand arms by bringing its tip over the cap from
    // above; only an armed hand presses. So brushing the button from the side
    // does nothing, while a press that drifts sideways (it always does) keeps
    // going until the tip is `slide` radii from the center.
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        bool armed = (st->armed >> h) & 1;
        if (!hand->active || !vrui__hand_free(h)) { armed = false; }
        else {
            Vector3 l = sfxr_pose_apply_inv(base, vrui__tip(h));
            float r = sqrtf(l.x * l.x + l.z * l.z);
            bool above = l.y > CAP_H - 0.002f && l.y < CAP_H + 0.03f;
            if (SFXR_BREAK(vrui_button_side_entry)) above = l.y < CAP_H + 0.03f;
            // point-to-press: only a pointing finger (bare hands: the index tip is the poke point anyway)
            bool shape_ok = !sp->require_point || SFXR_BREAK(vrui_button_ignores_shape) ||
                            hand->shape == SFXR_SHAPE_POINT || hand->source == SFXR_SOURCE_HAND;
            if (!armed && above && shape_ok && r < sp->radius * 1.1f) armed = true;
            float keep_r = sp->radius * (was && !SFXR_BREAK(vrui_button_no_slide) ? sp->slide : 1.3f);
            if (armed && (r > keep_r || l.y > CAP_H + 0.04f || l.y < -0.03f)) armed = false;
            if (armed) {
                float d = Clamp(CAP_H - l.y, 0, sp->travel);
                if (d > depth) depth = d;
                if (l.y < CAP_H + 0.01f) vrui__grab_offer(h, id, 0.01f);   // hide the laser while touching
            }
        }
        st->armed = (st->armed & ~(1u << h)) | ((armed ? 1u : 0u) << h);
    }
    bool poke_down = depth > sp->travel * (was ? sp->release_at : sp->press_at);

    // Laser + trigger (at the current pull level).
    SfxrPose cap_pose = base;
    cap_pose.position = sfxr_pose_apply(base, (Vector3){ 0, CAP_H * 0.5f, 0 });
    Vector3 cap_half = { sp->radius, CAP_H * 0.5f, sp->radius };
    bool ray_down = false;
    for (int h = 0; h < 2; h++) {
        if (!sfxr_hand((SfxrHandId)h)->active) continue;
        float t = vrui__ray_box(vrui__hand_ray(h), cap_pose, cap_half);
        if (t >= 0) vrui__ray_offer(h, id, t);
        if (vrui__ray_hot(h, id)) {
            res.hovered = true;
            if (vrui__trigger(h)->pressed) C.ray_active[h] = id;
            if (C.ray_active[h] == id) {
                if (vrui__trigger(h)->down) ray_down = true;
                else C.ray_active[h] = VRUI_ID_NONE;
            }
        }
        if (vrui__grab_hot(h, id)) res.hovered = true;
    }
    if (ray_down) depth = sp->travel;

    res.down = poke_down || ray_down;
    res.pressed = res.down && !was;
    res.released = !res.down && was;
    st->down = res.down;
    if (res.pressed) {
        for (int h = 0; h < 2; h++) if (vrui__grab_hot(h, id) || vrui__ray_hot(h, id)) vrui__click_pulse(h);
        if (sp->latching && latched) { *latched = !*latched; res.changed = true; }
    }
    // Latching buttons rest a little lower while latched.
    float rest = (sp->latching && latched && *latched) ? sp->travel * 0.5f : 0.0f;
    float target = fmaxf(depth, rest);
    st->depth += (target - st->depth) * 0.5f;
    res.depth = sp->travel > 0 ? st->depth / sp->travel : 0;
    res.part = base;
    res.part.position = sfxr_pose_apply(base, (Vector3){ 0, CAP_H - st->depth, 0 });

    if (sp->draw) {
        SfxrPose plate = base;
        plate.position = sfxr_pose_apply(base, (Vector3){ 0, 0.003f, 0 });
        vrui_box(plate, (Vector3){ sp->radius * 2.6f, 0.006f, sp->radius * 2.6f }, (Color){ 50, 52, 58, 255 });
        Vector3 a = sfxr_pose_apply(base, (Vector3){ 0, 0.004f, 0 });
        Color col = res.down || (sp->latching && latched && *latched) ? ColorBrightness(sp->color, 0.3f) : sp->color;
        vrui__cylinder(a, res.part.position, sp->radius, vrui__hover_tint(col, res.hovered, false));
        vrui__label(base, (Vector3){ 0, CAP_H + 0.04f, 0 }, sp->label);
    }
    return res;
}

// ---------------------------------------------------------------------------
// Rocker: toggle switches
// ---------------------------------------------------------------------------

typedef struct {
    unsigned in;   // bit h: hand h's tip flipped it and hasn't backed out past exit_margin yet
} RockerState;
VRUI_STATE_FITS(RockerState);

VruiRockerSpec vrui_rocker_spec(void)
{
    VruiRockerSpec s = {0};
    s.half = (Vector3){ 0.02f, 0.012f, 0.035f };
    s.exit_margin = 0.015f;
    s.draw = true;
    return s;
}

VruiPress vrui_rocker(VruiId id, SfxrPose base, const VruiRockerSpec *sp, bool *on)
{
    RockerState *st = VRUI_STATE(vrui__item(id), RockerState);
    VruiPress res = {0};
    SfxrPose body = base;
    body.position = sfxr_pose_apply(base, (Vector3){ 0, sp->half.y, 0 });
    Vector3 poke_half = { sp->half.x, sp->half.y + 0.01f, sp->half.z };

    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        bool in = (st->in >> h) & 1;
        if (!hand->active || vrui_hand_busy((SfxrHandId)h)) in = false;
        else {
            // Poke: entering the switch flips it once. Leaving needs the tip to
            // back out by exit_margin, so a finger jittering on the edge can't
            // flip it back and forth.
            float dist = vrui__point_box_dist(vrui__tip(h), body, poke_half);
            if (dist < 0.02f) { vrui__grab_offer(h, id, 0.01f); res.hovered = true; }
            if (!in && dist < 0.001f) {
                in = true;
                *on = !*on; res.changed = res.pressed = true;
                vrui__click_pulse(h);
            } else if (in && dist > (SFXR_BREAK(vrui_switch_no_exit_margin) ? 0.001f : sp->exit_margin)) {
                in = false;
            }
        }
        st->in = (st->in & ~(1u << h)) | ((in ? 1u : 0u) << h);

        float t = vrui__ray_box(vrui__hand_ray(h), body, sp->half);
        if (t >= 0) vrui__ray_offer(h, id, t);
        if (vrui__ray_hot(h, id)) {
            res.hovered = true;
            if (vrui__trigger(h)->pressed) { *on = !*on; res.changed = res.pressed = true; vrui__click_pulse(h); }
        }
    }
    res.down = st->in != 0;

    SfxrPose rocker = body;
    float tilt = (*on ? -1.0f : 1.0f) * 12.0f * DEG2RAD;
    rocker.orientation = QuaternionMultiply(base.orientation, QuaternionFromAxisAngle((Vector3){1, 0, 0}, tilt));
    res.part = rocker;
    res.depth = *on ? 1.0f : 0.0f;
    if (sp->draw) {
        SfxrPose plate = base;
        plate.position = sfxr_pose_apply(base, (Vector3){ 0, 0.002f, 0 });
        vrui_box(plate, (Vector3){ sp->half.x * 2.8f, 0.004f, sp->half.z * 2.4f }, (Color){ 50, 52, 58, 255 });
        Color col = *on ? (Color){ 70, 200, 110, 255 } : (Color){ 150, 70, 70, 255 };
        vrui_box(rocker, Vector3Scale(sp->half, 2), vrui__hover_tint(col, res.hovered, false));
        vrui__label(base, (Vector3){ 0, 0.06f, 0 }, sp->label ? TextFormat("%s: %s", sp->label, *on ? "ON" : "OFF") : NULL);
    }
    return res;
}

// ---------------------------------------------------------------------------
// Short forms
// ---------------------------------------------------------------------------

bool vrui_push_button(VruiId id, SfxrPose base, float radius, Color color, const char *label)
{
    VruiPressSpec s = vrui_press_spec();
    s.radius = radius;
    s.color = color;
    s.label = label;
    return vrui_press(id, base, &s, NULL).pressed;
}

bool vrui_switch(VruiId id, SfxrPose base, bool *on, const char *label)
{
    VruiRockerSpec s = vrui_rocker_spec();
    s.label = label;
    return vrui_rocker(id, base, &s, on).changed;
}
