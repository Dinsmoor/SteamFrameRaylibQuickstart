// vrui_wield.c - things held by their handles, with weight (vrui.h section
// 14, docs/WIELDING.md). Modeled on Blade & Sorcery's wielding.
//
// Each frame:
//   1. hands let go (a sticky grip only when fully open) and take hold (a
//      hand near a handle and the grab button; or laser + grip pulls it in)
//   2. the hands say where it should be (the TARGET): one hand holds it by
//      the relation fixed when it took hold -- handle along the fist, face
//      toward the knuckles, snapped to the allowed ways round; a second hand
//      on the handle steers its axis; a loosened sticky grip slides along
//   3. the thing follows the target through springs set by its weight, or,
//      held by nobody, falls, bounces and settles (loose)

#include "vrui_internal.h"

#include <string.h>

// One hand's hold (kept in a per-hand sub-item of the widget).
typedef struct {
    bool       on;
    int        grip;         // which handle (-1: no handles, held as taken)
    float      t;            // where along it, 0 (a) .. 1 (b)
    Quaternion rel;          // the thing's rotation in the hand's frame
    Vector3    rel_pos;      // (no handles) its position in the hand's frame
    Vector3    last_hand;    // the hand's position last frame (sliding)
    bool       sliding, at_end;
} HandHold;
VRUI_STATE_FITS(HandHold);

typedef struct {
    int      main;           // the hand in charge, -1 none
    Vector3  vel, ang;       // the springs' velocities while held; motion while loose
    Vector3  v_est, w_est;   // measured from the shown pose (the throw)
    SfxrPose prev;
    bool     have_prev, loose;
    int      fly_hand;       // laser pull: flying to this hand (-1 none)
    float    fly_t;
    SfxrPose fly_from;
    float    rest_y;         // (lift_hands) the height it was lying at
} WieldState;
VRUI_STATE_FITS(WieldState);

static const char *const WEIGHT_NAMES[VRUI_WEIGHT_COUNT] = { "feather", "light", "medium", "heavy", "huge" };
const char *vrui_weight_name(VruiWeight w) { return (unsigned)w < VRUI_WEIGHT_COUNT ? WEIGHT_NAMES[w] : "?"; }

VruiWieldSpec vrui_wield_spec(VruiWeight weight)
{
    VruiWieldSpec s = {0};
    s.half = (Vector3){ 0.05f, 0.05f, 0.05f };
    s.reach = 0.08f;
    s.two_handed = true;
    s.lift_hands = 1;
    s.pull = true;
    s.gravity = 9.8f;
    switch (weight) {
    case VRUI_WEIGHT_FEATHER:   // on your hand, and drifts down when dropped
        s.mass = 0.02f; s.drag = 4.0f; s.bounce = 0.1f; s.max_throw = 4.0f; s.two_handed = false; break;
    case VRUI_WEIGHT_LIGHT:     // a ball, a dagger: a touch of weight, throws far
        s.mass = 0.3f; s.drag = 0.05f; s.bounce = 0.5f; s.max_throw = 14.0f; s.two_handed = false; break;
    case VRUI_WEIGHT_MEDIUM:    // a sword, a brick: follows well, swings through a little
        s.mass = 1.5f; s.drag = 0.02f; s.bounce = 0.25f; s.max_throw = 10.0f; break;
    case VRUI_WEIGHT_HEAVY:     // a hammer: swings behind your wrist one-handed, steady with two
        s.mass = 4.0f; s.drag = 0.01f; s.bounce = 0.1f; s.max_throw = 6.0f; s.sticky = true; s.slide = true; break;
    default:                    // an anvil: drag it with one hand, lift it with two, can't throw it
        s.mass = 40.0f; s.drag = 0.0f; s.bounce = 0.0f; s.max_throw = 1.5f; s.lift_hands = 2; s.sticky = true; break;
    }
    return s;
}

// --- geometry -------------------------------------------------------------------------

