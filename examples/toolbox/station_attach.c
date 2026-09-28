// station_attach.c - Attach & label: how one thing rides on another, and
// every kind of world label (docs/ATTACHING.md, vrui.h sections 9 and 10).
//
//   Turntable   flick it: three blocks and a lever ride on it. The lever is a
//               mechanism mounted ON the turntable, and a flag rides on the
//               lever's tip: bench -> turntable -> lever -> flag, a chain of
//               parents, each one line of pose math.
//   The blocks  each has a label saying what it's attached to right now.
//               Take one off (it's in your hand: the hand is its parent), put
//               it back on the turntable where you like (it stays exactly
//               there and rides along), put it on your hip (it rides on your
//               belt: vrui_body), or let go anywhere else (it drops: no parent).
//   Labels      one of each kind on the right: text printed on the bench
//               (vrui_text_at), a floating name (vrui_text3d), a tag on a plate
//               (vrui_tag), a callout pointing at a spot (vrui_callout); the
//               sign overhead is a vrui_sign.
//
// The one rule behind all of it (sfxr.h, "Attaching"):
//     child_world = sfxr_pose_mul(parent_world, child_local)   every frame
//     child_local = sfxr_pose_relative(parent_world, child_world)   when attaching

#include "toolbox.h"

#define BENCH_X  7.9f
#define BENCH_W  1.6f
#define BENCH_D  0.6f

typedef enum { ON_TABLE, IN_HAND, ON_BELT, LOOSE } Parent;
static const char *const PARENT_WORDS[] = { "on the turntable", "in your hand", "on your belt", "no parent: dropped" };

typedef struct {
    Parent parent;
    SfxrPose local;     // relative to the parent (ON_TABLE, ON_BELT)
    SfxrPose world;     // this frame's world pose
    float vy;           // LOOSE: falling speed
    Color color;
    const char *name;
} Rider;

static struct {
    bool init;
    float spin;         // turntable angle (the spinner's value, in turns)
    float arm;          // the lever on the turntable, 0..1
    Rider r[3];
} A;

static SfxrPose origin(void) { return row_pose(BENCH_X, TABLE_Y); }
static SfxrPose on_bench(float x, float y, float z) { return sfxr_pose_mul(origin(), (SfxrPose){ { x, y, z }, QuaternionIdentity() }); }

#define TT_X   -0.4f      // turntable center on the bench
#define TT_R    0.2f
#define HALF    0.03f     // the blocks' half size

// The belt slot: on your right hip, a little forward. The body's origin is on
// the floor under your head, so the waist is a fraction of eye height up.
static SfxrPose belt_slot(void)
{
    return sfxr_pose_mul(vrui_body(), (SfxrPose){ { 0.2f, 0.58f * vrui_eye_height(), -0.12f }, QuaternionIdentity() });
}

static void init(void)
{
    static const Color COLS[3] = { { 230, 80, 70, 255 }, { 90, 200, 110, 255 }, { 80, 160, 240, 255 } };
    static const char *const NAMES[3] = { "red", "green", "blue" };
    for (int i = 0; i < 3; i++) {
        float a = (float)i / 3.0f * 2.0f * PI;
        A.r[i] = (Rider){ .parent = ON_TABLE, .color = COLS[i], .name = NAMES[i] };
        A.r[i].local = (SfxrPose){ { sinf(a) * TT_R * 0.65f, HALF + 0.001f, cosf(a) * TT_R * 0.65f }, QuaternionIdentity() };
    }
    A.init = true;
}

// Where a released block goes: back on the turntable if you let go over it,
// onto your belt if you let go at your hip, else it drops.
static void reattach(Rider *r, SfxrPose table)
{
    Vector3 on = sfxr_pose_apply_inv(table, r->world.position);
    if (on.x * on.x + on.z * on.z < TT_R * TT_R && on.y > -0.02f && on.y < 0.25f) {
        r->parent = ON_TABLE;
        r->local = sfxr_pose_relative(table, r->world);      // exactly where you put it...
        r->local.position.y = HALF + 0.001f;                  // ...resting on the top
        return;
    }
    if (Vector3Distance(r->world.position, belt_slot().position) < 0.2f) {
        r->parent = ON_BELT;
        r->local = (SfxrPose){ { 0 }, QuaternionIdentity() };  // snaps into the slot
        return;
    }
    r->parent = LOOSE;
    r->vy = 0;
}

static void fall(Rider *r)
{
    // No parent: gravity, and it lands on the bench top or the floor.
    r->vy -= 9.8f * sfxr_dt();
    r->world.position.y += r->vy * sfxr_dt();
    Vector3 on = sfxr_pose_apply_inv(origin(), r->world.position);
    bool over_bench = fabsf(on.x) < BENCH_W * 0.5f && fabsf(on.z) < BENCH_D * 0.5f && on.y > -0.05f;
    float ground = (over_bench ? TABLE_Y : 0.0f) + HALF;
    if (r->world.position.y < ground) { r->world.position.y = ground; r->vy = 0; }
}

