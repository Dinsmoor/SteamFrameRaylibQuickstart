// vrui_attach.c - things that go with the player: the body estimate
// (belts, holsters, body-locked displays), lazy follow (tag-along HUDs) and
// arrows toward things out of view. vrui.h section 10, docs/ATTACHING.md.

#include "vrui_internal.h"

// Yaw of a direction on the floor: 0 = -Z, the rotation about +Y that
// QuaternionFromAxisAngle((0,1,0), yaw) would give (counter-clockwise from above).
static float yaw_of(Vector3 f) { return atan2f(-f.x, -f.z); }

static float wrap_pi(float a)
{
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

// The headset only tracks the head and hands, so the torso is a guess:
//   * it stands on the floor under the head, a little behind the eyes (the
//     neck is behind them);
//   * it faces where the head faces, but lazily: turning your head less than
//     45 degrees leaves it alone, more drags it along, and it drifts round to
//     where you keep looking within a couple of seconds (people turn their
//     body to follow their head).
// The yaw is kept relative to the rig, so a snap turn or teleport carries the
// body with the player instead of leaving it behind.
void vrui__body_update(void)
{
    SfxrPose head = sfxr_head();
    Vector3 f = sfxr_pose_forward(head);
    f.y = 0;
    float rig = sfxr_rig_yaw();
    bool level = Vector3Length(f) > 0.2f;   // looking straight up or down: keep the old yaw
    if (!C.body.init) {
        C.body.yaw_rel = level ? wrap_pi(yaw_of(f) - rig) : 0.0f;
        C.body.init = true;
    } else if (level) {
        float diff = wrap_pi(yaw_of(f) - rig - C.body.yaw_rel);
        const float free_turn = 45.0f * DEG2RAD;
        if (SFXR_BREAK(vrui_body_follows_head)) diff = 0, C.body.yaw_rel = wrap_pi(yaw_of(f) - rig);
        // dragged along past 45 degrees...
        if (diff > free_turn) { C.body.yaw_rel += diff - free_turn; diff = free_turn; }
        else if (diff < -free_turn) { C.body.yaw_rel += diff + free_turn; diff = -free_turn; }
        // ...and drifting round to where you keep looking (time constant 2 s)
        C.body.yaw_rel += diff * fminf(1.0f, sfxr_dt() * 0.5f);
        C.body.yaw_rel = wrap_pi(C.body.yaw_rel);
    }
    Quaternion q = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, C.body.yaw_rel + rig);
    Vector3 back = Vector3RotateByQuaternion((Vector3){ 0, 0, 0.08f }, q);
    Vector3 floor = sfxr_head_floor_point();
    C.body.pose = (SfxrPose){ Vector3Add(floor, back), q };
}

SfxrPose vrui_body(void) { return C.body.pose; }

float vrui_eye_height(void) { return sfxr_head().position.y - sfxr_head_floor_point().y; }

// ---------------------------------------------------------------------------
// Lazy follow
// ---------------------------------------------------------------------------

typedef struct {
    bool init, moving;
    SfxrPose rel;   // where it is, relative to the rig (so rig moves carry it)
} FollowState;
VRUI_STATE_FITS(FollowState);

static SfxrPose rig_pose(void)
{
    if (SFXR_BREAK(vrui_follow_world_space)) return sfxr_pose_identity();
    return (SfxrPose){ sfxr_rig_position(), QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, sfxr_rig_yaw()) };
}

SfxrPose vrui_follow(VruiId id, SfxrPose target, float max_deg, float glide_s)
{
    FollowState *fs = VRUI_STATE(vrui__item(id), FollowState);
    SfxrPose rig = rig_pose();
    if (!fs->init) {
        fs->rel = sfxr_pose_relative(rig, target);
        fs->init = true;
        return target;
    }
    SfxrPose cur = sfxr_pose_mul(rig, fs->rel);
    Vector3 eye = sfxr_head().position;
    Vector3 a = Vector3Subtract(cur.position, eye), b = Vector3Subtract(target.position, eye);
    float angle = Vector3Angle(a, b) * RAD2DEG;
    float depth = fabsf(Vector3Length(a) - Vector3Length(b));
    if (!fs->moving && (angle > max_deg || depth > 0.3f)) fs->moving = true;
    if (fs->moving) {
        // exponential glide: about 95% of the way in glide_s
        float t = 1.0f - expf(-3.0f * sfxr_dt() / fmaxf(glide_s, 1e-3f));
        cur = sfxr_pose_lerp(cur, target, t);
        if (angle < 1.0f && depth < 0.01f) fs->moving = false;
    }
    fs->rel = sfxr_pose_relative(rig, cur);
    return cur;
}

// ---------------------------------------------------------------------------
// Arrows toward things out of view
// ---------------------------------------------------------------------------

bool vrui_offscreen_arrow(Vector3 target, const char *label, Color color)
{
    SfxrPose head = sfxr_head();
    Vector3 local = sfxr_pose_apply_inv(head, target);   // -Z is straight ahead
    float len = Vector3Length(local);
    if (len < 1e-3f) return false;
    if (-local.z / len > cosf(30.0f * DEG2RAD) && !SFXR_BREAK(vrui_arrow_ignores_view)) return false;   // in view: nothing to point at

    // Which way to turn, as a direction across the view (x right, y up).
    // Behind you only "left or right" matters (up or down would point at the
    // sky or your feet), so the arrow is level and on the shorter side.
    Vector2 d = local.z > 0 ? (Vector2){ local.x >= 0 ? 1.0f : -1.0f, 0 } : (Vector2){ local.x, local.y };
    if (Vector2Length(d) < 1e-3f) d = (Vector2){ 1, 0 };
    d = Vector2Normalize(d);
    // two targets the same way: fan the arrows apart so their labels don't
    // print on top of each other
    for (int tries = 0; tries < 8; tries++) {
        bool clash = false;
        for (int i = 0; i < C.narrows; i++) clash |= Vector2DotProduct(d, C.arrow_dir[i]) > cosf(12.0f * DEG2RAD);
        if (!clash) break;
        d = Vector2Rotate(d, (d.x >= 0 ? -14.0f : 14.0f) * DEG2RAD);
    }
    if (C.narrows < 8) C.arrow_dir[C.narrows++] = d;

    const float D = 0.8f, R = D * tanf(20.0f * DEG2RAD);   // a ring 20 degrees off center, 0.8 m out
    Vector3 c = { d.x * R, d.y * R, -D };
    Vector3 dir = { d.x, d.y, 0 }, side = { -d.y, d.x, 0 };
    Vector3 tip = Vector3Add(c, Vector3Scale(dir, 0.025f));
    Vector3 l = Vector3Add(Vector3Add(c, Vector3Scale(side, 0.016f)), Vector3Scale(dir, -0.012f));
    Vector3 r = Vector3Add(Vector3Add(c, Vector3Scale(side, -0.016f)), Vector3Scale(dir, -0.012f));

    vrui_on_top_begin();
    vrui__triangle(sfxr_pose_apply(head, tip), sfxr_pose_apply(head, l), sfxr_pose_apply(head, r), color);
    if (label && *label) {
        Vector3 at = Vector3Add(c, Vector3Scale(dir, -0.035f));
        SfxrPose lp = { sfxr_pose_apply(head, at), head.orientation };
        vrui_text_at(lp, label, 0.0196f, color);
    }
    vrui_on_top_end();
    return true;
}
