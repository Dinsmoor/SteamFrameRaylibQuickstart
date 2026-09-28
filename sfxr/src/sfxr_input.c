// sfxr_input.c - from raw readings to what apps use: pressed/released edges,
// the trigger and grip pull levels, every Frame control's press and touch,
// hand shapes and finger curl; and the accessors for hands, gaze and haptics.

#include "sfxr_internal.h"

#include <math.h>
#include <string.h>

#define S sfxr_state

// ---------------------------------------------------------------------------
// Input derivation
// ---------------------------------------------------------------------------

static void edge(SfxrButton *b, bool now, bool touched)
{
    b->pressed = now && !b->down;
    b->released = !now && b->down;
    b->down = now;
    b->touched = touched || now;
}

// Press/release points per pull level (see SfxrPull in sfxr.h).
static struct { float press, release; } pull_th[SFXR_PULL_COUNT] = {
    [SFXR_PULL_SOFT] = { 0.25f, 0.15f },
    [SFXR_PULL_FIRM] = { 0.55f, 0.35f },
    [SFXR_PULL_FULL] = { 0.97f, 0.85f },
};

void sfxr_set_pull_threshold(SfxrPull level, float press, float release)
{
    if ((unsigned)level >= SFXR_PULL_COUNT) return;
    press = Clamp(press, 0.01f, 1.0f);
    pull_th[level].press = press;
    pull_th[level].release = Clamp(release, 0.0f, press);
}

// The analog value decides every level. The hardware click only helps FULL:
// it counts as fully pulled even if the analog reads a bit short.
static void pull_levels(SfxrButton out[SFXR_PULL_COUNT], float v, bool click, bool touched)
{
    for (int l = 0; l < SFXR_PULL_COUNT; l++) {
        SfxrButton *b = &out[l];
        float rel = SFXR_BREAK(sfxr_no_hysteresis) ? pull_th[l].press : pull_th[l].release;
        bool now = b->down ? (v > rel) : (v >= pull_th[l].press);
        if (l == SFXR_PULL_FULL && click) now = true;
        edge(b, now, touched);
    }
}

static const struct { const char *name; unsigned hands; } CONTROLS[SFXR_CTL_COUNT] = {   // hands: bit 0 left, bit 1 right
    [SFXR_CTL_TRIGGER] = { "Trigger", 3 }, [SFXR_CTL_SQUEEZE] = { "Grip", 3 },
    [SFXR_CTL_STICK] = { "Stick", 3 },     [SFXR_CTL_BUMPER] = { "Bumper", 3 },
    [SFXR_CTL_A] = { "A", 2 }, [SFXR_CTL_B] = { "B", 2 }, [SFXR_CTL_X] = { "X", 2 }, [SFXR_CTL_Y] = { "Y", 2 },
    [SFXR_CTL_MENU] = { "Menu", 2 },       [SFXR_CTL_VIEW] = { "View", 1 },
    [SFXR_CTL_DPAD_UP] = { "D-pad up", 1 }, [SFXR_CTL_DPAD_DOWN] = { "D-pad down", 1 },
    [SFXR_CTL_DPAD_LEFT] = { "D-pad left", 1 }, [SFXR_CTL_DPAD_RIGHT] = { "D-pad right", 1 },
};

const char *sfxr_control_name(SfxrControl c)
{
    return (unsigned)c < SFXR_CTL_COUNT ? CONTROLS[c].name : "?";
}

bool sfxr_control_on_hand(SfxrControl c, SfxrHandId hand)
{
    return (unsigned)c < SFXR_CTL_COUNT && (CONTROLS[c].hands >> (hand == SFXR_RIGHT ? 1 : 0)) & 1;
}

// Poses a backend didn't provide are derived, so apps can always use them:
// poke = just past the aim point, pinch = aim, palm = grip.
static SfxrPose raw_pose(const SfxrRawHand *r, int bit, SfxrPose given)
{
    if (r->pose_valid & bit) return given;
    switch (bit) {
    case RAW_POSE_POKE: {
        SfxrPose p = r->aim;
        p.position = Vector3Add(r->aim.position, Vector3Scale(sfxr_pose_forward(r->aim), 0.01f));
        return p;
    }
    case RAW_POSE_PINCH: return r->aim;
    default:             return r->grip;
    }
}