static void labels_gallery(void)
{
    Color ink = { 235, 235, 240, 255 };
    // printed ON the bench's front edge, facing you: gone when you walk behind it
    SfxrPose front = on_bench(0.35f, -0.03f, BENCH_D * 0.5f + 0.002f);
    vrui_text_at(front, "vrui_text_at: printed on the bench (walk behind it: gone)", 0.014f, (Color){ 250, 220, 150, 255 });

    // a floating name over a ball
    vrui_box(on_bench(0.15f, 0.04f, 0.05f), (Vector3){ 0.06f, 0.08f, 0.06f }, (Color){ 200, 170, 90, 255 });
    vrui_text3d(sfxr_pose_apply(origin(), (Vector3){ 0.15f, 0.15f, 0.05f }), "vrui_text3d\n(turns to face you)", 0.016f, ink);

    // a tag above a thing, on a plate: the plate keeps it readable whatever
    // is behind it (here, a checkerboard on a stand, when you look down at it)
    vrui_box(on_bench(0.465f, 0.022f, -0.2f), (Vector3){ 0.01f, 0.044f, 0.01f }, (Color){ 90, 90, 96, 255 });
    for (int i = 0; i < 16; i++)
        vrui_box(on_bench(0.42f + 0.03f * (float)(i % 4), 0.06f + 0.03f * (float)(i / 4), -0.2f),
                 (Vector3){ 0.03f, 0.03f, 0.01f }, (i + i / 4) % 2 ? (Color){ 240, 240, 240, 255 } : (Color){ 30, 30, 30, 255 });
    vrui_tag(sfxr_pose_apply(origin(), (Vector3){ 0.465f, 0.21f, -0.2f }), "vrui_tag: a checkerboard", 0.016f, ink,
             (Color){ 20, 22, 28, 220 });

    // a callout pointing at one small screw; walk away and it stays readable
    vrui_box(on_bench(0.68f, 0.005f, 0.12f), (Vector3){ 0.012f, 0.01f, 0.012f }, (Color){ 170, 170, 180, 255 });
    vrui_callout(sfxr_pose_apply(origin(), (Vector3){ 0.68f, 0.012f, 0.12f }), "vrui_callout: this screw", 0.12f,
                 (Color){ 255, 210, 90, 255 });
}

void station_attach(void)
{
    if (!A.init) init();
    station_sign(BENCH_X, "Attach & label", "things riding on things, and every kind\nof world label (docs/ATTACHING.md)");

    // the bench
    Color wood = { 120, 92, 66, 255 };
    vrui_box(on_bench(0, -0.025f, 0), (Vector3){ BENCH_W, 0.05f, BENCH_D }, wood);
    for (int i = 0; i < 4; i++)
        vrui_box(on_bench((i & 1) ? BENCH_W * 0.46f : -BENCH_W * 0.46f, -TABLE_Y * 0.5f, (i & 2) ? BENCH_D * 0.4f : -BENCH_D * 0.4f),
                 (Vector3){ 0.05f, TABLE_Y - 0.05f, 0.05f }, (Color){ 90, 68, 52, 255 });

    // 1. the turntable: a spinner (flick it, it coasts). Its result's `part`
    // is the turning top: the parent of everything that rides on it.
    VruiMechSpec tt = vrui_spinner_spec(24);
    tt.size = TT_R;
    tt.label = "TURNTABLE (flick it)";
    VruiMech m = vrui_rotary(VRUI_ID2(G_ATTACH, 1), on_bench(TT_X, 0, 0), &tt, &A.spin);
    SfxrPose table = m.part;

    // 2. a lever mounted ON the turntable: its base is a pose on the table,
    // so the whole mechanism rides along and still works while it turns
    SfxrPose mount = table;   // the part pose is the top surface
    VruiMechSpec ls = vrui_lever_spec();
    ls.size = 0.14f;
    ls.label = NULL;
    VruiMech lever = vrui_pivot(VRUI_ID2(G_ATTACH, 2), mount, &ls, &A.arm);

    // 3. a flag on the lever's tip: a grandchild of the turntable
    SfxrPose flag = sfxr_pose_mul(lever.part, (SfxrPose){ { 0, ls.size + 0.03f, 0 }, QuaternionIdentity() });
    vrui_box(flag, (Vector3){ 0.05f, 0.03f, 0.004f }, (Color){ 250, 200, 60, 255 });
    vrui_callout(sfxr_pose_apply(flag, (Vector3){ 0, 0.015f, 0 }), "flag: lever -> turntable -> bench", 0.3f, RAYWHITE);

    // 4. the blocks: a parent each, which changes as you move them
    for (int i = 0; i < 3; i++) {
        Rider *r = &A.r[i];
        if (r->parent == ON_TABLE) r->world = sfxr_pose_mul(table, r->local);
        else if (r->parent == ON_BELT) r->world = sfxr_pose_mul(belt_slot(), r->local);
        else if (r->parent == LOOSE) fall(r);
        // (IN_HAND: vrui_grabbable below moves it with the hand)

        VruiGrab g = vrui_grabbable(VRUI_ID2(G_ATTACH, 10 + i), &r->world, (Vector3){ HALF, HALF, HALF }, r->color);
        if (g.grabbed) r->parent = IN_HAND;
        if (g.released) reattach(r, table);

        // the label follows the block because it's computed from its pose
        vrui_callout(Vector3Add(r->world.position, (Vector3){ 0, HALF, 0 }), TextFormat("%s: %s", r->name, PARENT_WORDS[r->parent]),
                     0.08f + 0.06f * (float)i, r->color);   // different heights, so the tags don't overlap
        if (r->parent == LOOSE && r->world.position.y < 0.2f && Vector3Distance(r->world.position, origin().position) > 6.0f) {
            r->parent = ON_TABLE;   // lost far away on the floor: home it goes
        }
    }

    // While you hold a block near your hip, show the slot so you know it's there.
    for (int i = 0; i < 3; i++)
        if (A.r[i].parent == IN_HAND && Vector3Distance(A.r[i].world.position, belt_slot().position) < 0.35f) {
            SfxrPose s = belt_slot();
            vrui_box(s, (Vector3){ 0.08f, 0.08f, 0.08f }, (Color){ 120, 200, 255, 90 });
            vrui_tag(Vector3Add(s.position, (Vector3){ 0, 0.09f, 0 }), "belt slot: let go here", 0.014f, RAYWHITE,
                     (Color){ 20, 22, 28, 200 });
        }

    labels_gallery();
}
