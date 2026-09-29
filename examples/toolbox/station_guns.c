// station_guns.c - two tools for moving things from afar, on a stand beside
// the Weights table (docs/WIELDING.md, "Physics guns"). They work on the
// Weights station's things, so you can feel what weight does to each.
//
//   PHYSGUN (blue, Garry's Mod)   aim at a thing and hold the trigger: it
//            hangs on the end of the beam where you caught it. Swing it
//            around; twist your wrist to turn it; push the stick forward or
//            back to reel it out or in. Let go and it flies on with the swing
//            it had. Press A while holding it to FREEZE it where it is, in
//            mid-air; catch it with the beam again to unfreeze it. The beam
//            bends when the thing lags: heavy things lag, but anything moves.
//   GRAVITY GUN (orange, Half-Life 2)   A pulls the thing you aim at into
//            the air in front of the gun and holds it there; A again drops
//            it. The trigger PUNTS: the held thing, or whatever you aim at
//            within 5 m. It pushes everything equally hard, so light things
//            fly and heavy ones barely move, and it can't lift an anvil (it
//            only tugs it).
//
// Pick a gun up with the grip, the way you pick anything up: it snaps into
// your hand pointing where your laser would. Let go of the grip and it goes
// back to its stand. A hand holding a gun has no laser (the gun is its
// pointer) and its buttons belong to the gun (vrui_claim_input), so the stick
// doesn't turn or teleport you while you reel.
//
// The things are the Weights station's (weights_thing). The guns move them
// with vrui_wield_set_motion: while a gun has one it sets the pose itself,
// and when it lets go, the thing's own falling, bouncing and settling take
// over with the velocity it had.

#include "toolbox.h"
#include "sfxr_break.h"

#define BREAK SFXR_BREAK_DECLARE
#include "toolbox_breaks.def"
#undef BREAK

#include <string.h>

#define STAND_X  -20.1f          // left of the Weights table
#define STAND_H  1.0f
#define PHYS_RANGE  15.0f
#define GRAV_RANGE  8.0f
#define PUNT_RANGE  5.0f
#define GRAV_LIFT_KG 30.0f      // heavier than this: the gravity gun only tugs it
#define PUNT_IMPULSE 12.0f      // N*s: speed = impulse / mass, up to PUNT_MAX
#define PUNT_MAX 20.0f

enum { GUN_PHYS, GUN_GRAV, GUN_COUNT };
static const char *const GUN_NAMES[GUN_COUNT] = { "physgun", "gravity gun" };
static const Color GUN_COLOR[GUN_COUNT] = { { 70, 170, 255, 255 }, { 255, 150, 40, 255 } };

typedef struct {
    SfxrPose pose, home;
    bool held;
    int hand;
    int target;              // the thing it has (-1: none)
    float dist;              // physgun: how far out along the beam
    Vector3 grab_local;      // physgun: where on the thing the beam caught it
    Quaternion rel;          // the thing's rotation in the gun's frame, when caught
    float flash;             // gravity gun: the punt's flash, seconds left
    Vector3 flash_to;
} Gun;

static struct {
    bool init;
    Gun gun[GUN_COUNT];
    bool frozen[WEIGHT_THINGS];
} G;

static void init(void)
{
    for (int g = 0; g < GUN_COUNT; g++) {
        Gun *gn = &G.gun[g];
        // lying on the stand, barrel toward the lane (-Z)
        gn->home = (SfxrPose){ { STAND_X - 0.14f + 0.28f * (float)g, STAND_H + 0.035f, ROW_Z }, QuaternionIdentity() };
        gn->pose = gn->home;
        gn->target = -1;
    }
    G.init = true;
}