// ---------------------------------------------------------------------------
// Hand shapes
// ---------------------------------------------------------------------------

const char *sfxr_hand_shape_name(SfxrHandShape s)
{
    static const char *n[SFXR_SHAPE_COUNT] = { "relaxed", "open", "point", "fist", "thumbs up", "pinch" };
    return (unsigned)s < SFXR_SHAPE_COUNT ? n[s] : "?";
}

// Bend of a finger chain: angle between its first and last bone, 0..1.
static float chain_curl(const SfxrHandJoints *j, int a, int b, int c, int d, float full_deg)
{
    Vector3 u = Vector3Subtract(j->joint[b].position, j->joint[a].position);
    Vector3 v = Vector3Subtract(j->joint[d].position, j->joint[c].position);
    float lu = Vector3Length(u), lv = Vector3Length(v);
    if (lu < 1e-5f || lv < 1e-5f) return 0;
    float cosang = Clamp(Vector3DotProduct(u, v) / (lu * lv), -1, 1);
    return Clamp(acosf(cosang) * RAD2DEG / full_deg, 0, 1);
}

float sfxr__finger_curl(const SfxrHandJoints *j, int f)
{
    if (f == SFXR_FINGER_THUMB)
        return chain_curl(j, SFXR_JOINT_THUMB_METACARPAL, SFXR_JOINT_THUMB_PROXIMAL, SFXR_JOINT_THUMB_DISTAL, SFXR_JOINT_THUMB_TIP, 70.0f);
    int m = SFXR_JOINT_INDEX_METACARPAL + (f - 1) * 5;
    return chain_curl(j, m, m + 1, m + 3, m + 4, 150.0f);
}

// `pinching`: thumb and index tips are touching (joints only). A light
// fingertip pinch barely curls the index, so the distance decides, not curl.
static SfxrHandShape shape_from_curl(const float c[5], bool pinching)
{
    float others = (c[2] + c[3] + c[4]) / 3.0f;
    if (pinching && others < 0.35f && !SFXR_BREAK(sfxr_pinch_shape_from_curl_only)) return SFXR_SHAPE_PINCH;
    bool index_out = c[1] < 0.35f, index_in = c[1] > 0.55f;
    bool others_out = others < 0.35f, others_in = others > 0.55f;
    bool thumb_in = c[0] > 0.4f, thumb_out = c[0] < 0.3f;
    if (index_out && others_out && !thumb_in) return SFXR_SHAPE_OPEN;
    if (index_out && others_in) return SFXR_SHAPE_POINT;
    if (index_in && others_in && thumb_out) return SFXR_SHAPE_THUMBS_UP;
    if (index_in && others_in) return SFXR_SHAPE_FIST;
    if (index_in && thumb_in && others_out) return SFXR_SHAPE_PINCH;
    return SFXR_SHAPE_RELAXED;
}