static Quaternion from_basis(Vector3 x, Vector3 y, Vector3 z)
{
    Matrix m = { x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, 0, 0, 0, 1 };
    return QuaternionNormalize(QuaternionFromMatrix(m));
}

static Vector3 orth(Vector3 v, Vector3 axis, Vector3 fallback)
{
    Vector3 o = Vector3Subtract(v, Vector3Scale(axis, Vector3DotProduct(v, axis)));
    if (Vector3Length(o) < 1e-4f) o = Vector3Subtract(fallback, Vector3Scale(axis, Vector3DotProduct(fallback, axis)));
    return Vector3Normalize(o);
}

static Vector3 grip_axis(const VruiGrip *g)
{
    Vector3 d = Vector3Subtract(g->b, g->a);
    return Vector3Length(d) > 1e-5f ? Vector3Normalize(d) : (Vector3){ 0, 1, 0 };
}

static Vector3 grip_point(const VruiGrip *g, float t) { return Vector3Lerp(g->a, g->b, t); }

// The thing's own basis for a handle: along it, its face, and the third.
static Quaternion grip_basis(const VruiGrip *g)
{
    Vector3 ax = grip_axis(g), fc = orth(g->face, ax, (Vector3){ 1, 0, 0 });
    return from_basis(ax, fc, Vector3CrossProduct(ax, fc));
}

// Where along a handle a point (world) is nearest, 0..1.
static float nearest_t(const VruiGrip *g, SfxrPose thing, Vector3 p)
{
    Vector3 l = sfxr_pose_apply_inv(thing, p), ab = Vector3Subtract(g->b, g->a);
    float len2 = Vector3DotProduct(ab, ab);
    return len2 > 1e-9f ? Clamp(Vector3DotProduct(Vector3Subtract(l, g->a), ab) / len2, 0, 1) : 0;
}

static SfxrPose box_pose(SfxrPose thing, const VruiWieldSpec *sp)
{
    return (SfxrPose){ sfxr_pose_apply(thing, sp->box_center), thing.orientation };
}

// --- holds ------------------------------------------------------------------------------

static HandHold *hold_of(VruiId id, int h) { return VRUI_STATE(vrui__widget_item(id, 1 + h), HandHold); }

// A hand takes hold: fix how the thing sits in it. With a handle, it sits the
// way it's meant to (along the fist, face to the knuckles), snapped to the
// allowed way round nearest how it lies now, so it turns as little as it can.
static void take(VruiId id, int h, int grip, float t, SfxrPose thing, const VruiWieldSpec *sp)
{
    HandHold *hh = hold_of(id, h);
    const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
    memset(hh, 0, sizeof *hh);
    hh->on = true;
    hh->grip = grip;
    hh->t = t;
    hh->last_hand = hand->grip.position;
    Quaternion hq = hand->grip.orientation, hinv = QuaternionInvert(hq);
    if (grip < 0 || SFXR_BREAK(vrui_wield_keeps_grab_angle)) {
        // no handle (or the old way): held exactly as taken
        SfxrPose rel = sfxr_pose_mul(sfxr_pose_inverse(hand->grip), thing);
        hh->rel = rel.orientation;
        hh->rel_pos = rel.position;
        if (grip >= 0) hh->rel_pos = Vector3Scale(Vector3RotateByQuaternion(grip_point(&sp->grip[grip], t), hh->rel), -1.0f);
        return;
    }
    const VruiGrip *g = &sp->grip[grip];
    Quaternion ib = grip_basis(g);
    // how it lies now, in the hand's frame
    Vector3 axis_now = Vector3RotateByQuaternion(Vector3RotateByQuaternion(grip_axis(g), thing.orientation), hinv);
    Vector3 face_now = Vector3RotateByQuaternion(Vector3RotateByQuaternion(orth(g->face, grip_axis(g), (Vector3){ 1, 0, 0 }),
                                                                           thing.orientation), hinv);
    bool reversed = g->reversible && axis_now.z > 0;   // lying the other way up: take it reversed
    Vector3 hz = { 0, 0, reversed ? 1.0f : -1.0f };
    // the ways round it may sit: face toward the knuckles (-Y), turned by whole steps about the fist
    Vector3 want = { face_now.x, face_now.y, 0 };
    Vector3 hy = { 0, -1, 0 };
    if (g->rolls == 0) {
        if (Vector3Length(want) > 1e-3f) hy = Vector3Normalize(want);
    } else {
        float best = -2;
        for (int k = 0; k < g->rolls; k++) {
            float a = 2.0f * PI * (float)k / (float)g->rolls;
            Vector3 c = { sinf(a), -cosf(a), 0 };
            float d = Vector3DotProduct(c, Vector3Length(want) > 1e-3f ? Vector3Normalize(want) : (Vector3){ 0, -1, 0 });
            if (d > best) { best = d; hy = c; }
        }
    }
    Quaternion hb = from_basis(hz, hy, Vector3CrossProduct(hz, hy));
    hh->rel = QuaternionMultiply(hb, QuaternionInvert(ib));
}

