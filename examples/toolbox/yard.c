// yard.c - the Movement yard, behind you as you face the row of stations:
// examples of moving the player that aren't "teleport anywhere"
// (docs/MOVEMENT.md).
//
//   Teleport pads   inside the yard you can only land on a pad; each pad
//                   puts you on its center facing the way its arrow points
//   Stairs          walk up them in your room (20 cm steps)
//   Climbing wall   rock holds on the left, a ladder on the right: grab
//                   (grip), pull down to rise, push yourself over the top
//                   edge and let go to stand on it
//   Monkey bars     between two platforms: hand over hand; let go and you
//                   drop to the ground
//   The LIFT        (on the way to the yard) is a surface too: stand on it and
//                   move the workbench's LIFT slider with the laser
//
// All of it is data for vrui_locomotion: a ground_height() function over a
// list of boxes, a pad list, a valid_target() rule, and handholds. The boxes
// are the only "physics" -- enough for platforms, stairs, walls and lifts.
//
// The numbers below are written as if the yard were straight ahead of the
// spawn point (-Z), which is easy to picture; W() turns the whole yard round
// to stand behind it. A half turn keeps every box lined up with the axes, so
// the box tests stay simple. (To put it elsewhere, change W and L together.)

#include "toolbox.h"

static Vector3 W(Vector3 p) { return (Vector3){ -p.x, p.y, -p.z }; }   // yard -> world
static Vector3 L(Vector3 p) { return (Vector3){ -p.x, p.y, -p.z }; }   // world -> yard (a half turn is its own inverse)
#define YAW_TURN 180.0f

typedef struct { Vector3 lo, hi; Color color; } Block;   // hi.y is the top you stand on

#define YARD_X0 -3.6f
#define YARD_X1  3.6f
#define YARD_Z0 -12.0f   // far end
#define YARD_Z1 -3.9f    // near end

#define WALL_FACE_Z -8.0f
#define WALL_TOP     2.6f
#define BAR_Y        2.8f   // 2 m above the monkey-bar platforms

static const Block BLOCKS[] = {
    // climbing wall: 2.6 m tall, its face toward you at z = -8
    { { -3.2f, 0, -11.0f }, { -0.8f, WALL_TOP, WALL_FACE_Z }, { 150, 132, 110, 255 } },
    // monkey bars: near and far platforms
    { { 1.0f, 0, -6.2f }, { 3.0f, 0.8f, -5.0f }, { 110, 120, 140, 255 } },
    { { 1.0f, 0, -10.0f }, { 3.0f, 0.8f, -8.8f }, { 110, 120, 140, 255 } },
    // stairs up to the near platform
    { { 1.5f, 0, -4.4f }, { 2.5f, 0.2f, -4.1f }, { 130, 138, 150, 255 } },
    { { 1.5f, 0, -4.7f }, { 2.5f, 0.4f, -4.4f }, { 130, 138, 150, 255 } },
    { { 1.5f, 0, -5.0f }, { 2.5f, 0.6f, -4.7f }, { 130, 138, 150, 255 } },
};
#define NBLOCKS ((int)(sizeof BLOCKS / sizeof BLOCKS[0]))

static const VruiTeleportPad PADS_YARD[] = {
    { { 0.0f, 0, -4.6f }, 0.45f, true, 0 },               // the way in
    { { -2.0f, 0, -7.2f }, 0.45f, true, 0 },              // foot of the wall
    { { -2.0f, WALL_TOP, -9.6f }, 0.45f, true, 180 },     // top of the wall, looking back
    { { 2.0f, 0.8f, -5.7f }, 0.4f, true, 0 },             // near monkey-bar platform
    { { 2.0f, 0.8f, -9.4f }, 0.4f, true, 180 },           // far one, looking back
    { { 0.0f, 0, -11.2f }, 0.45f, true, 180 },            // the far end
};
#define NPADS ((int)(sizeof PADS_YARD / sizeof PADS_YARD[0]))
static VruiTeleportPad PADS[NPADS];   // the same, in world space (yard_setup)

