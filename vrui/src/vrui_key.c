// vrui_key.c - the key switch (vrui_key_switch): a key you carry to its slot,
// push in, and turn (docs/MECHANISMS.md, "Key switch").
//
// One hold does it all, the way you'd use a real key: pick it up, bring its
// tip to the slot roughly lined up with it (it's pulled in the last few
// centimeters, like a magnet), then twist your wrist to turn it, then (back at
// the first stop) pull it straight out. Letting go halfway never loses it:
// out of the slot it stays where you let go; in the slot it settles on the
// nearest stop (or springs back from the last one, like an ignition).
//
// The key's pose: origin at the tip of the blade, +Y along the key toward
// its bow (the part you hold). In the slot, the tip sits 3.5 cm deep.

#include "vrui_internal.h"

typedef struct {
    int   holder;        // holding hand + 1 (0: nobody)
    bool  in;            // in the slot
    SfxrPose rel;        // out of the slot: the key in hand space
    float angle;         // in the slot: turned this far clockwise from stop 0 (rad)
    Vector3 xref;        // twist reference: the hand's X axis last frame, in the slot's frame
    float out_ref;       // the hand's height above the slot when it last settled (for pulling out)
    int   last_stop;
    bool  armed;         // out of the slot and far enough away to go in again
} KeyState;
VRUI_STATE_FITS(KeyState);

#define BLADE  0.045f
#define DEPTH  0.035f
#define BOW_H  0.03f

VruiKeySpec vrui_key_spec(int positions)
{
    VruiKeySpec s = { 0 };
    s.positions = positions >= 2 ? positions : 2;
    s.step_deg = 45.0f;
    s.capture = 0.03f;
    s.align_deg = 30.0f;
    s.pull_out = 0.04f;
    s.draw = true;
    s.color = (Color){ 214, 180, 90, 255 };
    return s;
}

// Signed angle (rad) from a to b round +Y, CLOCKWISE looking down +Y
// positive, like every rotary in vrui.
static float clockwise(Vector3 a, Vector3 b)
{
    return -atan2f(a.z * b.x - a.x * b.z, a.x * b.x + a.z * b.z);
}

static Vector3 flat(Vector3 v) { v.y = 0; float l = Vector3Length(v); return l > 1e-5f ? Vector3Scale(v, 1.0f / l) : (Vector3){ 1, 0, 0 }; }

static SfxrPose key_in_slot(SfxrPose slot, float angle)
{
    return sfxr_pose_mul(slot, (SfxrPose){ { 0, -DEPTH, 0 }, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -angle) });
}

// The hand's X axis, seen in the slot's frame (its twist round the slot's axis).
static Vector3 hand_x(SfxrPose slot, int h)
{
    Quaternion inv = QuaternionInvert(slot.orientation);
    return flat(Vector3RotateByQuaternion(sfxr_pose_right(sfxr_hand((SfxrHandId)h)->grip), inv));
}

