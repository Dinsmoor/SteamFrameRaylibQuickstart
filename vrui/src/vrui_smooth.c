// vrui_smooth.c - interpolation and smoothing: the small tools that make
// motion feel like something, and one pose smoother built from them that
// held things (a weapon, a tool, a HUD) can follow with (vrui.h section 12,
// docs/SMOOTHING.md).
//
// Everything is frame-rate independent: the same settings look the same at
// 72, 90 or 144 Hz. The usual "pose = lerp(pose, target, 0.1)" every frame
// is not: at 144 Hz it catches up twice as fast as at 72.

#include "vrui_internal.h"

// --- primitives ---------------------------------------------------------------

float vrui_damp(float current, float target, float halflife, float dt)
{
    if (halflife <= 0) return target;
    return Lerp(current, target, 1.0f - exp2f(-dt / halflife));
}

Vector3 vrui_damp3(Vector3 current, Vector3 target, float halflife, float dt)
{
    if (halflife <= 0) return target;
    return Vector3Lerp(current, target, 1.0f - exp2f(-dt / halflife));
}

// Slerp the short way round (q and -q are the same rotation).
static Quaternion slerp_short(Quaternion a, Quaternion b, float t)
{
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = (Quaternion){ -b.x, -b.y, -b.z, -b.w };
    return QuaternionSlerp(a, b, t);
}

Quaternion vrui_dampq(Quaternion current, Quaternion target, float halflife, float dt)
{
    if (halflife <= 0) return target;
    return slerp_short(current, target, 1.0f - exp2f(-dt / halflife));
}

// A spring pulls toward the target, damping resists the speed. frequency is
// how fast it swings (Hz); damping 1 settles as fast as possible without
// overshooting, below 1 it overshoots and wobbles, above 1 it creeps.
void vrui_spring(float *x, float *v, float target, float frequency_hz, float damping, float dt)
{
    float w = 2.0f * PI * frequency_hz;
    // semi-implicit Euler, substepped so stiff springs stay stable at 72 Hz
    int n = (int)ceilf(dt * w / 0.5f);
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    float h = dt / (float)n;
    for (int i = 0; i < n; i++) {
        *v += (-2.0f * damping * w * *v - w * w * (*x - target)) * h;
        *x += *v * h;
    }
}

void vrui_spring3(Vector3 *x, Vector3 *v, Vector3 target, float frequency_hz, float damping, float dt)
{
    vrui_spring(&x->x, &v->x, target.x, frequency_hz, damping, dt);
    vrui_spring(&x->y, &v->y, target.y, frequency_hz, damping, dt);
    vrui_spring(&x->z, &v->z, target.z, frequency_hz, damping, dt);
}

// The rotation that takes a to b, as an axis * angle vector (radians).
static Vector3 rotation_between(Quaternion a, Quaternion b)
{
    Quaternion d = QuaternionMultiply(b, QuaternionInvert(a));
    if (d.w < 0) d = (Quaternion){ -d.x, -d.y, -d.z, -d.w };
    float s = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (s < 1e-6f) return (Vector3){ 0 };
    float angle = 2.0f * atan2f(s, d.w);
    return Vector3Scale((Vector3){ d.x, d.y, d.z }, angle / s);
}

static Quaternion rotate_by(Quaternion q, Vector3 w, float dt)   // turn q by angular velocity w for dt
{
    float a = Vector3Length(w) * dt;
    if (a < 1e-7f) return q;
    return QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(Vector3Normalize(w), a), q));
}

void vrui_springq(Quaternion *q, Vector3 *w, Quaternion target, float frequency_hz, float damping, float dt)
{
    float om = 2.0f * PI * frequency_hz;
    int n = (int)ceilf(dt * om / 0.5f);
    if (n < 1) n = 1;
    float h = dt / (float)n;
    for (int i = 0; i < n; i++) {
        Vector3 err = rotation_between(*q, target);
        *w = Vector3Add(*w, Vector3Scale(Vector3Subtract(Vector3Scale(err, om * om), Vector3Scale(*w, 2.0f * damping * om)), h));
        *q = rotate_by(*q, *w, h);
    }
}

Vector3 vrui_move_toward3(Vector3 current, Vector3 target, float max_step)
{
    Vector3 d = Vector3Subtract(target, current);
    float l = Vector3Length(d);
    return l <= max_step || l < 1e-9f ? target : Vector3Add(current, Vector3Scale(d, max_step / l));
}

Quaternion vrui_turn_toward(Quaternion current, Quaternion target, float max_radians)
{
    Vector3 r = rotation_between(current, target);
    float a = Vector3Length(r);
    if (a <= max_radians) return target;
    return rotate_by(current, r, max_radians / a);
}