static void derive_shapes(void)
{
    const uint32_t thumb_ctls = RAW_BIT(SFXR_CTL_STICK) | RAW_BIT(SFXR_CTL_A) | RAW_BIT(SFXR_CTL_B) | RAW_BIT(SFXR_CTL_X) |
                                RAW_BIT(SFXR_CTL_Y) | RAW_BIT(SFXR_CTL_MENU) | RAW_BIT(SFXR_CTL_VIEW) |
                                RAW_BIT(SFXR_CTL_DPAD_UP) | RAW_BIT(SFXR_CTL_DPAD_DOWN) | RAW_BIT(SFXR_CTL_DPAD_LEFT) |
                                RAW_BIT(SFXR_CTL_DPAD_RIGHT);
    for (int i = 0; i < 2; i++) {
        SfxrHand *h = &S.hands[i];
        const SfxrRawHand *r = &S.raw[i];
        const SfxrHandJoints *j = &S.sig.joints[i];
        float c[5];
        if (j->valid && !SFXR_BREAK(sfxr_shapes_from_values_only)) {
            // measured: bend from the first to the last bone of each finger
            for (int f = 0; f < 5; f++) c[f] = sfxr__finger_curl(j, f);
            h->curl_from_joints = true;
        } else {
            // estimated from the touch sensors: a finger resting on its control
            // counts as mostly closed; pulling it in closes it the rest of the way
            bool touch_ok = !SFXR_BREAK(sfxr_shapes_from_values_only);
            bool trig_t = touch_ok && (r->touch & RAW_BIT(SFXR_CTL_TRIGGER));
            bool grip_t = touch_ok && (r->touch & RAW_BIT(SFXR_CTL_SQUEEZE));
            bool thumb_t = touch_ok && (r->touch & thumb_ctls);
            c[1] = fmaxf(trig_t ? 0.6f : 0.0f, r->trigger);
            c[2] = c[3] = c[4] = fmaxf(grip_t ? 0.6f : 0.0f, r->squeeze);
            c[0] = thumb_t ? 0.7f : 0.0f;
            h->curl_from_joints = false;
        }
        for (int f = 0; f < 5; f++) h->curl[f] = c[f];
        // debounce: a new shape must hold for 3 frames
        bool pinching = j->valid && S.gestures[i].pinch.down;
        SfxrHandShape now = r->active ? shape_from_curl(c, pinching) : SFXR_SHAPE_RELAXED;
        if (now == h->shape) S.shape_frames[i] = 0;
        else if (now == S.shape_pending[i]) { if (++S.shape_frames[i] >= 3) { h->shape = now; S.shape_frames[i] = 0; } }
        else { S.shape_pending[i] = now; S.shape_frames[i] = 1; }
    }
}

// SteamVR's hand skeleton for the Frame controllers (joints from the touch
// sensors, XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT) has the thumb
// mirrored across the controller: lift your thumb off and its bones swing
// ~3 cm to the FAR side of the controller, away from the palm, where a real
// thumb lifts up and out on the palm side (every recorded session, both
// hands, as mirror images of each other). Reflect the thumb back across the
// controller's side-to-side axis, about its own base; the palm and fingers
// are left as reported. Applied here, after recording, so recordings keep
// what the runtime said and replays get the same fix.
void sfxr__unmirror_thumb(SfxrPose grip, SfxrHandJoints *j)
{
    Quaternion gi = QuaternionInvert(grip.orientation);
    Vector3 base = Vector3RotateByQuaternion(Vector3Subtract(j->joint[SFXR_JOINT_THUMB_METACARPAL].position, grip.position), gi);
    for (int k = SFXR_JOINT_THUMB_METACARPAL; k <= SFXR_JOINT_THUMB_TIP; k++) {
        SfxrPose *p = &j->joint[k];
        Vector3 l = Vector3RotateByQuaternion(Vector3Subtract(p->position, grip.position), gi);
        l.x = 2.0f * base.x - l.x;
        p->position = Vector3Add(grip.position, Vector3RotateByQuaternion(l, grip.orientation));
        // the same reflection for its orientation: x -> -x is (x, y, z, w) -> (x, -y, -z, w)
        Quaternion q = QuaternionMultiply(gi, p->orientation);
        q = (Quaternion){ q.x, -q.y, -q.z, q.w };
        p->orientation = QuaternionMultiply(grip.orientation, q);
    }
}

