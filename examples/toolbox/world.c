// world.c - the toolbox's world: ground, trees and sky, the lift platform,
// and the workbench straight ahead, whose controls are wired to the world:
//
//   SPAWN (a push button that wants a FULL pull, a per-item override)
//   GRID  (switch)   SKY (lever)   SIZE (knob)   LIFT (slider)
//
// plus throwable blocks, and what your hand does to a block depending on its
// SHAPE (read from the Frame's touch sensors): an open palm shoves it, a fast
// fist knocks it flying. Grabbing is the grip (or the grab style chosen on the
// Toolbox panel).

#include "toolbox.h"

#include <math.h>

ToolboxWorld world;

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

#define MAX_BLOCKS 48

typedef struct {
    bool alive, held;
    SfxrPose pose;
    Vector3 vel, angvel;
    Vector3 half;
    Color color;
} Block;

static Block blocks[MAX_BLOCKS];

static const Color SPAWN_COLORS[6] = {
    { 230, 80, 70, 255 }, { 240, 170, 60, 255 }, { 90, 200, 110, 255 },
    { 80, 160, 240, 255 }, { 170, 110, 230, 255 }, { 235, 235, 240, 255 },
};
const char *const SPAWN_COLOR_NAMES[6] = { "Red", "Amber", "Green", "Blue", "Violet", "White" };

void world_spawn_block(Vector3 at)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &blocks[i];
        if (b->alive) continue;
        float s = 0.04f * world.block_size;
        *b = (Block){0};
        b->alive = true;
        b->pose = (SfxrPose){ at, QuaternionFromEuler(0.3f * (float)(i % 3), 0.5f * (float)i, 0) };
        b->half = (Vector3){ s, s, s };
        b->color = SPAWN_COLORS[world.color_index];
        world.spawned++;
        return;
    }
}

void world_reset_blocks(void)
{
    for (int i = 0; i < MAX_BLOCKS; i++) blocks[i].alive = false;
    int keep = world.color_index;
    for (int i = 0; i < 4; i++) {
        world.color_index = i;
        world_spawn_block((Vector3){ 0.15f + 0.12f * (float)i, TABLE_Y + 0.05f, TABLE_Z - 0.22f });
    }
    world.color_index = keep;
}

void world_init(void)
{
    world.show_grid = true;
    world.block_size = 1.0f;
    world.gravity = true;
    world.throw_power = 1.3f;
    world_reset_blocks();
}

static float lowest_corner_y(const Block *b)
{
    float m = 1e9f;
    for (int i = 0; i < 8; i++) {
        Vector3 c = { (i & 1) ? b->half.x : -b->half.x, (i & 2) ? b->half.y : -b->half.y, (i & 4) ? b->half.z : -b->half.z };
        m = fminf(m, sfxr_pose_apply(b->pose, c).y);
    }
    return m;
}

static void step_block(Block *b, float dt)
{
    if (b->held || !b->alive) return;
    if (world.gravity) b->vel.y -= 9.8f * dt;
    b->pose.position = Vector3Add(b->pose.position, Vector3Scale(b->vel, dt));
    Quaternion w = { b->angvel.x, b->angvel.y, b->angvel.z, 0 };
    Quaternion dq = QuaternionScale(QuaternionMultiply(w, b->pose.orientation), 0.5f * dt);
    b->pose.orientation = QuaternionNormalize(QuaternionAdd(b->pose.orientation, dq));

    // resting surfaces: the table top (inside its footprint) or the floor
    Vector3 p = b->pose.position;
    bool over_table = fabsf(p.x) < TABLE_W * 0.5f && fabsf(p.z - TABLE_Z) < TABLE_D * 0.5f && p.y > TABLE_Y - 0.05f;
    float ground = over_table ? TABLE_Y : 0.0f;
    float low = lowest_corner_y(b);
    if (low < ground) {
        b->pose.position.y += ground - low;
        if (b->vel.y < 0) b->vel.y = -b->vel.y * 0.25f;
        b->vel.x *= 0.85f;
        b->vel.z *= 0.85f;
        b->angvel = Vector3Scale(b->angvel, 0.85f);
        if (fabsf(b->vel.y) < 0.05f) b->vel.y = 0;
    }
    if (!world.gravity) {   // zero-g: gentle drag so things drift to a stop
        b->vel = Vector3Scale(b->vel, 1.0f - 0.8f * dt);
        b->angvel = Vector3Scale(b->angvel, 1.0f - 0.8f * dt);
    }
}

void world_step(float dt)
{
    for (int i = 0; i < MAX_BLOCKS; i++) step_block(&blocks[i], dt);
}

