// sfxr_hands.c - bare hands: gestures measured from the hand joints, and
// controller-like input for hands a runtime reports only as joints.
//
// Why measure from joints at all when the runtime's hand-interaction profile
// already gives pinch and grasp values? Three reasons:
//   * some runtimes report joints but no hand-interaction profile, and the
//     hands would otherwise do nothing (sfxr then builds the whole SfxrHand
//     from the joints: see fill_raw_from_joints);
//   * apps want more than two buttons: a second pinch, palm up, palm toward
//     the face;
//   * the thresholds and hysteresis are ours, documented and tested, instead
//     of whatever each runtime chose.
//
// Everything is computed in tracking (stage) space, where the joints arrive,
// and turned into world space at the end like every other pose.

#include "sfxr_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define S sfxr_state

// Distances (m) and angles, see SfxrHandGestures in sfxr.h.
#define PINCH_CLOSE      0.020f
#define PINCH_OPEN       0.035f
#define PINCH_FULL       0.015f   // strength 1
#define PINCH_NONE       0.050f   // strength 0
#define GRASP_CLOSE      0.65f
#define GRASP_OPEN       0.45f
#define FACING_ENTER_DEG 40.0f
#define FACING_EXIT_DEG  55.0f

static void edge(SfxrButton *b, bool now)
{
    b->pressed = now && !b->down;
    b->released = !now && b->down;
    b->down = now;
    b->touched = now;
}

// A button that closes below `close` and opens above `open` (distances).
static bool below_with_hysteresis(bool was, float v, float close, float open)
{
    if (SFXR_BREAK(sfxr_pinch_no_hysteresis)) open = close;
    return was ? v < open : v < close;
}

// Facing test with hysteresis: angle between `a` and `b` (both unit).
static bool facing(bool was, Vector3 a, Vector3 b)
{
    float c = Vector3DotProduct(a, b);
    return c > cosf((was ? FACING_EXIT_DEG : FACING_ENTER_DEG) * DEG2RAD);
}

static SfxrPose look_along(Vector3 origin, Vector3 fwd)
{
    SfxrPose p;
    p.position = origin;
    p.orientation = QuaternionFromVector3ToVector3((Vector3){ 0, 0, -1 }, Vector3Normalize(fwd));
    // keep the ray's roll level: build from forward + world up instead when not vertical
    Vector3 f = Vector3Normalize(fwd);
    if (fabsf(f.y) < 0.98f) {
        Vector3 z = Vector3Negate(f);
        Vector3 x = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, z));
        Vector3 y = Vector3CrossProduct(z, x);
        Matrix m = { x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, 0, 0, 0, 1 };
        p.orientation = QuaternionFromMatrix(m);
    }
    return p;
}

// Where the shoulder probably is (stage space): below and beside the head,
// turned with the head's heading (not its pitch or roll).
static Vector3 shoulder(int hand)
{
    SfxrPose h = S.head_stage;
    Vector3 f = Vector3RotateByQuaternion((Vector3){ 0, 0, -1 }, h.orientation);
    f.y = 0;
    if (Vector3Length(f) < 1e-3f) f = (Vector3){ 0, 0, -1 };
    f = Vector3Normalize(f);
    Vector3 right = { -f.z, 0, f.x };
    float side = hand ? 0.17f : -0.17f;
    return Vector3Add(Vector3Add(h.position, Vector3Scale(right, side)),
                      Vector3Add((Vector3){ 0, -0.25f, 0 }, Vector3Scale(f, -0.05f)));
}

static void measure(int i, const SfxrHandJoints *j, SfxrHandGestures *g)
{
    SfxrHandGestures prev = *g;
    if (!j->valid) {
        memset(g, 0, sizeof *g);
        // let held "buttons" release cleanly
        edge(&g->pinch, false);
        g->pinch.released = prev.pinch.down;
        g->middle_pinch.released = prev.middle_pinch.down;
        g->grasp.released = prev.grasp.down;
        return;
    }
    g->valid = true;
    Vector3 thumb = j->joint[SFXR_JOINT_THUMB_TIP].position;
    for (int f = 0; f < 4; f++)
        g->pinch_dist[f] = Vector3Distance(thumb, j->joint[SFXR_JOINT_INDEX_TIP + 5 * f].position);
    g->pinch_strength = Clamp((PINCH_NONE - g->pinch_dist[0]) / (PINCH_NONE - PINCH_FULL), 0, 1);
    edge(&g->pinch, below_with_hysteresis(prev.pinch.down, g->pinch_dist[0], PINCH_CLOSE, PINCH_OPEN));
    edge(&g->middle_pinch, below_with_hysteresis(prev.middle_pinch.down, g->pinch_dist[1], PINCH_CLOSE, PINCH_OPEN));

    float c = (sfxr__finger_curl(j, SFXR_FINGER_MIDDLE) + sfxr__finger_curl(j, SFXR_FINGER_RING) +
               sfxr__finger_curl(j, SFXR_FINGER_LITTLE)) / 3.0f;
    g->grasp_strength = Clamp((c - 0.2f) / 0.6f, 0, 1);
    edge(&g->grasp, prev.grasp.down ? c > GRASP_OPEN : c > GRASP_CLOSE);

    // OpenXR joints: -Y comes out of the palm
    const SfxrPose *palm = &j->joint[SFXR_JOINT_PALM];
    Vector3 n = Vector3RotateByQuaternion((Vector3){ 0, SFXR_BREAK(sfxr_palm_normal_flipped) ? 1.0f : -1.0f, 0 },
                                          palm->orientation);
    g->palm_normal = n;
    g->palm_up = facing(prev.palm_up, n, (Vector3){ 0, 1, 0 });
    Vector3 to_head = Vector3Normalize(Vector3Subtract(S.head_stage.position, palm->position));
    g->palm_to_head = facing(prev.palm_to_head, n, to_head);

    Vector3 knuckle = j->joint[SFXR_JOINT_INDEX_PROXIMAL].position;
    g->ray = look_along(knuckle, Vector3Subtract(knuckle, shoulder(i)));
    g->index_tip = j->joint[SFXR_JOINT_INDEX_TIP].position;
    g->thumb_tip = thumb;
}

