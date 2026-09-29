// vrui_collide.c - what's solid, and what a ray or a moving ball hits
// (vrui.h section 15). Deliberately small: a list of shapes rebuilt every
// frame, and three queries against it. No physics engine: callers decide what
// a hit means (bounce, stop, land, hurt).
//
// Every query is the same one underneath: a SPHERE CAST, a ball of radius R
// moving along a straight line. A ray is a cast with R = 0. Against each
// shape the trick is to grow the shape by R and shrink the ball to a point:
// a ball touching a box is its center touching the box made R bigger. Then
// each test is "where does a line enter this shape" -- the first entry wins.

#include "vrui_internal.h"
#include "sfxr_break.h"

#define MAX_COLLIDERS 2048

typedef enum { COL_BOX, COL_SPHERE, COL_CAPSULE, COL_GROUND } ColKind;
typedef struct {
    ColKind kind;
    SfxrPose pose;          // box
    Vector3 half;           // box
    Vector3 a, b;           // sphere center = a; capsule axis a..b; ground: a.y
    float r;
    int tag;
    Vector3 center;         // a sphere round all of it (a quick "can't be near" test)
    float bound;            // (< 0: unbounded, the ground)
} Collider;

// Two lists: this frame's, being declared, and last frame's, complete, which
// queries use.
static Collider cur[MAX_COLLIDERS], last[MAX_COLLIDERS];
static int ncur, nlast;

void vrui__collide_frame(void)
{
    memcpy(last, cur, sizeof cur[0] * (size_t)ncur);
    nlast = ncur;
    ncur = 0;
}

int vrui_collider_count(void) { return nlast; }

static Collider *add(ColKind kind, int tag)
{
    if (ncur >= MAX_COLLIDERS) return NULL;
    Collider *c = &cur[ncur++];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->tag = tag;
    return c;
}

void vrui_collider_box(SfxrPose pose, Vector3 size, int tag)
{
    Collider *c = add(COL_BOX, tag);
    if (!c) return;
    c->pose = pose;
    c->half = Vector3Scale(size, 0.5f);
    c->center = pose.position;
    c->bound = Vector3Length(c->half);
}

void vrui_collider_sphere(Vector3 center, float radius, int tag)
{
    Collider *c = add(COL_SPHERE, tag);
    if (!c) return;
    c->a = c->center = center;
    c->r = c->bound = radius;
}

void vrui_collider_capsule(Vector3 a, Vector3 b, float radius, int tag)
{
    Collider *c = add(COL_CAPSULE, tag);
    if (!c) return;
    c->a = a;
    c->b = b;
    c->r = radius;
    c->center = Vector3Lerp(a, b, 0.5f);
    c->bound = Vector3Distance(a, b) * 0.5f + radius;
}

void vrui_collider_ground(float y, int tag)
{
    Collider *c = add(COL_GROUND, tag);
    if (!c) return;
    c->a.y = y;
    c->bound = -1;
}

// --- where a line o + t d (d unit, 0 <= t <= len) first enters each shape, grown by R

// A ball of radius r at c. |o + t d - c| = r is a quadratic in t.
static bool enter_sphere(Vector3 o, Vector3 d, Vector3 c, float r, float *t, Vector3 *n)
{
    Vector3 m = Vector3Subtract(o, c);
    float b = Vector3DotProduct(m, d), cc = Vector3DotProduct(m, m) - r * r;
    if (cc < 0) return false;                 // starts inside: ignored
    float disc = b * b - cc;
    if (b > 0 || disc < 0) return false;      // moving away, or passing by
    *t = -b - sqrtf(disc);
    *n = Vector3Normalize(Vector3Add(m, Vector3Scale(d, *t)));
    return true;
}

// A box: in its own frame it's three pairs of flat "slabs"; the line is inside
// the box where it's inside all three at once. It enters at the last of the
// three entries (if that's before the first exit).
static bool enter_box(Vector3 o, Vector3 d, const Collider *c, float R, float *t, Vector3 *n)
{
    Vector3 lo = sfxr_pose_apply_inv(c->pose, o);
    Vector3 ld = Vector3RotateByQuaternion(d, QuaternionInvert(c->pose.orientation));
    float ov[3] = { lo.x, lo.y, lo.z }, dv[3] = { ld.x, ld.y, ld.z };
    float hv[3] = { c->half.x + R, c->half.y + R, c->half.z + R };
    float tin = -1e30f, tout = 1e30f;
    int axis = -1;
    for (int k = 0; k < 3; k++) {
        if (fabsf(dv[k]) < 1e-9f) {
            if (ov[k] < -hv[k] || ov[k] > hv[k]) return false;   // parallel, outside this slab
            continue;
        }
        float t1 = (-hv[k] - ov[k]) / dv[k], t2 = (hv[k] - ov[k]) / dv[k];
        if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
        if (t1 > tin) { tin = t1; axis = k; }
        if (t2 < tout) tout = t2;
    }
    if (tin > tout || tout < 0 || tin < 0 || axis < 0) return false;   // misses, behind, or starts inside
    Vector3 ln = { 0 };
    float s = dv[axis] > 0 ? -1.0f : 1.0f;   // the face it came in through faces back at it
    if (axis == 0) ln.x = s; else if (axis == 1) ln.y = s; else ln.z = s;
    *t = tin;
    *n = Vector3RotateByQuaternion(ln, c->pose.orientation);
    return true;
}