// Conditional physical interaction: what a free hand does to a block depends
// on the hand's shape. Open hand: the palm shoves it. Fist: a fast punch
// knocks it away.
static void hand_physics(Block *b)
{
    for (int h = 0; h < 2; h++) {
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        if (!hand->active || vrui_hand_busy((SfxrHandId)h)) continue;
        float speed = Vector3Length(hand->velocity);
        bool open = hand->shape == SFXR_SHAPE_OPEN, fist = hand->shape == SFXR_SHAPE_FIST;
        if ((!open && !fist) || speed < 0.25f) continue;
        Vector3 at = open ? hand->palm.position : hand->grip.position;
        if (Vector3Distance(at, b->pose.position) > b->half.x * 1.7f + 0.04f) continue;
        Vector3 away = Vector3Subtract(b->pose.position, at);
        if (Vector3DotProduct(away, hand->velocity) <= 0) continue;   // only when moving into it
        if (open) {
            b->vel = Vector3Lerp(b->vel, hand->velocity, 0.6f);
            vrui_haptic_hum((SfxrHandId)h, Clamp(0.15f + speed * 0.2f, 0, 0.6f), 90.0f);
        } else if (speed > 1.2f) {
            b->vel = Vector3Add(Vector3Scale(hand->velocity, 1.6f), (Vector3){ 0, 1.0f, 0 });
            b->angvel = (Vector3){ speed * 3.0f, speed * 2.0f, 0 };
            vrui_haptic_pulse((SfxrHandId)h, Clamp(0.4f + speed * 0.15f, 0, 1), 0.04f, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// The workbench: controls wired to the world
// ---------------------------------------------------------------------------

static SfxrPose on_table(float x, float z_off)
{
    return (SfxrPose){ { x, TABLE_Y, TABLE_Z + z_off }, QuaternionIdentity() };
}

void world_workbench(void)
{
    station_sign(0, "Workbench", "controls wired to the world;\npick up and throw the blocks");
    // A per-item pull level: SPAWN wants a deliberate full pull with the
    // laser (poking it with a fingertip works as usual).
    vrui_push_pull(SFXR_PULL_FULL);
    if (vrui_push_button(VRUI_ID2(G_TABLE, 1), on_table(-0.5f, 0.1f), 0.035f, (Color){ 220, 60, 50, 255 }, "SPAWN (full pull)"))
        world_spawn_block((Vector3){ -0.3f + 0.1f * (float)(world.spawned % 5), TABLE_Y + 0.3f, TABLE_Z });
    vrui_pop_pull();
    vrui_switch(VRUI_ID2(G_TABLE, 2), on_table(-0.3f, 0.1f), &world.show_grid, "GRID");
    vrui_lever(VRUI_ID2(G_TABLE, 3), on_table(-0.1f, 0.0f), 0.18f, &world.sky, "SKY");
    vrui_knob(VRUI_ID2(G_TABLE, 4), on_table(0.12f, 0.12f), 0.035f, &world.block_size, 0.5f, 2.0f, 0.75f, "SIZE");
    vrui_slider3d(VRUI_ID2(G_TABLE, 5), on_table(0.4f, 0.2f), 0.3f, &world.lift, "LIFT");

    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &blocks[i];
        if (!b->alive) continue;
        VruiGrab g = vrui_grabbable(VRUI_ID2(G_BLOCKS, i), &b->pose, b->half, b->color);
        b->held = g.held;
        if (g.grabbed) { b->vel = (Vector3){0}; b->angvel = (Vector3){0}; }
        if (g.released) {
            b->vel = Vector3Scale(g.release_velocity, world.throw_power);
            b->angvel = g.release_angular_velocity;
        }
        if (!g.held) hand_physics(b);
        if (b->pose.position.y < -5 || Vector3Length(b->pose.position) > 60) b->alive = false;
    }
}

// ---------------------------------------------------------------------------
// Drawing the world
// ---------------------------------------------------------------------------

Color world_sky(void)
{
    if (world.passthrough) return (Color){ 0, 0, 0, 0 };   // transparent: the room shows through
    Color day = { 120, 170, 225, 255 }, dusk = { 50, 36, 70, 255 };
    return ColorLerp(day, dusk, world.sky);
}

void world_draw(void)
{
    if (world.passthrough) {   // keep the table, drop the scenery
        DrawCube((Vector3){ 0, TABLE_Y - 0.025f, TABLE_Z }, TABLE_W, 0.05f, TABLE_D, (Color){ 140, 100, 70, 255 });
        return;
    }
    DrawPlane((Vector3){ 0, -0.001f, 0 }, (Vector2){ 200, 200 },
              ColorLerp((Color){ 88, 120, 78, 255 }, (Color){ 40, 44, 52, 255 }, world.sky));
    if (world.show_grid) sfxr_draw_floor_grid(20, (Color){ 255, 255, 255, 70 }, (Color){ 255, 255, 255, 30 });

    // landmarks for scale, and to make turning and teleporting readable
    for (int i = 0; i < 12; i++) {
        float a = (float)i / 12.0f * 2.0f * PI;
        Vector3 base = { cosf(a) * 26.0f, 0, sinf(a) * 26.0f };   // well clear of the row and the yard
        float h = 2.0f + (float)(i % 3);
        DrawCylinder(base, 0.25f, 0.25f, h, 8, (Color){ 150, 140, 125, 255 });
        DrawSphereEx((Vector3){ base.x, h + 0.6f, base.z }, 0.9f, 6, 8, (Color){ 70, 130, 70, 255 });
    }

    // the workbench
    DrawCube((Vector3){ 0, TABLE_Y - 0.025f, TABLE_Z }, TABLE_W, 0.05f, TABLE_D, (Color){ 140, 100, 70, 255 });
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? TABLE_W * 0.45f : -TABLE_W * 0.45f;
        float z = TABLE_Z + ((i & 2) ? TABLE_D * 0.4f : -TABLE_D * 0.4f);
        DrawCube((Vector3){ x, (TABLE_Y - 0.05f) * 0.5f, z }, 0.05f, TABLE_Y - 0.05f, 0.05f, (Color){ 100, 72, 50, 255 });
    }

    // the lift platform, driven by the LIFT slider (a surface: yard.c)
    float lift_h = world_lift_height();
    DrawCube((Vector3){ LIFT_X, lift_h * 0.5f, LIFT_Z }, 1.0f, lift_h, 1.0f, (Color){ 90, 96, 110, 255 });
    DrawCubeWires((Vector3){ LIFT_X, lift_h * 0.5f, LIFT_Z }, 1.0f, lift_h, 1.0f, (Color){ 30, 32, 40, 255 });
}

float world_lift_height(void) { return 0.05f + world.lift * 1.5f; }