// A hand the runtime reports only as joints: build the controller-like raw
// input from the gestures (stage space), so vrui and apps just work.
static void fill_raw_from_joints(int i, const SfxrHandJoints *j, const SfxrHandGestures *g)
{
    SfxrRawHand *r = &S.raw[i];
    memset(r, 0, sizeof *r);
    r->active = true;
    r->source = SFXR_SOURCE_HAND;
    r->pose_valid = RAW_POSE_GRIP | RAW_POSE_AIM | RAW_POSE_POKE | RAW_POSE_PINCH | RAW_POSE_PALM;
    SfxrPose palm = j->joint[SFXR_JOINT_PALM];
    r->palm = palm;
    // The grip is where a held handle would sit: 3 cm out of the palm, oriented
    // like a controller's grip (-Z along the pointing direction, palm toward
    // -X on the right hand and +X on the left).
    r->grip.position = Vector3Add(palm.position, Vector3Scale(g->palm_normal, 0.03f));
    r->grip.orientation = QuaternionMultiply(palm.orientation,
                                             QuaternionFromAxisAngle((Vector3){ 0, 0, 1 }, (i ? 90.0f : -90.0f) * DEG2RAD));
    r->aim = g->ray;
    r->poke = j->joint[SFXR_JOINT_INDEX_TIP];
    r->pinch = (SfxrPose){ Vector3Lerp(g->thumb_tip, g->index_tip, 0.5f), g->ray.orientation };
    r->trigger = g->pinch_strength;
    r->squeeze = g->grasp_strength;
    if (S.joint_hand_had_prev[i] && S.dt > 0) {
        r->velocity = Vector3Scale(Vector3Subtract(r->grip.position, S.joint_hand_prev[i]), 1.0f / S.dt);
        r->has_velocity = true;
    }
    S.joint_hand_prev[i] = r->grip.position;
    S.joint_hand_had_prev[i] = true;
    snprintf(r->profile, sizeof r->profile, "joints (no hand-interaction profile)");
}

void sfxr__hands_update(void)
{
    static int force = -1;   // SFXR_HANDS=joints: always build bare hands from the joints
    if (force < 0) {
        const char *e = sfxr_env_str("SFXR_HANDS");
        force = e && !strcmp(e, "joints");
        if (force) SFXR_LOG("SFXR_HANDS=joints: bare hands built from the joints, not the runtime's hand profile");
    }
    SfxrPose rig = sfxr_rig_pose();
    for (int i = 0; i < 2; i++) {
        const SfxrHandJoints *j = &S.sig.joints[i];
        SfxrHandGestures *g = &S.gestures[i];
        measure(i, j, g);

        bool bare = j->valid && j->source == SFXR_SOURCE_HAND;
        bool runtime_hand = S.raw[i].active && S.raw[i].source == SFXR_SOURCE_HAND;
        if (bare && ((!S.raw[i].active && !SFXR_BREAK(sfxr_no_joint_hands)) || (force && runtime_hand)))
            fill_raw_from_joints(i, j, g);
        else
            S.joint_hand_had_prev[i] = false;

        // to world space
        if (g->valid) {
            g->palm_normal = Vector3RotateByQuaternion(g->palm_normal, rig.orientation);
            g->ray = sfxr_pose_mul(rig, g->ray);
            g->index_tip = sfxr_pose_apply(rig, g->index_tip);
            g->thumb_tip = sfxr_pose_apply(rig, g->thumb_tip);
        }
    }
}

const SfxrHandGestures *sfxr_hand_gestures(SfxrHandId hand) { return &S.gestures[hand == SFXR_RIGHT ? 1 : 0]; }