void sfxr__derive_input(void)
{
    sfxr__hands_update();   // gestures; hands known only as joints get raw input first
    SfxrPose rig = sfxr_rig_pose();
    for (int i = 0; i < 2; i++) {
        const SfxrRawHand *r = &S.raw[i];
        SfxrHand *h = &S.hands[i];
        if (h->active != r->active || (r->active && h->source != (SfxrInputSource)r->source))
            sfxr_event("hand", "%s %s", i ? "R" : "L",
                       !r->active ? "lost" : r->source == SFXR_SOURCE_HAND ? "bare hand" : "controller");
        h->active = r->active;
        h->source = (SfxrInputSource)r->source;
        h->grip  = sfxr_pose_mul(rig, r->grip);
        h->aim   = sfxr_pose_mul(rig, r->aim);
        h->poke  = sfxr_pose_mul(rig, raw_pose(r, RAW_POSE_POKE, r->poke));
        h->pinch = sfxr_pose_mul(rig, raw_pose(r, RAW_POSE_PINCH, r->pinch));
        h->palm  = sfxr_pose_mul(rig, raw_pose(r, RAW_POSE_PALM, r->palm));
        h->velocity = Vector3RotateByQuaternion(r->velocity, rig.orientation);
        h->angular_velocity = Vector3RotateByQuaternion(r->angular_velocity, rig.orientation);
        h->trigger = r->trigger;
        h->squeeze = r->squeeze;
        h->stick = r->stick;
        for (int c = 0; c < SFXR_CTL_COUNT; c++)
            edge(&h->button[c], (r->click >> c) & 1, (r->touch >> c) & 1);
        pull_levels(h->trigger_at, r->trigger, h->button[SFXR_CTL_TRIGGER].down, h->button[SFXR_CTL_TRIGGER].touched);
        pull_levels(h->squeeze_at, r->squeeze, h->button[SFXR_CTL_SQUEEZE].down, h->button[SFXR_CTL_SQUEEZE].touched);
        h->trigger_btn = h->trigger_at[SFXR_PULL_FIRM];
        h->squeeze_btn = h->squeeze_at[SFXR_PULL_FIRM];
        h->stick_btn  = h->button[SFXR_CTL_STICK];
        h->bumper     = h->button[SFXR_CTL_BUMPER];
        h->a          = h->button[SFXR_CTL_A];
        h->b          = h->button[SFXR_CTL_B];
        h->x          = h->button[SFXR_CTL_X];
        h->y          = h->button[SFXR_CTL_Y];
        h->menu       = h->button[SFXR_CTL_MENU];
        h->view       = h->button[SFXR_CTL_VIEW];
        h->dpad_up    = h->button[SFXR_CTL_DPAD_UP];
        h->dpad_down  = h->button[SFXR_CTL_DPAD_DOWN];
        h->dpad_left  = h->button[SFXR_CTL_DPAD_LEFT];
        h->dpad_right = h->button[SFXR_CTL_DPAD_RIGHT];
        h->primary    = i ? h->a : h->dpad_down;
        h->secondary  = i ? h->b : h->dpad_up;
    }
    if (S.gaze_valid) S.gaze_world = sfxr_pose_mul(rig, S.gaze_stage);
    derive_shapes();
    for (int i = 0; i < 2; i++) {
        const SfxrHandJoints *in = &S.sig.joints[i];
        SfxrHandJoints *out = &S.joints_world[i];
        out->valid = in->valid;
        out->source = in->source;
        SfxrHandJoints fixed;
        if (in->valid && in->source == SFXR_SOURCE_CONTROLLER && strstr(S.raw[i].profile, "frame_controller") &&
            !SFXR_BREAK(sfxr_thumb_as_reported)) {
            fixed = *in;
            sfxr__unmirror_thumb(S.raw[i].grip, &fixed);
            in = &fixed;
        }
        for (int j = 0; j < SFXR_JOINT_COUNT; j++) {
            out->joint[j] = in->valid ? sfxr_pose_mul(rig, in->joint[j]) : sfxr_pose_identity();
            out->radius[j] = in->radius[j];
        }
    }
}

const SfxrHand *sfxr_hand(SfxrHandId hand) { return &S.hands[hand == SFXR_RIGHT ? 1 : 0]; }
const char *sfxr_interaction_profile(SfxrHandId hand) { return S.raw[hand == SFXR_RIGHT ? 1 : 0].profile; }

void sfxr_haptic(SfxrHandId hand, float amplitude, float seconds, float frequency_hz)
{
    if (S.initialized && S.vt->haptic) S.vt->haptic(hand, amplitude, seconds, frequency_hz);
}

bool sfxr_gaze(SfxrPose *out)
{
    if (!S.gaze_valid) return false;
    if (out) *out = S.gaze_world;
    return true;
}