static bool on_block(Vector3 p, const Block *b, float *g)
{
    if (p.x >= b->lo.x && p.x <= b->hi.x && p.z >= b->lo.z && p.z <= b->hi.z && b->hi.y <= p.y + 1e-4f && b->hi.y > *g) {
        *g = b->hi.y;
        return true;
    }
    return false;
}

// vrui_locomotion's ground_height: the highest top at or below p.
static float toolbox_ground(Vector3 p, void *user)
{
    (void)user;
    float g = 0;
    Vector3 y = L(p);
    for (int i = 0; i < NBLOCKS; i++) on_block(y, &BLOCKS[i], &g);
    // the lift (world.c) is a surface like any other, already in world space
    Block lift = { { LIFT_X - 0.5f, 0, LIFT_Z - 0.5f }, { LIFT_X + 0.5f, world_lift_height(), LIFT_Z + 0.5f }, { 0 } };
    on_block(p, &lift, &g);
    return g;
}

// Inside the yard only pads are valid (vrui checks pads before this rule);
// everywhere else you can teleport anywhere.
static bool outside_yard(Vector3 t, void *user)
{
    (void)user;
    Vector3 y = L(t);
    return !(y.x > YARD_X0 && y.x < YARD_X1 && y.z > YARD_Z0 && y.z < YARD_Z1);
}

void yard_setup(VruiLocoConfig *loco)
{
    for (int i = 0; i < NPADS; i++) {
        PADS[i] = PADS_YARD[i];
        PADS[i].center = W(PADS_YARD[i].center);
        PADS[i].yaw_deg = PADS_YARD[i].yaw_deg + YAW_TURN;
    }
    loco->ground_height = toolbox_ground;
    loco->valid_target = outside_yard;
    loco->pads = PADS;
    loco->npads = NPADS;
}

void yard_update(void)
{
    if (world.passthrough) return;
    Color hold = { 230, 150, 60, 255 }, rung = { 200, 200, 210, 255 };
    int id = 0;

    // rock holds on the left part of the wall face, staggered like a real route
    static const float ROCKS[][2] = {
        { -2.9f, 0.9f }, { -2.2f, 1.1f }, { -2.6f, 1.5f }, { -1.9f, 1.7f }, { -2.9f, 1.9f },
        { -2.3f, 2.1f }, { -1.8f, 2.3f }, { -2.7f, 2.35f },
    };
    for (int i = 0; i < (int)(sizeof ROCKS / sizeof ROCKS[0]); i++) {
        Vector3 at = { ROCKS[i][0], ROCKS[i][1], WALL_FACE_Z + 0.04f };
        vrui_handhold(VRUI_ID2(G_YARD, ++id), W(at), W(at), 0.035f, hold);
    }
    // a ladder on the right part: rungs every 30 cm, standing off the face
    for (int i = 0; i < 8; i++) {
        float y = 0.35f + 0.3f * (float)i;
        vrui_handhold(VRUI_ID2(G_YARD, ++id), W((Vector3){ -1.35f, y, WALL_FACE_Z + 0.08f }),
                      W((Vector3){ -0.95f, y, WALL_FACE_Z + 0.08f }), 0.018f, rung);
    }
    // the top edge: grab it to pull yourself over
    vrui_handhold(VRUI_ID2(G_YARD, ++id), W((Vector3){ -3.15f, WALL_TOP, WALL_FACE_Z + 0.03f }),
                  W((Vector3){ -0.85f, WALL_TOP, WALL_FACE_Z + 0.03f }), 0.03f, hold);

    // monkey bars across the gap between the platforms
    for (int i = 0; i < 6; i++) {
        float z = -6.4f - 0.44f * (float)i;
        vrui_handhold(VRUI_ID2(G_YARD, ++id), W((Vector3){ 1.4f, BAR_Y, z }), W((Vector3){ 2.6f, BAR_Y, z }), 0.02f, rung);
    }

    // signs: the yard's name over the entrance, and a word at each example
    SfxrPose gate = vrui_facing(W((Vector3){ 0, 3.2f, YARD_Z1 }), (Vector3){ 0, 3.2f, 0 });   // faces the row
    vrui_sign(gate, 2.4f, "Movement yard", "in here you can only teleport onto pads", (Color){ 44, 50, 64, 255 });
    vrui_text3d(W((Vector3){ -2.0f, WALL_TOP + 0.5f, WALL_FACE_Z + 0.3f }),
                "climb: grab, pull down, push over the top, let go", 0.06f, RAYWHITE);
    vrui_text3d(W((Vector3){ 2.0f, BAR_Y + 0.35f, -7.5f }), "monkey bars: hand over hand", 0.06f, RAYWHITE);
    vrui_text3d(W((Vector3){ 2.0f, 1.2f, -3.9f }), "stairs: walk up in your room", 0.05f, RAYWHITE);
}