// Where one hand alone would put the thing.
static SfxrPose one_hand_target(const HandHold *hh, int h, const VruiWieldSpec *sp)
{
    SfxrPose hand = sfxr_hand((SfxrHandId)h)->grip;
    Quaternion q = QuaternionMultiply(hand.orientation, hh->rel);
    if (hh->grip < 0) return (SfxrPose){ sfxr_pose_apply(hand, hh->rel_pos), q };
    Vector3 p = grip_point(&sp->grip[hh->grip], hh->t);
    return (SfxrPose){ Vector3Subtract(hand.position, Vector3RotateByQuaternion(p, q)), q };
}

// Both hands on it: anchored at the main hand, its handle pointing from
// there through the other hand, turned about that line as the main hand has it.
static SfxrPose two_hand_target(const HandHold *hm, int m, const HandHold *hs, int s, const VruiWieldSpec *sp)
{
    SfxrPose one = one_hand_target(hm, m, sp);
    if (hm->grip < 0 || hs->grip < 0 || SFXR_BREAK(vrui_wield_second_hand_ignored)) {
        if (SFXR_BREAK(vrui_wield_second_hand_ignored)) return one;
        SfxrPose other = one_hand_target(hs, s, sp);   // no handles: halfway between what each hand says
        return (SfxrPose){ Vector3Lerp(one.position, other.position, 0.5f), QuaternionSlerp(one.orientation, other.orientation, 0.5f) };
    }
    Vector3 pm = sfxr_hand((SfxrHandId)m)->grip.position, ps = sfxr_hand((SfxrHandId)s)->grip.position;
    Vector3 lm = grip_point(&sp->grip[hm->grip], hm->t), ls = grip_point(&sp->grip[hs->grip], hs->t);
    Vector3 d_local = Vector3Subtract(ls, lm), d_world = Vector3Subtract(ps, pm);
    if (Vector3Length(d_local) < 0.05f || Vector3Length(d_world) < 0.05f) return one;   // hands together: one grip
    d_local = Vector3Normalize(d_local);
    d_world = Vector3Normalize(d_world);
    // the face, as the main hand holds it, squared up to the new line
    Vector3 face_local = orth(sp->grip[hm->grip].face, d_local, (Vector3){ 1, 0, 0 });
    Vector3 face_world = orth(Vector3RotateByQuaternion(face_local, one.orientation), d_world, (Vector3){ 0, 1, 0 });
    Quaternion lb = from_basis(d_local, face_local, Vector3CrossProduct(d_local, face_local));
    Quaternion wb = from_basis(d_world, face_world, Vector3CrossProduct(d_world, face_world));
    Quaternion q = QuaternionMultiply(wb, QuaternionInvert(lb));
    return (SfxrPose){ Vector3Subtract(pm, Vector3RotateByQuaternion(lm, q)), q };
}

// --- following, and loose -------------------------------------------------------------------