// The gun in the hand: its barrel along the hand's aim, like the laser.
static SfxrPose in_hand(int h)
{
    return sfxr_pose_mul(sfxr_hand((SfxrHandId)h)->aim, (SfxrPose){ { 0, -0.03f, 0.06f }, QuaternionIdentity() });
}
static Vector3 muzzle(const Gun *gn) { return sfxr_pose_apply(gn->pose, (Vector3){ 0, 0.01f, -0.15f }); }
static Vector3 barrel(const Gun *gn) { return sfxr_pose_forward(gn->pose); }

// The thing the gun points at (-1 none), and how far.
static int aimed(const Gun *gn, float range, float *dist)
{
    Ray ray = { muzzle(gn), barrel(gn) };
    int best = -1;
    float bd = range;
    for (int k = 0; k < WEIGHT_THINGS; k++) {
        const WeightThing *t = weights_thing(k);
        Vector3 hh = t->spec->half;
        float r = fmaxf(fmaxf(hh.x, hh.y), hh.z) + 0.03f;   // a little generous: aiming far is hard
        RayCollision rc = GetRayCollisionSphere(ray, t->pose->position, r);
        if (rc.hit && rc.distance < bd) { bd = rc.distance; best = k; }
    }
    if (dist) *dist = bd;
    return best;
}

static void let_go(Gun *gn, bool fly)
{
    if (gn->target < 0) return;
    const WeightThing *t = weights_thing(gn->target);
    Vector3 v = fly ? t->w->velocity : Vector3Zero();
    float cap = t->spec->max_throw * 1.5f;
    if (Vector3Length(v) > cap) v = Vector3Scale(Vector3Normalize(v), cap);
    vrui_wield_set_motion(t->id, true, v, fly ? t->w->angular_velocity : Vector3Zero());
    sfxr_event("gun", "%s dropped %s", GUN_NAMES[gn - G.gun], t->name);
    gn->target = -1;
}

// --- the physgun
static void physgun(Gun *gn)
{
    const SfxrHand *hand = sfxr_hand((SfxrHandId)gn->hand);
    const SfxrButton *trig = &hand->trigger_at[vrui_style()->pull];
    float dt = sfxr_dt();
    if (trig->pressed && gn->target < 0) {
        float d;
        int k = aimed(gn, PHYS_RANGE, &d);
        if (k >= 0) {
            const WeightThing *t = weights_thing(k);
            gn->target = k;
            gn->dist = d;
            Vector3 hit = Vector3Add(muzzle(gn), Vector3Scale(barrel(gn), d));
            gn->grab_local = sfxr_pose_apply_inv(*t->pose, hit);
            gn->rel = QuaternionMultiply(QuaternionInvert(gn->pose.orientation), t->pose->orientation);
            G.frozen[k] = false;
            sound_play(SND_TRILL, hit, 0.4f);
            vrui_haptic_pulse((SfxrHandId)gn->hand, 0.5f, 0.03f, 150);
            sfxr_event("gun", "physgun caught %s", t->name);
        } else {
            sound_play(SND_CLICK, muzzle(gn), 0.3f);   // nothing there
        }
    }
    if (gn->target < 0) return;
    const WeightThing *t = weights_thing(gn->target);
    if (t->w->hands > 0) { gn->target = -1; return; }   // a hand took it
    if (!trig->down) { let_go(gn, true); return; }
    if (hand->primary.pressed && SFXR_BREAK(toolbox_physgun_no_freeze)) { let_go(gn, false); return; }
    if (hand->primary.pressed) {   // freeze it here
        G.frozen[gn->target] = true;
        vrui_wield_set_motion(t->id, false, Vector3Zero(), Vector3Zero());
        sound_play(SND_BELL, t->pose->position, 0.3f);
        sfxr_event("gun", "physgun froze %s", t->name);
        gn->target = -1;
        return;
    }
    // reel with the stick
    if (fabsf(hand->stick.y) > 0.2f) gn->dist = Clamp(gn->dist + hand->stick.y * 2.5f * dt, 0.3f, PHYS_RANGE);
    // where it wants to be: the caught spot on the end of the beam, turned
    // with the gun
    Quaternion want_q = QuaternionMultiply(gn->pose.orientation, gn->rel);
    Vector3 end = Vector3Add(muzzle(gn), Vector3Scale(barrel(gn), gn->dist));
    Vector3 want_p = Vector3Subtract(end, Vector3RotateByQuaternion(gn->grab_local, want_q));
    // it follows through a spring: a feather at once, an anvil a beat behind
    float m = t->spec->mass, rate = Clamp(20.0f / sqrtf(fmaxf(m, 0.02f)), 3.0f, 40.0f);
    float k = 1.0f - expf(-rate * dt);
    vrui_wield_set_motion(t->id, false, Vector3Zero(), Vector3Zero());
    t->pose->position = Vector3Lerp(t->pose->position, want_p, k);
    t->pose->orientation = QuaternionSlerp(t->pose->orientation, want_q, k);
    if (Vector3Distance(t->pose->position, want_p) > 0.08f) vrui_haptic_pulse((SfxrHandId)gn->hand, 0.15f, 0.02f, 80);
}

