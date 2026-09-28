// sfxr_hand_model.c - a procedural hand skeleton: the 26 OpenXR joints from a
// grip pose, five finger curls and a pinch amount. The test harness builds
// scripted bare hands with it, and the simulator its bare-hand mode.
//
// The layout is an average adult hand. It only needs to be plausible and
// exact about the things sfxr measures: each finger's bend from its first to
// its last bone (curl), the thumb-to-fingertip distances (pinch) and which way
// the palm faces. Curl c bends a finger by c * 50 degrees at each of its three
// joints (so 150 degrees end to end, sfxr's "fully curled"), and the thumb by
// c * 35 degrees at each of two (70 degrees).
//
// Frames: the palm joint's -Y comes out of the palm and -Z points along the
// fingers (OpenXR). The grip is the controller-style grip: -Z along the
// pointing direction, the palm facing -X on the right hand and +X on the
// left, with the palm center 3 cm behind the grip.

#include "sfxr_internal.h"

#include <math.h>
#include <string.h>

// Right hand, palm-local (the left hand mirrors x). Per finger: x, the
// knuckle's z, and three bone lengths.
static const struct { float x, knuckle_z, len[3]; } FINGER[4] = {
    { -0.022f, -0.040f, { 0.040f, 0.025f, 0.020f } },   // index
    {  0.000f, -0.045f, { 0.045f, 0.028f, 0.022f } },   // middle
    {  0.020f, -0.040f, { 0.040f, 0.025f, 0.020f } },   // ring
    {  0.038f, -0.030f, { 0.030f, 0.020f, 0.018f } },   // little
};

static SfxrPose joint_pose(SfxrPose palm, Vector3 local, Quaternion local_q)
{
    return sfxr_pose_mul(palm, (SfxrPose){ local, local_q });
}

void sfxr__hand_model(int hand, SfxrPose grip, const float curl[5], float pinch, SfxrHandJoints *out)
{
    float mx = hand ? 1.0f : -1.0f;   // mirror x for the left hand
    memset(out, 0, sizeof *out);
    out->valid = true;
    out->source = SFXR_SOURCE_HAND;

    SfxrPose palm;
    palm.orientation = QuaternionMultiply(grip.orientation,
                                          QuaternionFromAxisAngle((Vector3){ 0, 0, 1 }, (hand ? -90.0f : 90.0f) * DEG2RAD));
    palm.position = Vector3Add(grip.position, Vector3RotateByQuaternion((Vector3){ 0.03f * mx, 0, 0 }, grip.orientation));

    Quaternion I = QuaternionIdentity();
    out->joint[SFXR_JOINT_PALM] = palm;
    out->joint[SFXR_JOINT_WRIST] = joint_pose(palm, (Vector3){ 0, 0, 0.07f }, I);
    out->radius[SFXR_JOINT_PALM] = out->radius[SFXR_JOINT_WRIST] = 0.02f;

    // fingers: metacarpal straight along -Z, then three bends toward the palm (-Y)
    for (int f = 0; f < 4; f++) {
        int base = SFXR_JOINT_INDEX_METACARPAL + 5 * f;
        float x = FINGER[f].x * mx;
        float a = Clamp(curl[f + 1], 0, 1) * 50.0f * DEG2RAD;
        Vector3 p = { x, 0, 0.035f };
        out->joint[base] = joint_pose(palm, p, I);
        p = (Vector3){ x, 0, FINGER[f].knuckle_z };
        out->joint[base + 1] = joint_pose(palm, p, I);
        for (int k = 0; k < 3; k++) {
            Quaternion q = QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -a * (float)(k + 1));
            Vector3 d = Vector3RotateByQuaternion((Vector3){ 0, 0, -1 }, q);
            p = Vector3Add(p, Vector3Scale(d, FINGER[f].len[k]));
            out->joint[base + 2 + k] = joint_pose(palm, p, q);
        }
        for (int k = 0; k < 5; k++) out->radius[base + k] = k == 4 ? 0.007f : 0.009f;
    }

    // thumb: out and forward from the base of the palm, bending across it
    Vector3 t0 = Vector3Normalize((Vector3){ -0.02f * mx, -0.005f, -0.03f });
    Vector3 across = { 1.0f * mx, -0.5f, 0 };
    Vector3 axis = Vector3Normalize(Vector3CrossProduct(t0, across));
    float a = Clamp(curl[0], 0, 1) * 35.0f * DEG2RAD;
    Vector3 p = { -0.02f * mx, -0.01f, 0.03f };
    const float tl[3] = { 0.036f, 0.030f, 0.025f };
    out->joint[SFXR_JOINT_THUMB_METACARPAL] = joint_pose(palm, p, QuaternionFromVector3ToVector3((Vector3){ 0, 0, -1 }, t0));
    for (int k = 0; k < 3; k++) {
        Quaternion q = QuaternionFromAxisAngle(axis, a * (float)k);
        Vector3 d = Vector3RotateByQuaternion(t0, q);
        p = Vector3Add(p, Vector3Scale(d, tl[k]));
        out->joint[SFXR_JOINT_THUMB_PROXIMAL + k] = joint_pose(palm, p, QuaternionFromVector3ToVector3((Vector3){ 0, 0, -1 }, d));
    }
    for (int k = 0; k < 4; k++) out->radius[SFXR_JOINT_THUMB_METACARPAL + k] = k == 3 ? 0.008f : 0.01f;

    // pinch: the thumb tip goes to the index tip (5 mm short, on the thumb's side)
    if (pinch > 0) {
        Vector3 index_tip = out->joint[SFXR_JOINT_INDEX_TIP].position;
        Vector3 tip = out->joint[SFXR_JOINT_THUMB_TIP].position;
        Vector3 to = Vector3Subtract(tip, index_tip);
        float d = Vector3Length(to);
        Vector3 target = d > 1e-4f ? Vector3Add(index_tip, Vector3Scale(to, 0.005f / d)) : index_tip;
        out->joint[SFXR_JOINT_THUMB_TIP].position = Vector3Lerp(tip, target, Clamp(pinch, 0, 1));
    }
}
