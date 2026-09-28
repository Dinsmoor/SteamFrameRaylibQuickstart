// garden_rigidbody.c - a small box rigid body: just enough physics to knock
// a chair over or throw a hammer (garden.h).
//
//   * semi-implicit Euler for position and spin;
//   * a solid-box inertia tensor, so a tall thin thing tips easily one way
//     and stubbornly the other;
//   * contact per corner against a flat floor, with a little bounce and
//     friction, velocity first and then a gentle position fix (so a box
//     resting on four corners settles instead of buzzing);
//   * sleep once it has stopped, so settled props cost nothing.
//
// Box-only and floor-only on purpose: prop-against-prop contact needs a
// broadphase and a contact solver, overkill for "the chair tips when you
// walk into it", and a box on a floor is easy to predict.

#include "garden.h"

#define LINEAR_DAMP   0.10f   // fraction of speed bled off per second
#define ANGULAR_DAMP  0.35f   // spin settles faster, so it doesn't spin on
#define RESTITUTION   0.18f   // corner bounce
#define REST_VEL      0.7f    // contacts slower than this don't bounce (m/s)
#define FRICTION      0.55f
#define SLEEP_LIN     0.035f
#define SLEEP_ANG     0.10f
#define SLEEP_TIME    0.40f
#define MAX_ANG_VEL   16.0f   // so a hard hit can't make it explode

// World-space inverse inertia applied to w: into body space, scale, back out.
static Vector3 inv_inertia(const RigidBody *rb, Vector3 w)
{
    Vector3 b = Vector3RotateByQuaternion(w, QuaternionInvert(rb->orient));
    b = (Vector3){ b.x * rb->inv_inertia.x, b.y * rb->inv_inertia.y, b.z * rb->inv_inertia.z };
    return Vector3RotateByQuaternion(b, rb->orient);
}

void rb_init(RigidBody *rb, Vector3 center, Vector3 half, Quaternion orient, float mass)
{
    if (mass <= 0) mass = 1;
    *rb = (RigidBody){ 0 };
    rb->half = half;
    rb->pos = center;
    rb->orient = orient;
    rb->mass = mass;
    rb->inv_mass = 1.0f / mass;
    // solid box: I = m/12 (a^2 + b^2), a and b the full sides across that axis
    float w = 2 * half.x, h = 2 * half.y, d = 2 * half.z, k = mass / 12.0f;
    float ix = k * (h * h + d * d), iy = k * (w * w + d * d), iz = k * (w * w + h * h);
    rb->inv_inertia = (Vector3){ ix > 0 ? 1 / ix : 0, iy > 0 ? 1 / iy : 0, iz > 0 ? 1 / iz : 0 };
}

void rb_apply_impulse(RigidBody *rb, Vector3 imp, Vector3 at)
{
    rb->vel = Vector3Add(rb->vel, Vector3Scale(imp, rb->inv_mass));
    Vector3 r = Vector3Subtract(at, rb->pos);
    rb->ang_vel = Vector3Add(rb->ang_vel, inv_inertia(rb, Vector3CrossProduct(r, imp)));
    rb->asleep = false;
    rb->rest_timer = 0;
}

Vector3 rb_point_velocity(const RigidBody *rb, Vector3 at)
{
    return Vector3Add(rb->vel, Vector3CrossProduct(rb->ang_vel, Vector3Subtract(at, rb->pos)));
}

static void corners(const RigidBody *rb, Vector3 out[8])
{
    int n = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                out[n++] = Vector3Add(rb->pos, Vector3RotateByQuaternion((Vector3){ sx * rb->half.x, sy * rb->half.y, sz * rb->half.z }, rb->orient));
}