// --- the gravity gun
static void gravity_gun(Gun *gn)
{
    const SfxrHand *hand = sfxr_hand((SfxrHandId)gn->hand);
    const SfxrButton *trig = &hand->trigger_at[vrui_style()->pull];
    float dt = sfxr_dt();
    gn->flash = fmaxf(0, gn->flash - dt);
    if (gn->target >= 0 && weights_thing(gn->target)->w->hands > 0) gn->target = -1;   // a hand took it

    if (hand->primary.pressed) {
        if (gn->target >= 0) {
            let_go(gn, false);   // A again: drop it
        } else {
            float d;
            int k = aimed(gn, GRAV_RANGE, &d);
            if (k >= 0 && (weights_thing(k)->spec->mass <= GRAV_LIFT_KG || SFXR_BREAK(toolbox_gravgun_lifts_anything))) {
                const WeightThing *t = weights_thing(k);
                gn->target = k;
                gn->rel = QuaternionMultiply(QuaternionInvert(gn->pose.orientation), t->pose->orientation);
                G.frozen[k] = false;
                sound_play(SND_CHOMP, t->pose->position, 0.5f);
                sfxr_event("gun", "gravity gun lifted %s", t->name);
            } else if (k >= 0) {   // too heavy: a tug toward you, no more
                const WeightThing *t = weights_thing(k);
                Vector3 toward = Vector3Normalize(Vector3Subtract(muzzle(gn), t->pose->position));
                vrui_wield_set_motion(t->id, true, Vector3Scale(toward, PUNT_IMPULSE / t->spec->mass), Vector3Zero());
                sound_play(SND_STOP, t->pose->position, 0.5f);
                vrui_haptic_pulse((SfxrHandId)gn->hand, 0.5f, 0.12f, 40);
                sfxr_event("gun", "gravity gun can't lift %s", t->name);
            } else {
                sound_play(SND_CLICK, muzzle(gn), 0.3f);
            }
        }
    }
    if (gn->target >= 0) {
        // held in the air in front of the gun, pulled in quickly
        const WeightThing *t = weights_thing(gn->target);
        Vector3 hh = t->spec->half;
        float gap = 0.12f + fmaxf(fmaxf(hh.x, hh.y), hh.z);
        Vector3 want_p = Vector3Add(muzzle(gn), Vector3Scale(barrel(gn), gap));
        Quaternion want_q = QuaternionMultiply(gn->pose.orientation, gn->rel);
        float k = 1.0f - expf(-14.0f * dt);
        vrui_wield_set_motion(t->id, false, Vector3Zero(), Vector3Zero());
        t->pose->position = Vector3Lerp(t->pose->position, want_p, k);
        t->pose->orientation = QuaternionSlerp(t->pose->orientation, want_q, k);
    }
    if (!trig->pressed) return;
    // the punt: the held thing, or what's aimed at close by
    int k = gn->target;
    float d = 0;
    if (k < 0) k = aimed(gn, PUNT_RANGE, &d);
    if (k < 0) { sound_play(SND_CLICK, muzzle(gn), 0.4f); return; }
    const WeightThing *t = weights_thing(k);
    float speed = fminf(PUNT_IMPULSE / t->spec->mass, PUNT_MAX) * (gn->target >= 0 ? 1.0f : 1.0f - 0.5f * d / PUNT_RANGE);
    Vector3 dir = Vector3Normalize(Vector3Add(barrel(gn), (Vector3){ 0, 0.1f, 0 }));   // a little lift
    gn->target = -1;
    G.frozen[k] = false;
    vrui_wield_set_motion(t->id, true, Vector3Scale(dir, speed), (Vector3){ 3, 1, 0 });
    gn->flash = 0.12f;
    gn->flash_to = t->pose->position;
    sound_play(SND_WHOOSH, t->pose->position, 0.8f);
    vrui_haptic_pulse((SfxrHandId)gn->hand, 0.8f, 0.08f, 60);
    sfxr_event("gun", "gravity gun punted %s at %.1f m/s", t->name, speed);
}

