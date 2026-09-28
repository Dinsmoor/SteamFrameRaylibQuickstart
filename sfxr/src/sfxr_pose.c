// sfxr_pose.c - pose math, and the rig: where the player's tracking space
// sits in the world (moving, turning and teleporting the player).

#include "sfxr_internal.h"
#include "rlgl.h"

#include <math.h>
#include <string.h>

#define S sfxr_state

// ---------------------------------------------------------------------------
// Pose math
// ---------------------------------------------------------------------------

SfxrPose sfxr_pose_identity(void)
{
    return (SfxrPose){ {0, 0, 0}, {0, 0, 0, 1} };
}

Matrix sfxr_pose_to_matrix(SfxrPose p)
{
    Matrix r = QuaternionToMatrix(p.orientation);
    r.m12 = p.position.x;
    r.m13 = p.position.y;
    r.m14 = p.position.z;
    return r;
}

Vector3 sfxr_pose_forward(SfxrPose p) { return Vector3RotateByQuaternion((Vector3){0, 0, -1}, p.orientation); }
Vector3 sfxr_pose_up(SfxrPose p)      { return Vector3RotateByQuaternion((Vector3){0, 1, 0}, p.orientation); }
Vector3 sfxr_pose_right(SfxrPose p)   { return Vector3RotateByQuaternion((Vector3){1, 0, 0}, p.orientation); }

Vector3 sfxr_pose_apply(SfxrPose p, Vector3 local)
{
    return Vector3Add(p.position, Vector3RotateByQuaternion(local, p.orientation));
}

Vector3 sfxr_pose_apply_inv(SfxrPose p, Vector3 world)
{
    return Vector3RotateByQuaternion(Vector3Subtract(world, p.position), QuaternionInvert(p.orientation));
}

SfxrPose sfxr_pose_mul(SfxrPose parent, SfxrPose child)
{
    SfxrPose r;
    r.orientation = QuaternionNormalize(QuaternionMultiply(parent.orientation, child.orientation));
    r.position = sfxr_pose_apply(parent, child.position);
    return r;
}

SfxrPose sfxr_pose_inverse(SfxrPose p)
{
    SfxrPose r;
    r.orientation = QuaternionInvert(p.orientation);
    r.position = Vector3RotateByQuaternion(Vector3Negate(p.position), r.orientation);
    return r;
}

SfxrPose sfxr_pose_lerp(SfxrPose a, SfxrPose b, float t)
{
    SfxrPose r;
    r.position = Vector3Lerp(a.position, b.position, t);
    r.orientation = QuaternionSlerp(a.orientation, b.orientation, t);
    return r;
}

SfxrPose sfxr_pose_look_at_yaw(Vector3 from, Vector3 target)
{
    Vector3 d = Vector3Subtract(target, from);
    float yaw = atan2f(-d.x, -d.z);   // yaw so that -Z points at target
    SfxrPose p;
    p.position = from;
    p.orientation = QuaternionFromAxisAngle((Vector3){0, 1, 0}, yaw);
    return p;
}

void sfxr_push_pose(SfxrPose p)
{
    rlPushMatrix();
    Matrix m = sfxr_pose_to_matrix(p);
    rlMultMatrixf(MatrixToFloat(m));
}

void sfxr_pop_pose(void) { rlPopMatrix(); }

// ---------------------------------------------------------------------------
// Rig
// ---------------------------------------------------------------------------

SfxrPose sfxr_rig_pose(void)
{
    SfxrPose p;
    p.position = S.rig_pos;
    p.orientation = QuaternionFromAxisAngle((Vector3){0, 1, 0}, S.rig_yaw);
    return p;
}

SfxrPose sfxr_head(void)         { return sfxr_pose_mul(sfxr_rig_pose(), S.head_stage); }
SfxrPose sfxr_eye(int eye)       { return sfxr_pose_mul(sfxr_rig_pose(), S.eye_stage[eye ? 1 : 0]); }
Vector3  sfxr_rig_position(void) { return S.rig_pos; }
float    sfxr_rig_yaw(void)      { return S.rig_yaw; }

void sfxr_rig_set(Vector3 position, float yaw) { S.rig_pos = position; S.rig_yaw = yaw; }
void sfxr_rig_move(Vector3 d)                  { S.rig_pos = Vector3Add(S.rig_pos, d); }

void sfxr_rig_turn(float yaw_delta)
{
    Vector3 head_world = sfxr_head().position;
    S.rig_yaw += yaw_delta;
    Quaternion q = QuaternionFromAxisAngle((Vector3){0, 1, 0}, S.rig_yaw);
    Vector3 off = Vector3RotateByQuaternion(S.head_stage.position, q);
    S.rig_pos.x = head_world.x - off.x;
    S.rig_pos.z = head_world.z - off.z;
}

Vector3 sfxr_head_floor_point(void)
{
    Vector3 h = sfxr_head().position;
    return (Vector3){ h.x, S.rig_pos.y, h.z };
}

void sfxr_rig_teleport(Vector3 target)
{
    Vector3 f = sfxr_head_floor_point();
    S.rig_pos = Vector3Add(S.rig_pos, Vector3Subtract(target, f));
}

Camera3D sfxr_head_camera(void)
{
    SfxrPose h = sfxr_head();
    Camera3D c = {0};
    c.position = h.position;
    c.target = Vector3Add(h.position, sfxr_pose_forward(h));
    c.up = sfxr_pose_up(h);
    c.fovy = 90.0f;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}
