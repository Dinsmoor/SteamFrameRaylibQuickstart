// tests/collide - the collision queries keep their promises (vrui.h section
// 15): a ray stops at the first solid thing, a fast ball doesn't pass through
// a thin board, the ground under a point is the highest thing below it, and
// things are solid from the frame after they're declared.
//
//   make test T=collide

#include "sfxt.h"

#include <math.h>

enum { WOOD = 7, FLOOR = 3, POST = 9, BALL = 11, LATE = 13 };

static bool late;   // a sixth thing, declared once a case says so

static void scene(void)
{
    if (late) vrui_collider_sphere((Vector3){ 0, 1, -3 }, 0.2f, LATE);
    vrui_collider_ground(0, FLOOR);
    vrui_collider_box((SfxrPose){ { 0, 0.875f, -1 }, QuaternionIdentity() }, (Vector3){ 1.2f, 0.05f, 0.6f }, WOOD);   // a table top, 0.9 m high
    vrui_collider_box((SfxrPose){ { 2, 1, 0 }, QuaternionIdentity() }, (Vector3){ 1, 1, 0.02f }, WOOD);              // a 2 cm board
    vrui_collider_capsule((Vector3){ -2, 0, 0 }, (Vector3){ -2, 2, 0 }, 0.1f, POST);                                // a post
    vrui_collider_sphere((Vector3){ 0, 1, 3 }, 0.25f, BALL);
}

static void setup(void) { sfxt_noise(0, 0, 0); sfxt_frames(2); }

// Straight down onto the table: the top, facing up, tagged wood.
static void ray_stops_at_the_table_top(void)
{
    setup();
    VruiHit h = vrui_raycast((Vector3){ 0.1f, 2, -1 }, (Vector3){ 0, -1, 0 }, 5);
    CHECK(h.hit && h.tag == WOOD, "hits the table (tag %d)", h.tag);
    CHECK_NEAR(h.point.y, 0.9f, 0.001f, "at its top");
    CHECK(h.normal.y > 0.99f, "facing up");
    h = vrui_raycast((Vector3){ 1, 2, -1 }, (Vector3){ 0, -1, 0 }, 5);
    CHECK(h.hit && h.tag == FLOOR && fabsf(h.point.y) < 0.001f, "beside it: the floor");
    h = vrui_raycast((Vector3){ 0, 1, 1 }, (Vector3){ 0, 0, 1 }, 5);
    CHECK(h.hit && h.tag == BALL, "along: the ball");
    CHECK_NEAR(h.distance, 1.75f, 0.001f, "at its surface");
    h = vrui_raycast((Vector3){ -1, 1, 0 }, (Vector3){ -1, 0, 0 }, 5);
    CHECK(h.hit && h.tag == POST, "sideways: the post");
    CHECK_NEAR(h.distance, 0.9f, 0.001f, "at its side");
}

// A ball moving a meter in one frame through a 2 cm board is stopped at the
// board's face: its center one radius short of it.
static void fast_ball_stops_at_a_thin_board(void)
{
    setup();
    VruiHit h = vrui_spherecast((Vector3){ 2, 1, -0.5f }, (Vector3){ 2, 1, 0.5f }, 0.05f);
    CHECK(h.hit, "the board stops it");
    CHECK_NEAR(h.center.z, -0.06f, 0.002f, "its center a radius short of the face");
    CHECK(h.normal.z < -0.99f, "the face turned toward it");
}

// What's under a point: the highest solid thing below, not above.
static void ground_is_what_is_below(void)
{
    setup();
    int tag;
    float y = vrui_ground((Vector3){ 0, 1.5f, -1 }, &tag);
    CHECK(fabsf(y - 0.9f) < 0.001f && tag == WOOD, "over the table: its top (%.3f, tag %d)", y, tag);
    y = vrui_ground((Vector3){ 0, 0.5f, -1 }, &tag);
    CHECK(fabsf(y) < 0.001f && tag == FLOOR, "under the table: the floor (%.3f, tag %d)", y, tag);
}

// Declared this frame, solid to queries next frame (they see the last complete set).
static void solid_from_the_next_frame(void)
{
    setup();
    CHECK(vrui_collider_count() == 5, "five things (%d)", vrui_collider_count());
    late = true;
    sfxt_frames(1);   // declared during this frame...
    VruiHit h = vrui_raycast((Vector3){ 0, 1, -1.5f }, (Vector3){ 0, 0, -1 }, 5);
    CHECK(!(h.hit && h.tag == LATE), "...not solid yet in the same frame");
    sfxt_frames(1);   // ...solid from the next
    h = vrui_raycast((Vector3){ 0, 1, -1.5f }, (Vector3){ 0, 0, -1 }, 5);
    CHECK(h.hit && h.tag == LATE, "solid the frame after (tag %d)", h.tag);
}

static const SfxtCase CASES[] = {
    { "collide/ray-stops-at-the-table-top",      ray_stops_at_the_table_top,      NULL },
    { "collide/fast-ball-stops-at-a-thin-board", fast_ball_stops_at_a_thin_board, "vrui_spherecast_endpoint_only" },
    { "collide/ground-is-what-is-below",         ground_is_what_is_below,         NULL },
    { "collide/solid-from-the-next-frame",       solid_from_the_next_frame,       NULL },
};

int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