// How firmly it follows the hands: position and rotation spring frequencies
// (Hz). Heavy things are slower; held far from the balance point they turn
// slower still (the moment of inertia about the hand); two hands are
// steadier. Weightless: exactly on the hand (0).
static void spring_rates(const VruiWieldSpec *sp, float lever, int hands, float *f_pos, float *f_rot)
{
    if (sp->mass <= 0 || SFXR_BREAK(vrui_wield_weightless)) { *f_pos = *f_rot = 0; return; }
    float inertia = sp->mass * (lever * lever + 0.02f);
    *f_pos = Clamp(14.0f / sqrtf(sp->mass), 3.0f, 25.0f) * (hands >= 2 ? 2.0f : 1.0f);
    *f_rot = Clamp(3.0f / sqrtf(inertia), 1.5f, 25.0f) * (hands >= 2 ? 2.5f : 1.0f);
}

static float ground_at(const VruiWieldSpec *sp, Vector3 p) { return sp->ground ? sp->ground(p) : 0.0f; }

// Loose: gravity, drag, spin; on the ground it bounces, rubs to a stop, and
// settles onto its flattest side.
static bool loose_step(WieldState *ws, SfxrPose *pose, const VruiWieldSpec *sp, float dt)
{
    ws->vel.y -= sp->gravity * dt;
    float keep = expf(-sp->drag * dt);
    ws->vel = Vector3Scale(ws->vel, keep);
    pose->position = Vector3Add(pose->position, Vector3Scale(ws->vel, dt));
    float w = Vector3Length(ws->ang);
    if (w > 1e-4f)
        pose->orientation = QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(Vector3Scale(ws->ang, 1.0f / w), w * dt), pose->orientation));
    // the lowest corner of its box, against the ground
    SfxrPose box = box_pose(*pose, sp);
    float low = 1e9f;
    for (int i = 0; i < 8; i++) {
        Vector3 c = { (i & 1) ? sp->half.x : -sp->half.x, (i & 2) ? sp->half.y : -sp->half.y, (i & 4) ? sp->half.z : -sp->half.z };
        float y = sfxr_pose_apply(box, c).y;
        if (y < low) low = y;
    }
    float g = ground_at(sp, box.position);
    bool on_ground = low <= g + 0.002f;
    if (low < g) {
        pose->position.y += g - low;
        if (ws->vel.y < 0) ws->vel.y = ws->vel.y < -0.6f ? -ws->vel.y * sp->bounce : 0;
    }
    if (on_ground) {
        float rub = fmaxf(0, 1.0f - 6.0f * dt);
        ws->vel.x *= rub;
        ws->vel.z *= rub;
        ws->ang = Vector3Scale(ws->ang, fmaxf(0, 1.0f - 5.0f * dt));
        // tip onto the side nearest to flat: turn its most-upright axis to straight up
        Vector3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, best = { 0, 1, 0 };
        float bd = -1;
        for (int k = 0; k < 3; k++) {
            Vector3 a = Vector3RotateByQuaternion(axes[k], pose->orientation);
            if (fabsf(a.y) > bd) { bd = fabsf(a.y); best = a.y < 0 ? Vector3Negate(a) : a; }
        }
        Quaternion tip = QuaternionFromVector3ToVector3(best, (Vector3){ 0, 1, 0 });
        pose->orientation = QuaternionNormalize(QuaternionSlerp(pose->orientation, QuaternionMultiply(tip, pose->orientation),
                                                                fminf(1, 4.0f * dt)));
        if (Vector3Length(ws->vel) < 0.05f && Vector3Length(ws->ang) < 0.2f && bd > 0.999f) {
            ws->vel = ws->ang = Vector3Zero();
            return false;   // at rest
        }
    }
    return true;
}

// --- the widget --------------------------------------------------------------------------------