// A capsule: a cylinder round the axis a..b (entered from the side), capped
// by two balls.
static bool enter_capsule(Vector3 o, Vector3 d, const Collider *c, float R, float *t, Vector3 *n)
{
    float r = c->r + R, best = 1e30f;
    Vector3 bn = { 0 };
    Vector3 ax = Vector3Subtract(c->b, c->a);
    float len = Vector3Length(ax);
    if (len > 1e-6f) {
        Vector3 u = Vector3Scale(ax, 1.0f / len);
        // the parts of o and d square to the axis: a 2D circle test
        Vector3 m = Vector3Subtract(o, c->a);
        Vector3 mp = Vector3Subtract(m, Vector3Scale(u, Vector3DotProduct(m, u)));
        Vector3 dp = Vector3Subtract(d, Vector3Scale(u, Vector3DotProduct(d, u)));
        float A = Vector3DotProduct(dp, dp), B = Vector3DotProduct(mp, dp), C = Vector3DotProduct(mp, mp) - r * r;
        if (A > 1e-9f && C >= 0 && B < 0 && B * B - A * C >= 0) {
            float tt = (-B - sqrtf(B * B - A * C)) / A;
            float along = Vector3DotProduct(Vector3Add(m, Vector3Scale(d, tt)), u);
            if (along >= 0 && along <= len) {
                best = tt;
                bn = Vector3Normalize(Vector3Add(mp, Vector3Scale(dp, tt)));
            }
        }
    }
    float ts;
    Vector3 ns;
    if (enter_sphere(o, d, c->a, r, &ts, &ns) && ts < best) { best = ts; bn = ns; }
    if (enter_sphere(o, d, c->b, r, &ts, &ns) && ts < best) { best = ts; bn = ns; }
    if (best > 1e29f) return false;
    *t = best;
    *n = bn;
    return true;
}

static VruiHit cast(Vector3 o, Vector3 d, float len, float R)
{
    VruiHit h = { 0 };
    h.distance = len;
    for (int i = 0; i < nlast; i++) {
        const Collider *c = &last[i];
        // quick reject: the line never comes within bound + R of its middle
        if (c->bound >= 0) {
            Vector3 m = Vector3Subtract(c->center, o);
            float along = Clamp(Vector3DotProduct(m, d), 0, len);
            if (Vector3Distance(c->center, Vector3Add(o, Vector3Scale(d, along))) > c->bound + R) continue;
        }
        float t = 0;
        Vector3 n = { 0 };
        bool in = false;
        switch (c->kind) {
        case COL_BOX:     in = enter_box(o, d, c, R, &t, &n); break;
        case COL_SPHERE:  in = enter_sphere(o, d, c->a, c->r + R, &t, &n); break;
        case COL_CAPSULE: in = enter_capsule(o, d, c, R, &t, &n); break;
        case COL_GROUND:
            // the level floor, from above only (a ball resting on it, touching, counts as above)
            if (d.y < 0 && o.y >= c->a.y + R - 1e-4f) { t = (c->a.y + R - o.y) / d.y; n = (Vector3){ 0, 1, 0 }; in = true; }
            break;
        }
        if (in && t >= 0 && t <= h.distance && (!h.hit || t < h.distance)) {
            h.hit = true;
            h.distance = t;
            h.normal = n;
            h.tag = c->tag;
        }
    }
    if (h.hit) {
        h.center = Vector3Add(o, Vector3Scale(d, h.distance));
        h.point = Vector3Subtract(h.center, Vector3Scale(h.normal, R));
    }
    return h;
}

VruiHit vrui_raycast(Vector3 from, Vector3 dir, float max_distance)
{
    float l = Vector3Length(dir);
    if (l < 1e-9f) return (VruiHit){ 0 };
    return cast(from, Vector3Scale(dir, 1.0f / l), max_distance, 0);
}

VruiHit vrui_spherecast(Vector3 from, Vector3 to, float radius)
{
    Vector3 d = Vector3Subtract(to, from);
    float len = Vector3Length(d);
    if (len < 1e-9f) return (VruiHit){ 0 };
    if (SFXR_BREAK(vrui_spherecast_endpoint_only)) {
        // (the broken way: only look near where it ends up -- a fast thing
        // skips clean over anything thinner than one frame's travel)
        return cast(Vector3Subtract(to, Vector3Scale(d, 0.01f / len)), Vector3Scale(d, 1.0f / len), 0.01f, radius);
    }
    return cast(from, Vector3Scale(d, 1.0f / len), len, radius);
}

float vrui_ground(Vector3 above, int *tag)
{
    VruiHit h = cast(above, (Vector3){ 0, -1, 0 }, 1000.0f, 0);
    if (tag) *tag = h.hit ? h.tag : -1;
    return h.hit ? h.point.y : 0.0f;
}