VruiKey vrui_key_switch(VruiId id, SfxrPose slot, const VruiKeySpec *sp, SfxrPose *key, int *position)
{
    VruiKey r = { 0 };
    r.hand = SFXR_RIGHT;
    VruiItem *it = vrui__item(id);
    vrui__name(id, sp->label);
    KeyState *ks = VRUI_STATE(it, KeyState);
    const float step = sp->step_deg * DEG2RAD;
    const float top = (float)(sp->positions - 1) * step;
    vrui__hint(id, ks->in ? "grab the key, twist your wrist to turn; pull out at the first stop"
                          : "grab the key, push it into the slot");

    // --- taking hold of the key's bow
    SfxrPose bow = sfxr_pose_mul(*key, (SfxrPose){ { 0, BLADE + BOW_H * 0.5f, 0 }, QuaternionIdentity() });
    Vector3 bow_half = { 0.018f, BOW_H * 0.5f + 0.01f, 0.012f };
    if (ks->holder > 0) {
        int hh = ks->holder - 1;
        const SfxrHand *hand = sfxr_hand((SfxrHandId)hh);
        if (!hand->active || !vrui__grab_down(hh)) {
            if (C.grab_active[hh] == id) C.grab_active[hh] = VRUI_ID_NONE;
            sfxr_event("release", "%s %s", vrui__who(id), hh ? "R" : "L");
            r.released = true;
            r.hand = (SfxrHandId)hh;
            ks->holder = 0;
        } else {
            vrui__grab_offer(hh, id, 0);
        }
    } else {
        for (int h = 0; h < 2; h++) {
            const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
            if (!hand->active) continue;
            float d = vrui__point_box_dist(hand->grip.position, bow, bow_half);
            if (d < 0.05f) vrui__grab_offer(h, id, d);
            if (!vrui__grab_hot(h, id)) continue;
            r.hovered = true;
            if (vrui__grab_pressed(h) && vrui__hand_free(h)) {
                ks->holder = h + 1;
                ks->rel = sfxr_pose_relative(hand->grip, *key);
                ks->xref = hand_x(slot, h);
                ks->out_ref = sfxr_pose_apply_inv(slot, hand->grip.position).y;
                C.grab_active[h] = id;
                sfxr_event("grab", "%s %s hand", vrui__who(id), h ? "R" : "L");
                vrui__click_pulse(h);
                r.grabbed = true;
                r.hand = (SfxrHandId)h;
                break;
            }
        }
    }

    int h = ks->holder - 1;
    if (!ks->in) {
        if (h >= 0) {
            *key = sfxr_pose_mul(sfxr_hand((SfxrHandId)h)->grip, ks->rel);   // in your hand
            // Close to the slot and roughly lined up: it goes in. Just after
            // pulling it out the tip is still at the slot, so it only goes
            // in again once it has been well clear (armed).
            float dist = Vector3Distance(key->position, slot.position);
            float tilt = Vector3Angle(sfxr_pose_up(*key), sfxr_pose_up(slot)) * RAD2DEG;
            if (dist > sp->capture + 0.03f) ks->armed = true;
            if (ks->armed && dist < sp->capture && (tilt < sp->align_deg || SFXR_BREAK(vrui_key_any_angle))) {
                ks->in = true;
                ks->angle = 0;
                ks->xref = hand_x(slot, h);
                ks->out_ref = sfxr_pose_apply_inv(slot, sfxr_hand((SfxrHandId)h)->grip.position).y;
                r.inserted_now = true;
                vrui_haptic_pulse((SfxrHandId)h, 0.6f, 0.03f, 0);
                sfxr_event("insert", "%s", vrui__who(id));
                if (SFXR_BREAK(vrui_key_needs_regrip)) {   // the tempting shortcut: end the hold on insertion
                    if (C.grab_active[h] == id) C.grab_active[h] = VRUI_ID_NONE;
                    ks->holder = 0;
                    h = -1;
                }
            }
        }
    }
    if (ks->in) {
        if (h >= 0) {
            const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
            // Twist: only the wrist's turn round the slot's axis counts.
            Vector3 x = hand_x(slot, h);
            float a = ks->angle + clockwise(ks->xref, x);
            ks->xref = x;
            if (a < 0 || a > top) {
                if (a > top) a = top;
                if (a < 0) a = 0;
            }
            ks->angle = a;
            // At the first stop, pulling straight back takes it out.
            float out = sfxr_pose_apply_inv(slot, hand->grip.position).y;
            if (ks->angle > step * 0.25f) ks->out_ref = fminf(ks->out_ref, out);   // turned: the key is locked in
            else if (out - ks->out_ref > sp->pull_out && !SFXR_BREAK(vrui_key_stuck)) {
                ks->in = false;
                ks->armed = false;
                ks->rel = sfxr_pose_relative(hand->grip, key_in_slot(slot, 0));
                r.removed_now = true;
                vrui_haptic_pulse((SfxrHandId)h, 0.4f, 0.02f, 0);
                sfxr_event("remove", "%s", vrui__who(id));
            }
            if (out < ks->out_ref) ks->out_ref = out;   // pushing in resets the reference
        } else {
            // let go: settle on the nearest stop; the last one (if sprung) springs back
            float target = floorf(ks->angle / step + 0.5f) * step;
            if (sp->spring_last && target >= top - 1e-4f && sp->positions > 2 && !SFXR_BREAK(vrui_mech_no_spring)) target = top - step;
            float k = 1.0f - expf(-sfxr_dt() * 20.0f);
            ks->angle += (target - ks->angle) * k;
        }
        if (ks->in) *key = key_in_slot(slot, ks->angle);
    }

    // --- the stop it's at, ticks and stops
    int stop = ks->in ? (int)floorf(ks->angle / step + 0.5f) : 0;
    if (stop < 0) stop = 0;
    if (stop > sp->positions - 1) stop = sp->positions - 1;
    if (stop != ks->last_stop) {
        if (h >= 0) vrui_haptic_pulse((SfxrHandId)h, 0.3f, 0.015f, 0);
        sfxr_event("turn", "%s -> %d%s%s", vrui__who(id), stop, sp->names ? " " : "", sp->names ? sp->names[stop] : "");
        ks->last_stop = stop;
    }
    if (position && *position != stop) { *position = stop; r.changed = true; }

    r.held = ks->holder > 0;
    if (r.held) r.hand = (SfxrHandId)(ks->holder - 1);
    r.inserted = ks->in;
    r.position = stop;
    r.key = *key;

    if (sp->draw) {
        // the slot: a round plate with a dark keyway, the stop names round it
        vrui__cylinder(slot.position, sfxr_pose_apply(slot, (Vector3){ 0, 0.008f, 0 }), 0.035f, (Color){ 150, 150, 160, 255 });
        SfxrPose way = sfxr_pose_mul(slot, (SfxrPose){ { 0, 0.0085f, 0 }, QuaternionIdentity() });
        vrui_box(way, (Vector3){ 0.004f, 0.001f, 0.02f }, (Color){ 20, 20, 24, 255 });
        for (int i = 0; sp->names && i < sp->positions; i++) {
            float a = (float)i * step;
            // printed on the plate, round the keyway: stop 0 straight up the plate (-Z), then clockwise
            SfxrPose tp = sfxr_pose_mul(slot, (SfxrPose){ { 0.05f * sinf(a), 0.009f, -0.05f * cosf(a) },
                                                         QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -PI / 2) });
            vrui_text_at(tp, sp->names[i], 0.012f, i == stop && ks->in ? C.style.accent : C.style.text);
        }
        // the key: blade and bow
        Color kc = vrui__hover_tint(sp->color, r.hovered, r.held);
        SfxrPose blade = sfxr_pose_mul(*key, (SfxrPose){ { 0, BLADE * 0.5f, 0 }, QuaternionIdentity() });
        vrui_box(blade, (Vector3){ 0.004f, BLADE, 0.009f }, kc);
        vrui_box(bow, (Vector3){ 0.03f, BOW_H, 0.006f }, kc);
        if (sp->label) vrui__label(slot, (Vector3){ 0, 0.02f, 0.06f }, sp->label);
    }
    return r;
}