static void let_go(VruiId id, int h, WieldState *ws)
{
    HandHold *hh = hold_of(id, h);
    if (!hh->on) return;
    hh->on = false;
    if (C.grab_active[h] == id) C.grab_active[h] = VRUI_ID_NONE;
    sfxr_event("release", "%s %s", vrui__who(id), h ? "R" : "L");
    if (ws->main == h) {
        int other = 1 - h;
        ws->main = hold_of(id, other)->on ? other : -1;
    }
}

void vrui_wield_drop(VruiId id)
{
    WieldState *ws = VRUI_STATE(vrui__item(id), WieldState);
    for (int h = 0; h < 2; h++) let_go(id, h, ws);
}

void vrui_wield_set_motion(VruiId id, bool loose, Vector3 velocity, Vector3 spin)
{
    WieldState *ws = VRUI_STATE(vrui__item(id), WieldState);
    for (int h = 0; h < 2; h++) let_go(id, h, ws);
    ws->fly_hand = -1;
    ws->loose = loose;
    ws->vel = loose ? velocity : Vector3Zero();
    ws->ang = loose ? spin : Vector3Zero();
}

static int hands_on(VruiId id) { return (int)hold_of(id, 0)->on + (int)hold_of(id, 1)->on; }

VruiWield vrui_wield(VruiId id, SfxrPose *pose, const VruiWieldSpec *sp)
{
    VruiItem *it = vrui__item(id);
    WieldState *ws = VRUI_STATE(it, WieldState);
    if (!ws->have_prev) { ws->main = -1; ws->fly_hand = -1; ws->prev = *pose; ws->have_prev = true; ws->rest_y = pose->position.y; }
    float dt = sfxr_dt();
    VruiWield out = {0};
    int before = hands_on(id);

    // 1a. letting go: a sticky grip only once the hand is (nearly) fully open
    for (int h = 0; h < 2; h++) {
        HandHold *hh = hold_of(id, h);
        if (!hh->on) continue;
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        bool grab_down = vrui__grab_down(h);
        bool sticky = sp->sticky && !SFXR_BREAK(vrui_wield_not_sticky);
        bool still = hand->active && (grab_down || (sticky && hand->squeeze > 0.1f));
        if (!still) { let_go(id, h, ws); continue; }
        vrui__grab_offer(h, id, 0);   // keep the hand (and hide its laser)
        // loosened (sticky, not fully closed): slide along the handle
        hh->sliding = sticky && sp->slide && !grab_down && hh->grip >= 0;
        if (hh->sliding) {
            const VruiGrip *g = &sp->grip[hh->grip];
            float len = Vector3Distance(g->a, g->b);
            if (len > 1e-3f) {
                Vector3 axis = Vector3RotateByQuaternion(grip_axis(g), pose->orientation);
                float dt_hand = Vector3DotProduct(Vector3Subtract(hand->grip.position, hh->last_hand), axis);
                float fall = hands_on(id) == 1 ? 0.6f * dt * axis.y : 0;   // one loose hand: it slides down through it
                float t = Clamp(hh->t + (dt_hand + fall) / len, 0, 1);
                bool end = t <= 0 || t >= 1;
                if (end && !hh->at_end) vrui_haptic_pulse((SfxrHandId)h, 0.4f, 0.03f, 0);   // the end of the handle
                hh->at_end = end;
                hh->t = t;
            }
        }
        hh->last_hand = hand->grip.position;
    }

    // 1b. taking hold: a hand near a handle (or the thing, with none) and the grab button
    for (int h = 0; h < 2; h++) {
        HandHold *hh = hold_of(id, h);
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        if (hh->on || !hand->active) continue;
        int best_g = -1;
        float best_d = 1e9f, best_t = 0;
        if (sp->ngrips == 0) {
            float d = vrui__point_box_dist(hand->grip.position, box_pose(*pose, sp), sp->half);
            if (d >= 0) best_d = d;
        }
        for (int g = 0; g < sp->ngrips; g++) {
            float t = nearest_t(&sp->grip[g], *pose, hand->grip.position);
            float d = Vector3Distance(sfxr_pose_apply(*pose, grip_point(&sp->grip[g], t)), hand->grip.position);
            if (d < best_d) { best_d = d; best_g = g; best_t = t; }
        }
        if (best_d <= sp->reach) vrui__grab_offer(h, id, best_d);
        if (sp->pull && before == 0 && ws->fly_hand < 0) {
            float t = vrui__ray_box(vrui__hand_ray(h), box_pose(*pose, sp), sp->half);
            if (t >= 0) vrui__ray_offer(h, id, t);
        }
        if (vrui__grab_hot(h, id) && vrui__grab_pressed(h) && vrui__hand_free(h)) {
            int others = hands_on(id);
            if (others == 1 && !sp->two_handed) vrui_wield_drop(id);   // not two-handed: this hand takes it over
            take(id, h, best_g, best_t, *pose, sp);
            C.grab_active[h] = id;
            if (ws->main < 0) ws->main = h;
            ws->fly_hand = -1;
            sfxr_event("grab", "%s %s hand", vrui__who(id), h ? "R" : "L");
            vrui__click_pulse(h);
        } else if (vrui__ray_hot(h, id) && vrui__squeeze(h)->pressed && vrui__hand_free(h) && ws->fly_hand < 0 && before == 0) {
            ws->fly_hand = h;   // laser + grip: it flies to the hand
            ws->fly_t = 0;
            ws->fly_from = *pose;
            ws->loose = false;
            sfxr_event("pull", "%s %s", vrui__who(id), h ? "R" : "L");
        }
        if (vrui__grab_hot(h, id) || vrui__ray_hot(h, id)) out.hovered = true;
    }
    int hands = hands_on(id);

    // 2-3. where it goes
    SfxrPose shown = *pose;
    if (hands > 0) {
        int m = ws->main >= 0 && hold_of(id, ws->main)->on ? ws->main : (hold_of(id, 0)->on ? 0 : 1);
        ws->main = m;
        HandHold *hm = hold_of(id, m), *hs = hold_of(id, 1 - m);
        SfxrPose target = hs->on ? two_hand_target(hm, m, hs, 1 - m, sp) : one_hand_target(hm, m, sp);
        if (before == 0) { ws->vel = ws->ang = Vector3Zero(); ws->loose = false; ws->rest_y = pose->position.y; }
        // too heavy for the hands on it: it drags (it won't come up off where it was)
        out.straining = hands < sp->lift_hands;
        if (out.straining && target.position.y > ws->rest_y + 0.03f) target.position.y = ws->rest_y + 0.03f;
        if (!out.straining) ws->rest_y = fminf(ws->rest_y, target.position.y);
        // the lever: from where the main hand holds it to its balance point
        Vector3 held_at = hm->grip >= 0 ? grip_point(&sp->grip[hm->grip], hm->t)
                                        : Vector3Negate(Vector3RotateByQuaternion(hm->rel_pos, QuaternionInvert(hm->rel)));
        float lever = Vector3Distance(held_at, sp->center);
        float fp, fr;
        spring_rates(sp, lever, hands, &fp, &fr);
        if (fp <= 0) shown = target;
        else {
            vrui_spring3(&shown.position, &ws->vel, target.position, fp, 0.75f, dt);
            vrui_springq(&shown.orientation, &ws->ang, target.orientation, fr, 0.75f, dt);
        }
        Quaternion a = shown.orientation, b = target.orientation;
        float ang = 2.0f * acosf(Clamp(fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 0, 1));
        out.lag = Vector3Distance(shown.position, target.position) + ang * 0.3f;
        if (out.straining || out.lag > 0.06f)   // the weight, in the hand
            vrui_haptic_pulse((SfxrHandId)m, Clamp(out.straining ? 0.35f : out.lag * 2.0f, 0, 0.5f), 0.02f, 60);
    } else if (ws->fly_hand >= 0) {
        // flying to the hand: a quick ease, aiming at where the hand holds it
        int h = ws->fly_hand;
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        if (!hand->active || !vrui__squeeze(h)->down) {
            ws->fly_hand = -1;   // let go mid-flight: it drops
            ws->loose = true;
            ws->vel = ws->v_est;
        } else {
            ws->fly_t += dt / 0.3f;
            HandHold probe;
            memset(&probe, 0, sizeof probe);
            SfxrPose target = hand->grip;
            if (sp->ngrips > 0) {
                HandHold *hh = hold_of(id, h);
                take(id, h, 0, 0.5f, *pose, sp);   // how it would sit...
                probe = *hh;
                hh->on = false;                    // ...but it isn't held yet
                target = one_hand_target(&probe, h, sp);
            }
            float e = vrui_ease(VRUI_EASE_SMOOTH, fminf(1, ws->fly_t));
            shown.position = Vector3Lerp(ws->fly_from.position, target.position, e);
            shown.orientation = QuaternionSlerp(ws->fly_from.orientation, target.orientation, e);
            out.pulling = true;
            if (ws->fly_t >= 1) {
                take(id, h, sp->ngrips > 0 ? 0 : -1, 0.5f, shown, sp);
                C.grab_active[h] = id;
                ws->main = h;
                ws->fly_hand = -1;
                ws->vel = ws->ang = Vector3Zero();
                sfxr_event("grab", "%s %s pulled", vrui__who(id), h ? "R" : "L");
                vrui__click_pulse(h);
                hands = 1;
            }
        }
    } else if (ws->loose && !sp->own_physics) {
        ws->loose = loose_step(ws, &shown, sp, dt);
    }

    // released by the last hand: loose, with the swing it had (capped)
    if (before > 0 && hands == 0 && ws->fly_hand < 0) {
        out.released = true;
        ws->loose = !sp->own_physics;
        ws->vel = ws->v_est;
        float v = Vector3Length(ws->vel);
        if (v > sp->max_throw && v > 0) ws->vel = Vector3Scale(ws->vel, sp->max_throw / v);
        ws->ang = ws->w_est;
        float w = Vector3Length(ws->ang);
        if (w > 20.0f) ws->ang = Vector3Scale(ws->ang, 20.0f / w);
    }
    out.grabbed = before == 0 && hands > 0;

    // measured motion (what a throw takes)
    if (dt > 0) {
        Vector3 v = Vector3Scale(Vector3Subtract(shown.position, ws->prev.position), 1.0f / dt);
        Quaternion dq = QuaternionMultiply(shown.orientation, QuaternionInvert(ws->prev.orientation));
        if (dq.w < 0) dq = (Quaternion){ -dq.x, -dq.y, -dq.z, -dq.w };
        Vector3 axis;
        float angle;
        QuaternionToAxisAngle(dq, &axis, &angle);
        Vector3 w = Vector3Scale(axis, angle / dt);
        ws->v_est = Vector3Lerp(ws->v_est, v, 0.6f);
        ws->w_est = Vector3Lerp(ws->w_est, w, 0.6f);
    }
    ws->prev = shown;
    *pose = shown;

    out.pose = shown;
    out.velocity = ws->v_est;
    out.angular_velocity = ws->w_est;
    out.hands = hands;
    out.hand = (SfxrHandId)(ws->main >= 0 ? ws->main : 0);
    out.loose = ws->loose;
    for (int h = 0; h < 2; h++) out.sliding[h] = hold_of(id, h)->on && hold_of(id, h)->sliding;

    vrui__hint(id, sp->ngrips > 0 ? (sp->slide ? "take the handle | loosen your grip to slide | other hand: both hands | laser + grip: pull"
                                               : "take the handle | other hand on it: both hands | laser + grip: pull it to you")
                                  : "grab it | laser + grip: pull it to you");
    // for tests: its pose, and its first handle's middle as the part ("rack.sword/handle")
    SfxrPose part = sp->ngrips > 0 ? (SfxrPose){ sfxr_pose_apply(shown, grip_point(&sp->grip[0], 0.5f)), shown.orientation } : shown;
    vrui__report(id, "wield", shown, part, (float)hands, true);
    return out;
}
