// station_hinges.c - Hinges & cords, out to your right past the benches:
// the big-motion mechanisms (docs/MECHANISMS.md).
//
//   Door       take the handle and walk it round: heavy, stops at the frame
//   Chest lid  the same hinge on its side (vrui_door with a turned hinge pose)
//   Bell cord  pull it all the way: rings once per pull, jiggling doesn't re-ring
//   Radio dial a tuning dial: ticks every 5, rests anywhere
//
// Everything here is placed in the station's own frame (station_pose, facing
// the middle): +X to your right as you face it, +Z toward you.

#include "toolbox.h"

static struct {
    float door, lid, cord, dial;
    int rings;
    float flash;       // bell lamp glow, seconds left
} H = { .dial = 40 };

static SfxrPose origin(void) { return station_pose(90, 4.2f, 0); }
static SfxrPose local(float x, float y, float z, Quaternion q)
{
    return sfxr_pose_mul(origin(), (SfxrPose){ { x, y, z }, q });
}

void station_hinges(void)
{
    Quaternion I = QuaternionIdentity();
    Color wood = { 110, 80, 55, 255 }, metal = { 80, 84, 96, 255 };

    // --- a door in a frame
    vrui_box(local(-1.24f, 1.05f, 0, I), (Vector3){ 0.06f, 2.1f, 0.1f }, wood);
    vrui_box(local(-0.36f, 1.05f, 0, I), (Vector3){ 0.06f, 2.1f, 0.1f }, wood);
    vrui_box(local(-0.8f, 2.13f, 0, I), (Vector3){ 0.94f, 0.06f, 0.1f }, wood);
    vrui_door(VRUI_ID2(G_HINGE, 1), local(-1.2f, 0.02f, 0.03f, I), 0.8f, 2.05f, &H.door, "DOOR");

    // --- a chest; its lid is a door on its side: hinge along the back top edge
    // (hinge +Y = the chest's X), handle side toward you (+X = +Z), front up
    vrui_box(local(0.1f, 0.22f, 0, I), (Vector3){ 0.6f, 0.44f, 0.4f }, wood);
    Matrix m = { 0, 1, 0, 0,
                 0, 0, 1, 0,
                 1, 0, 0, 0,
                 0, 0, 0, 1 };   // columns: hinge X = (0,0,1), Y = (1,0,0), Z = (0,1,0)
    Quaternion lid_q = QuaternionFromMatrix(m);
    vrui_door(VRUI_ID2(G_HINGE, 2), local(-0.2f, 0.46f, -0.2f, lid_q), 0.4f, 0.6f, &H.lid, "LID");

    // --- a bell cord hanging from a post, and a lamp that rings with it
    vrui_box(local(0.95f, 1.05f, -0.1f, I), (Vector3){ 0.06f, 2.1f, 0.06f }, metal);
    vrui_box(local(0.95f, 2.07f, 0.05f, I), (Vector3){ 0.05f, 0.05f, 0.3f }, metal);
    if (vrui_pull_cord(VRUI_ID2(G_HINGE, 3), local(0.95f, 2.03f, 0.18f, I), &H.cord, "BELL")) {
        H.rings++;
        H.flash = 0.4f;
    }
    H.flash = fmaxf(0, H.flash - sfxr_dt());
    vrui_lamp(local(0.95f, 2.12f, -0.1f, I), H.flash > 0, (Color){ 255, 210, 80, 255 },
              TextFormat("rang %d", H.rings));

    // --- a radio with a tuning dial on its face (the dial turns about the face's normal)
    vrui_box(local(1.7f, 0.95f, -0.05f, I), (Vector3){ 0.4f, 0.25f, 0.12f }, (Color){ 70, 60, 50, 255 });
    vrui_box(local(1.7f, 0.41f, -0.05f, I), (Vector3){ 0.06f, 0.82f, 0.06f }, metal);
    VruiMechSpec ds = vrui_dial_spec();
    ds.label = "TUNE";
    ds.value_format = NULL;
    vrui_rotary(VRUI_ID2(G_HINGE, 4), local(1.62f, 0.95f, 0.01f, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2)),
                &ds, &H.dial);
    vrui_text3d(sfxr_pose_apply(origin(), (Vector3){ 1.78f, 0.97f, 0.03f }), TextFormat("FM %.1f", 88.0f + H.dial * 0.2f),
                0.025f, (Color){ 255, 200, 120, 255 });

    vrui_text3d(sfxr_pose_apply(origin(), (Vector3){ 0.2f, 2.5f, 0 }), "Hinges & cords", 0.05f, RAYWHITE);
}