// One corner below the floor: an impulse that stops it going further in
// (with a bounce only for a real impact), then friction along the floor.
static void resolve_corner(RigidBody *rb, Vector3 c, float floor_y)
{
    if (c.y >= floor_y) return;
    const Vector3 N = { 0, 1, 0 };
    Vector3 r = Vector3Subtract(c, rb->pos);
    Vector3 vp = rb_point_velocity(rb, c);
    float vn = vp.y;
    if (vn >= 0) return;   // already leaving the floor
    float e = -vn > REST_VEL ? RESTITUTION : 0.0f;
    Vector3 term = Vector3CrossProduct(inv_inertia(rb, Vector3CrossProduct(r, N)), r);
    float denom = rb->inv_mass + Vector3DotProduct(N, term);
    float jn = -(1 + e) * vn / fmaxf(denom, 1e-6f);
    Vector3 J = Vector3Scale(N, jn);
    rb->vel = Vector3Add(rb->vel, Vector3Scale(J, rb->inv_mass));
    rb->ang_vel = Vector3Add(rb->ang_vel, inv_inertia(rb, Vector3CrossProduct(r, J)));

    vp = rb_point_velocity(rb, c);
    Vector3 vt = { vp.x, 0, vp.z };
    float vtl = Vector3Length(vt);
    if (vtl > 1e-4f) {
        Vector3 T = Vector3Scale(vt, 1 / vtl);
        Vector3 termt = Vector3CrossProduct(inv_inertia(rb, Vector3CrossProduct(r, T)), r);
        float jt = -vtl / fmaxf(rb->inv_mass + Vector3DotProduct(T, termt), 1e-6f);
        float maxjt = FRICTION * jn;   // Coulomb: friction can't beat the push
        jt = Clamp(jt, -maxjt, maxjt);
        Vector3 Jt = Vector3Scale(T, jt);
        rb->vel = Vector3Add(rb->vel, Vector3Scale(Jt, rb->inv_mass));
        rb->ang_vel = Vector3Add(rb->ang_vel, inv_inertia(rb, Vector3CrossProduct(r, Jt)));
    }
}

void rb_step(RigidBody *rb, float dt, float gravity, float floor_y)
{
    if (rb->asleep || dt <= 0) return;
    rb->vel.y -= gravity * dt;
    rb->vel = Vector3Scale(rb->vel, fmaxf(0, 1 - LINEAR_DAMP * dt));
    rb->ang_vel = Vector3Scale(rb->ang_vel, fmaxf(0, 1 - ANGULAR_DAMP * dt));
    float al = Vector3Length(rb->ang_vel);
    if (al > MAX_ANG_VEL) rb->ang_vel = Vector3Scale(rb->ang_vel, MAX_ANG_VEL / al);

    rb->pos = Vector3Add(rb->pos, Vector3Scale(rb->vel, dt));
    Quaternion wq = { rb->ang_vel.x, rb->ang_vel.y, rb->ang_vel.z, 0 };
    Quaternion dq = QuaternionMultiply(wq, rb->orient);
    rb->orient = QuaternionNormalize((Quaternion){ rb->orient.x + 0.5f * dq.x * dt, rb->orient.y + 0.5f * dq.y * dt,
                                                   rb->orient.z + 0.5f * dq.z * dt, rb->orient.w + 0.5f * dq.w * dt });

    // Several passes over the corners converge a face resting on four of them.
    for (int pass = 0; pass < 4; pass++) {
        Vector3 c[8];
        corners(rb, c);
        for (int i = 0; i < 8; i++) resolve_corner(rb, c[i], floor_y);
    }
    // Then lift it out of the floor in position only (a split impulse), so a
    // resting box doesn't carry a phantom upward speed.
    Vector3 c[8];
    corners(rb, c);
    float deepest = 0;
    for (int i = 0; i < 8; i++) deepest = fmaxf(deepest, floor_y - c[i].y);
    if (deepest > 0.003f) rb->pos.y += (deepest - 0.003f) * 0.4f;

    if (Vector3Length(rb->vel) < SLEEP_LIN && Vector3Length(rb->ang_vel) < SLEEP_ANG) {
        rb->rest_timer += dt;
        if (rb->rest_timer >= SLEEP_TIME) {
            rb->asleep = true;
            rb->vel = rb->ang_vel = (Vector3){ 0 };
        }
    } else {
        rb->rest_timer = 0;
    }
}

SfxrPose rb_pose(const RigidBody *rb) { return (SfxrPose){ rb->pos, rb->orient }; }