float vrui_ease(VruiEase e, float t)
{
    t = Clamp(t, 0, 1);
    switch (e) {
    case VRUI_EASE_SMOOTH:  return t * t * (3 - 2 * t);
    case VRUI_EASE_IN:      return t * t * t;
    case VRUI_EASE_OUT:     { float u = 1 - t; return 1 - u * u * u; }
    case VRUI_EASE_IN_OUT:  return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
    case VRUI_EASE_BACK:    { float c = 1.70158f, u = t - 1; return 1 + (c + 1) * u * u * u + c * u * u; }   // overshoots, settles
    case VRUI_EASE_ELASTIC: return t <= 0 || t >= 1 ? t : powf(2, -10 * t) * sinf((t * 10 - 0.75f) * (2 * PI / 3)) + 1;
    case VRUI_EASE_BOUNCE: {
        const float n = 7.5625f, d = 2.75f;
        if (t < 1 / d) return n * t * t;
        if (t < 2 / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
        if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
        t -= 2.625f / d; return n * t * t + 0.984375f;
    }
    default:                return t;
    }
}

// --- the jitter filter (the "1 euro filter", Casiez et al. 2012) --------------
// A low-pass filter whose cutoff rises with speed: when the thing is nearly
// still it smooths hard (the tremble goes), when it moves fast it barely
// smooths (no lag where lag would be felt).

static float alpha_for(float cutoff_hz, float dt)
{
    float tau = 1.0f / (2.0f * PI * cutoff_hz);
    return 1.0f / (1.0f + tau / dt);
}

// --- the pose smoother ----------------------------------------------------------

const char *vrui_smooth_name(VruiSmoothMode m)
{
    static const char *const N[VRUI_SMOOTH_COUNT] = { "Snap", "Lag", "Spring", "Heavy", "Steady" };
    return (unsigned)m < VRUI_SMOOTH_COUNT ? N[m] : "?";
}

VruiSmoothSpec vrui_smooth_spec(VruiSmoothMode mode)
{
    VruiSmoothSpec s = { 0 };
    s.mode = mode;
    s.halflife = 0.05f;       // Lag: half the gap closes every 50 ms
    s.frequency = 3.0f;       // Spring: swings three times a second...
    s.damping = 0.35f;        // ...and overshoots a fair bit
    s.max_speed = 3.0f;       // Heavy: no faster than 3 m/s...
    s.max_turn_deg = 300.0f;  // ...or 300 degrees a second, and it takes a moment to get going
    s.min_cutoff = 1.0f;      // Steady: smooth hard below about 1 Hz of motion...
    s.beta = 6.0f;            // ...and let fast motion straight through
    s.with_player = true;
    return s;
}

static SfxrPose rig_pose(void)
{
    return (SfxrPose){ sfxr_rig_position(), QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, sfxr_rig_yaw()) };
}

void vrui_smooth_reset(VruiSmooth *s, SfxrPose pose)
{
    memset(s, 0, sizeof *s);
    s->pose = s->raw = pose;
    s->rig = rig_pose();
    s->init = true;
}

SfxrPose vrui_smooth_pose(VruiSmooth *s, SfxrPose target, const VruiSmoothSpec *sp)
{
    float dt = sfxr_dt();
    if (!s->init || sp->mode == VRUI_SMOOTH_SNAP || dt <= 0) {
        vrui_smooth_reset(s, target);
        return target;
    }
    // Held things go with the player: when the rig moves (a teleport, stick
    // walking, a snap turn), everything the smoother remembers moves with it,
    // so only the HAND's motion is smoothed, not the jump across the world.
    SfxrPose rig = rig_pose();
    if (sp->with_player && !SFXR_BREAK(vrui_smooth_world_space)) {
        SfxrPose delta = sfxr_pose_mul(rig, sfxr_pose_inverse(s->rig));
        s->pose = sfxr_pose_mul(delta, s->pose);
        s->raw = sfxr_pose_mul(delta, s->raw);
        s->vel = Vector3RotateByQuaternion(s->vel, delta.orientation);
        s->ang_vel = Vector3RotateByQuaternion(s->ang_vel, delta.orientation);
    }
    s->rig = rig;

    SfxrPose p = s->pose;
    switch (sp->mode) {
    case VRUI_SMOOTH_LAG:
        p.position = vrui_damp3(p.position, target.position, sp->halflife, dt);
        p.orientation = vrui_dampq(p.orientation, target.orientation, sp->halflife, dt);
        break;
    case VRUI_SMOOTH_SPRING:
        vrui_spring3(&p.position, &s->vel, target.position, sp->frequency, sp->damping, dt);
        vrui_springq(&p.orientation, &s->ang_vel, target.orientation, sp->frequency, sp->damping, dt);
        break;
    case VRUI_SMOOTH_HEAVY: {
        // a firm, critically damped pull, but speed-limited: a flick can't
        // whip it round, a steady swing carries it with you
        vrui_spring3(&p.position, &s->vel, target.position, 5.0f, 1.0f, dt);
        float v = Vector3Length(s->vel);
        if (v > sp->max_speed) { s->vel = Vector3Scale(s->vel, sp->max_speed / v); }
        p.position = vrui_move_toward3(s->pose.position, p.position, sp->max_speed * dt);
        vrui_springq(&p.orientation, &s->ang_vel, target.orientation, 5.0f, 1.0f, dt);
        float w = Vector3Length(s->ang_vel), wmax = sp->max_turn_deg * DEG2RAD;
        if (w > wmax) s->ang_vel = Vector3Scale(s->ang_vel, wmax / w);
        p.orientation = vrui_turn_toward(s->pose.orientation, p.orientation, wmax * dt);
        break;
    }
    case VRUI_SMOOTH_STEADY: {
        // how fast the raw pose is moving, itself lightly smoothed...
        Vector3 dx = Vector3Scale(Vector3Subtract(target.position, s->raw.position), 1.0f / dt);
        float dr = Vector3Length(rotation_between(s->raw.orientation, target.orientation)) / dt;
        s->vel = Vector3Lerp(s->vel, dx, alpha_for(1.0f, dt));
        s->speed_r = Lerp(s->speed_r, dr, alpha_for(1.0f, dt));
        // ...sets how hard to smooth: hard when still, hardly at all when fast
        float ap = alpha_for(sp->min_cutoff + sp->beta * Vector3Length(s->vel), dt);
        float ar = alpha_for(sp->min_cutoff + sp->beta * 0.3f * s->speed_r, dt);   // 0.3 m per radian: a hand-sized lever
        p.position = Vector3Lerp(p.position, target.position, ap);
        p.orientation = slerp_short(p.orientation, target.orientation, ar);
        break;
    }
    default: p = target; break;
    }
    s->raw = target;
    s->pose = p;
    return p;
}
