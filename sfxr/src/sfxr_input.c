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

// The controller's tip: just past the aim point.
static SfxrPose tip_poke(const SfxrRawHand *r)
{
    SfxrPose p = r->aim;
    p.position = Vector3Add(r->aim.position, Vector3Scale(sfxr_pose_forward(r->aim), 0.01f));
    return p;
}

// Poses a backend didn't provide are derived, so apps can always use them:
// poke = the tip, pinch = aim, palm = grip.
static SfxrPose raw_pose(const SfxrRawHand *r, int bit, SfxrPose given)
{
    if (r->pose_valid & bit) return given;
    switch (bit) {
    case RAW_POSE_POKE: return tip_poke(r);
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
        // Joints give shapes for bare hands. Holding controllers, SteamVR's
        // skeleton keeps the index curled round the trigger even when the
        // finger is lifted off it (curl 0.97 with only the grip touched, in the
        // recordings), so a point never shows: the touch sensors decide.
        bool from_joints = j->valid && (j->source == SFXR_SOURCE_HAND || SFXR_BREAK(sfxr_shapes_from_controller_skeleton));
        if (from_joints && !SFXR_BREAK(sfxr_shapes_from_values_only)) {
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
// sensors, XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT) puts the thumb in the
// wrong place: touch the stick and its tip lands on the A button; touch A and
// it lands on the stick; lifted, it hangs over the far side (every recorded
// session, both hands; measured against the controller's own render model in
// SteamVR's driver). So the thumb is posed here instead, from the same touch
// sensors: its tip on the control it touches (following the stick as it
// tilts), or hovering over the face when it touches nothing; its bones arc
// from the reported base to there. The palm and fingers are left as
// reported. Applied after recording, so recordings keep what the runtime
// said and replays get the same fix.

// Where the thumb goes on the RIGHT controller, grip-local (the left one is
// its mirror image in x: D-pad where the buttons are, view where menu is).
// From frame_controller_right.json: each part's origin, into the grip frame.
static const Vector3 FACE_N = { -0.165f, 0.777f, -0.607f };   // out of the thumb face
static const struct { Vector3 p; SfxrControl right, left; } THUMB_SPOT[] = {
    { { -0.0278f, 0.0111f, -0.0537f }, SFXR_CTL_STICK, SFXR_CTL_STICK },
    { { 0.0009f, 0.0096f, -0.0539f }, SFXR_CTL_A, SFXR_CTL_DPAD_DOWN },
    { { 0.0087f, 0.0044f, -0.0621f }, SFXR_CTL_B, SFXR_CTL_DPAD_LEFT },
    { { -0.0087f, 0.0037f, -0.0593f }, SFXR_CTL_X, SFXR_CTL_DPAD_RIGHT },
    { { -0.0009f, -0.0015f, -0.0676f }, SFXR_CTL_Y, SFXR_CTL_DPAD_UP },
    { { -0.0165f, -0.0057f, -0.0670f }, SFXR_CTL_MENU, SFXR_CTL_VIEW },
};
#define THUMB_PAD 0.010f     // the tip joint sits this far off a control (the pad's thickness)
#define THUMB_STICK 0.005f   // the stick's cap stands this far above its origin
#define THUMB_HOVER 0.028f   // a lifted thumb, above the face

static Vector3 mirror_for(int hand, Vector3 v) { return hand ? v : (Vector3){ -v.x, v.y, v.z }; }

Vector3 sfxr__thumb_spot(int hand, unsigned touch, Vector2 stick)
{
    Vector3 n = mirror_for(hand, FACE_N), sum = { 0 };
    int count = 0;
    for (size_t k = 0; k < sizeof THUMB_SPOT / sizeof THUMB_SPOT[0]; k++) {
        SfxrControl c = hand ? THUMB_SPOT[k].right : THUMB_SPOT[k].left;
        if (!(touch & RAW_BIT(c))) continue;
        Vector3 p = Vector3Add(mirror_for(hand, THUMB_SPOT[k].p), Vector3Scale(n, THUMB_PAD));
        if (c == SFXR_CTL_STICK) {   // on top of the cap, which tilts with the stick
            Vector3 u = Vector3Normalize(Vector3Subtract((Vector3){ 1, 0, 0 }, Vector3Scale(n, n.x)));   // right, along the face
            Vector3 v = Vector3CrossProduct(n, u);                                                       // up the face
            p = Vector3Add(p, Vector3Scale(n, THUMB_STICK));
            p = Vector3Add(p, Vector3Add(Vector3Scale(u, stick.x * 0.006f), Vector3Scale(v, stick.y * 0.006f)));
        }
        sum = Vector3Add(sum, p);
        count++;
    }
    if (count) return Vector3Scale(sum, 1.0f / (float)count);
    // touching nothing: over the middle of the face, between the stick and the buttons
    Vector3 mid = Vector3Lerp(mirror_for(hand, THUMB_SPOT[0].p), mirror_for(hand, THUMB_SPOT[1].p), 0.5f);
    return Vector3Add(mid, Vector3Scale(n, THUMB_HOVER));
}

// Pose the thumb's bones (grip-local target) as an arc from its base
// (the metacarpal, kept) to the tip at `target`, bulging out of the face
// (the nail side up), with the bone lengths it has.
void sfxr__thumb_reach(int hand, SfxrPose grip, Vector3 target, SfxrHandJoints *j)
{
    const int first = SFXR_JOINT_THUMB_METACARPAL;
    Vector3 p[4];
    float len[3], total = 0;
    static const float fallback[3] = { 0.040f, 0.032f, 0.025f };
    for (int k = 0; k < 4; k++) p[k] = j->joint[first + k].position;
    for (int k = 0; k < 3; k++) {
        len[k] = Vector3Distance(p[k], p[k + 1]);
        if (len[k] < 0.01f || len[k] > 0.08f) len[k] = fallback[k];
        total += len[k];
    }
    Vector3 base = p[0], tip = sfxr_pose_apply(grip, target);
    Vector3 n = Vector3RotateByQuaternion(mirror_for(hand, FACE_N), grip.orientation);
    Vector3 chord = Vector3Subtract(tip, base);
    float d = Vector3Length(chord);
    // SteamVR's thumb is a little short for the far side of the face (the
    // stick): stretch it up to a third to get there
    if (d > total) {
        float grow = fminf(d / total, 1.35f);
        for (int k = 0; k < 3; k++) len[k] *= grow;
        total *= grow;
    }
    Vector3 e = d > 1e-5f ? Vector3Scale(chord, 1.0f / d) : sfxr_pose_forward(grip);
    Vector3 b = Vector3Subtract(n, Vector3Scale(e, Vector3DotProduct(n, e)));   // the bulge: away from the face
    b = Vector3Length(b) > 1e-4f ? Vector3Normalize(b) : Vector3RotateByQuaternion((Vector3){ 0, 1, 0 }, grip.orientation);
    // the arc of length `total` over the chord: sin(t/2)/(t/2) = d/total
    float ratio = Clamp(d / total, 0.0f, 1.0f), lo = 0, hi = 3.5f;
    for (int it = 0; it < 30; it++) {
        float t = 0.5f * (lo + hi);
        if (sinf(t * 0.5f) / (t * 0.5f) > ratio) lo = t; else hi = t;
    }
    float theta = 0.5f * (lo + hi);
    float s = 0;
    for (int k = 0; k < 4; k++) {
        Vector3 at, fwd, up;
        if (theta < 1e-3f) {   // straight (or out of reach: pointing at it)
            at = Vector3Add(base, Vector3Scale(e, s));
            fwd = e;
            up = b;
        } else {
            float r = total / theta, phi = -0.5f * theta + s / r;
            Vector3 center = Vector3Subtract(Vector3Add(base, Vector3Scale(e, 0.5f * d)), Vector3Scale(b, r * cosf(0.5f * theta)));
            up = Vector3Add(Vector3Scale(b, cosf(phi)), Vector3Scale(e, sinf(phi)));   // out from the arc's center
            at = Vector3Add(center, Vector3Scale(up, r));
            fwd = Vector3Add(Vector3Scale(b, -sinf(phi)), Vector3Scale(e, cosf(phi)));
        }
        // OpenXR joints: -Z along the bone toward the tip, +Y out of the back (the nail)
        Vector3 z = Vector3Negate(fwd), y = Vector3Normalize(Vector3Subtract(up, Vector3Scale(z, Vector3DotProduct(up, z))));
        Vector3 x = Vector3CrossProduct(y, z);
        Matrix m = { x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, 0, 0, 0, 1 };
        j->joint[first + k] = (SfxrPose){ at, QuaternionNormalize(QuaternionFromMatrix(m)) };
        if (k < 3) s += len[k];
    }
}

// The thumb's tip glides to where the sensors say it is (a touch is a
// step; a real thumb takes a moment to get there).
static void pose_thumb(int i, SfxrHandJoints *j)
{
    const SfxrRawHand *r = &S.raw[i];
    Vector3 want = sfxr__thumb_spot(i, r->touch | r->click, r->stick);
    float k = 1.0f - expf(-sfxr_dt() * 30.0f);
    S.thumb_tip[i] = S.thumb_tip_valid[i] ? Vector3Lerp(S.thumb_tip[i], want, k) : want;
    S.thumb_tip_valid[i] = true;
    sfxr__thumb_reach(i, r->grip, S.thumb_tip[i], j);
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
        // With controllers, poke from the controller's tip. SteamVR's poke pose
        // for the Frame controllers is 12.5 cm BELOW the grip and 4.6 cm to the
        // side (recorded, every session): nowhere near a finger or the tip.
        bool runtime_poke = r->source != SFXR_SOURCE_CONTROLLER || SFXR_BREAK(sfxr_poke_from_runtime);
        h->poke  = sfxr_pose_mul(rig, runtime_poke ? raw_pose(r, RAW_POSE_POKE, r->poke) : tip_poke(r));
        h->pinch = sfxr_pose_mul(rig, raw_pose(r, RAW_POSE_PINCH, r->pinch));
        h->palm  = sfxr_pose_mul(rig, raw_pose(r, RAW_POSE_PALM, r->palm));
        // A controller's palm pose (palm_ext, or the grip when there is none)
        // has the grip's axes: the palm faces its -X on the right hand and +X
        // on the left, and -Y runs down the handle. Turn it to the hand-joint
        // convention, -Y out of the palm, so `palm` means one thing for both.
        // (Recorded on the Frame: palm_ext is the grip tipped 42 degrees about
        // X, and SteamVR's own skeleton puts the palm normal along +-X.)
        if (r->source == SFXR_SOURCE_CONTROLLER && !SFXR_BREAK(sfxr_controller_palm_as_reported))
            h->palm.orientation = QuaternionMultiply(h->palm.orientation,
                                                     QuaternionFromAxisAngle((Vector3){ 0, 0, 1 }, (i ? -90.0f : 90.0f) * DEG2RAD));
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
            pose_thumb(i, &fixed);
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