// --- drawing
static void draw_gun(int g)
{
    const Gun *gn = &G.gun[g];
    Color c = GUN_COLOR[g];
    Color dark = ColorBrightness(c, -0.55f);
    vrui_box(sfxr_pose_mul(gn->pose, (SfxrPose){ { 0, 0.01f, -0.05f }, QuaternionIdentity() }), (Vector3){ 0.05f, 0.05f, 0.2f }, dark);
    vrui_box(sfxr_pose_mul(gn->pose, (SfxrPose){ { 0, 0.01f, -0.155f }, QuaternionIdentity() }), (Vector3){ 0.035f, 0.035f, 0.02f }, c);
    if (g == GUN_GRAV)   // the prongs
        for (int i = 0; i < 3; i++) {
            float a = (float)i * 2.0f * PI / 3.0f;
            Vector3 base = sfxr_pose_apply(gn->pose, (Vector3){ 0.03f * cosf(a), 0.01f + 0.03f * sinf(a), -0.16f });
            Vector3 tip = sfxr_pose_apply(gn->pose, (Vector3){ 0.045f * cosf(a), 0.01f + 0.045f * sinf(a), -0.21f });
            vrui_cylinder(base, tip, 0.005f, 0.004f, gn->target >= 0 ? c : dark);
        }
    // the handle, under the back of it
    vrui_box(sfxr_pose_mul(gn->pose, (SfxrPose){ { 0, -0.045f, 0.02f }, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 15 * DEG2RAD) }),
             (Vector3){ 0.03f, 0.08f, 0.035f }, dark);
    if (!gn->held) vrui_text3d(Vector3Add(gn->home.position, (Vector3){ 0, 0.14f, 0 }), GUN_NAMES[g], 0.03f, c);
}

// The physgun's beam: from the muzzle out along the barrel, bending over to
// where the thing really is (a quadratic curve), so its lag shows.
static void draw_beam(const Gun *gn)
{
    const WeightThing *t = weights_thing(gn->target);
    Vector3 a = muzzle(gn), b = sfxr_pose_apply(*t->pose, gn->grab_local);
    Vector3 ctrl = Vector3Add(a, Vector3Scale(barrel(gn), gn->dist * 0.6f));
    Vector3 prev = a;
    for (int i = 1; i <= 16; i++) {
        float s = (float)i / 16.0f;
        Vector3 p = Vector3Add(Vector3Add(Vector3Scale(a, (1 - s) * (1 - s)), Vector3Scale(ctrl, 2 * s * (1 - s))), Vector3Scale(b, s * s));
        vrui_cylinder(prev, p, 0.004f, 0.004f, (Color){ 120, 210, 255, 200 });
        prev = p;
    }
    vrui_sphere(b, 0.015f, (Color){ 180, 235, 255, 230 });
}