void yard_draw(void)
{
    if (world.passthrough) return;
    for (int i = 0; i < NBLOCKS; i++) {
        const Block *b = &BLOCKS[i];
        Vector3 c = W(Vector3Scale(Vector3Add(b->lo, b->hi), 0.5f)), s = Vector3Subtract(b->hi, b->lo);
        DrawCubeV(c, s, b->color);
        DrawCubeWiresV(c, s, (Color){ 40, 40, 48, 255 });
    }
    // ladder rails
    for (int k = 0; k < 2; k++) {
        float x = k ? -0.95f : -1.35f;
        DrawCylinderEx(W((Vector3){ x, 0, WALL_FACE_Z + 0.08f }), W((Vector3){ x, WALL_TOP, WALL_FACE_Z + 0.08f }), 0.02f, 0.02f, 6,
                       (Color){ 160, 160, 170, 255 });
    }
    // monkey-bar frame: two rails and four posts
    Color frame = { 80, 84, 96, 255 };
    for (int k = 0; k < 2; k++) {
        float x = k ? 2.6f : 1.4f;
        DrawCylinderEx(W((Vector3){ x, BAR_Y, -6.2f }), W((Vector3){ x, BAR_Y, -8.8f }), 0.03f, 0.03f, 6, frame);
        DrawCylinderEx(W((Vector3){ x, 0.8f, -6.2f }), W((Vector3){ x, BAR_Y, -6.2f }), 0.04f, 0.04f, 6, frame);
        DrawCylinderEx(W((Vector3){ x, 0.8f, -8.8f }), W((Vector3){ x, BAR_Y, -8.8f }), 0.04f, 0.04f, 6, frame);
    }
    // pads: a disc and an arrow the way you'll face
    for (int i = 0; i < NPADS; i++) {
        const VruiTeleportPad *p = &PADS[i];
        Vector3 c = { p->center.x, p->center.y + 0.005f, p->center.z };
        DrawCylinder(c, p->radius, p->radius, 0.02f, 24, (Color){ 70, 150, 220, 200 });
        float a = p->yaw_deg * DEG2RAD;
        Vector3 f = { sinf(a), 0, -cosf(a) }, r = { cosf(a), 0, sinf(a) };
        Vector3 tip = Vector3Add(c, Vector3Scale(f, p->radius * 0.8f));
        Vector3 up = { 0, 0.03f, 0 };
        tip = Vector3Add(tip, up);
        Vector3 l = Vector3Add(Vector3Add(c, Vector3Scale(r, -p->radius * 0.35f)), up);
        Vector3 rr = Vector3Add(Vector3Add(c, Vector3Scale(r, p->radius * 0.35f)), up);
        DrawTriangle3D(l, rr, tip, RAYWHITE);
        DrawTriangle3D(rr, l, tip, RAYWHITE);
    }
}