void station_guns(void)
{
    if (!G.init) init();
    // the stand
    vrui_box((SfxrPose){ { STAND_X, STAND_H - 0.02f, ROW_Z }, QuaternionIdentity() }, (Vector3){ 0.7f, 0.04f, 0.4f }, (Color){ 120, 92, 66, 255 });
    vrui_box((SfxrPose){ { STAND_X, (STAND_H - 0.04f) * 0.5f, ROW_Z }, QuaternionIdentity() }, (Vector3){ 0.08f, STAND_H - 0.04f, 0.08f },
             (Color){ 90, 68, 52, 255 });
    vrui_sign((SfxrPose){ { STAND_X, STAND_H + 0.55f, ROW_Z - 0.25f }, QuaternionIdentity() }, 0.8f, "Physics guns",
              "Pick one up with the grip. PHYSGUN: hold the trigger on a thing, swing it, twist your wrist, "
              "stick to reel it in or out, A to freeze it in the air. GRAVITY GUN: A lifts a thing (A again drops it), "
              "the trigger punts it.",
              (Color){ 44, 50, 64, 255 });
    for (int g = 0; g < GUN_COUNT; g++) {
        Gun *gn = &G.gun[g];
        VruiId id = VRUI_ID2(G_GUNS, 1 + g);
        VruiGrab gr = vrui_grab_region(id, &gn->pose, (Vector3){ 0.04f, 0.06f, 0.12f });
        vrui_name_widget(id, GUN_NAMES[g]);
        if (gr.grabbed) {
            gn->held = true;
            gn->hand = gr.hand;
            sound_play(SND_CLICK, gn->pose.position, 0.4f);
            sfxr_event("gun", "%s taken %s", GUN_NAMES[g], gr.hand == SFXR_RIGHT ? "R" : "L");
        }
        if (gr.released || (gn->held && !gr.held)) {   // back to its stand
            let_go(gn, true);
            gn->held = false;
            gn->pose = gn->home;
        }
        if (gn->held) {
            gn->pose = in_hand(gn->hand);
            vrui_claim_input((SfxrHandId)gn->hand);
            if (g == GUN_PHYS) physgun(gn); else gravity_gun(gn);
        } else {
            gn->pose = gn->home;
        }
        draw_gun(g);
        if (g == GUN_PHYS && gn->target >= 0) draw_beam(gn);
        if (g == GUN_GRAV && gn->target >= 0) {
            const WeightThing *t = weights_thing(gn->target);
            vrui_line(muzzle(gn), t->pose->position, (Color){ 255, 190, 90, 120 });
        }
        if (gn->flash > 0) vrui_cylinder(muzzle(gn), gn->flash_to, 0.01f, 0.004f, (Color){ 255, 220, 150, 220 });
        sfxr_report(g == GUN_PHYS ? "physgun_holding" : "gravgun_holding", gn->target >= 0 ? 1.0f : 0.0f);
        sfxr_report(g == GUN_PHYS ? "physgun_in_hand" : "gravgun_in_hand", gn->held ? 1.0f : 0.0f);
    }
    // frozen things wear a blue cage
    for (int k = 0; k < WEIGHT_THINGS; k++) {
        const WeightThing *t = weights_thing(k);
        if (G.frozen[k] && (t->w->hands > 0 || t->w->loose)) G.frozen[k] = false;
        if (G.frozen[k]) vrui_box_wires(*t->pose, Vector3Scale(Vector3AddValue(t->spec->half, 0.01f), 2), (Color){ 120, 210, 255, 255 });
        sfxr_report(TextFormat("%s_frozen", t->name), G.frozen[k] ? 1.0f : 0.0f);
    }
}

void guns_reset(void)
{
    for (int g = 0; g < GUN_COUNT; g++) G.gun[g].target = -1;
    memset(G.frozen, 0, sizeof G.frozen);
}
