// station_hinges.c - Hinges & cords, at the right end of the row: the
// big-motion mechanisms (docs/MECHANISMS.md).
//
//   Door       take the handle and walk it round: heavy, stops at the frame
//   Chest lid  the same hinge on its side (vrui_door with a turned hinge pose)
//   Bell cord  pull it all the way: rings once per pull, jiggling doesn't re-ring
//   Radio dial a tuning dial: ticks every 5, rests anywhere
//   Valve      a stuck valve on a pipe: needs BOTH hands on the wheel; a
//              gauge shows the pressure it lets through
//   Key switch take the key off its hook, push it in, turn it: OFF, ON,
//              START (springs back to ON, and the engine lamp lights)
//
// Everything here is placed in the station's own frame (row_pose, facing
// the middle): +X to your right as you face it, +Z toward you.

#include "toolbox.h"
#include "sfxr_break.h"

#define BREAK SFXR_BREAK_DECLARE
#include "toolbox_breaks.def"
#undef BREAK

static struct {
    float door, lid, cord, dial, valve;
    int rings;
    float flash;       // bell lamp glow, seconds left
    SfxrPose key;      // the key: on its hook, in your hand, or in the slot
    bool key_placed, running;
    int ignition;      // 0 OFF, 1 ON, 2 START
} H = { .dial = 40 };

static SfxrPose origin(void) { return row_pose(13.9f, 0); }
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
    // the frequency is printed on the radio's face (vrui_text_at: fixed to a
    // pose, readable from the front only), not floating toward you
    vrui_text_at(local(1.78f, 0.97f, 0.012f, I), TextFormat("FM %.1f", 88.0f + H.dial * 0.2f),
                 0.022f, (Color){ 255, 200, 120, 255 });

    // --- a valve on a pipe: the wheel faces you (base +Y toward you), and a
    // gauge further up the pipe shows the pressure it lets through
    Quaternion facing_you = QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2);   // +Y -> +Z
    vrui_box(local(2.6f, 1.0f, -0.12f, I), (Vector3){ 0.1f, 2.0f, 0.1f }, (Color){ 120, 124, 132, 255 });
    VruiMechSpec vs = vrui_valve_spec();
    vs.label = "VALVE";
    vrui_valve(VRUI_ID2(G_HINGE, 5), local(2.6f, 1.15f, -0.07f, facing_you), &vs, &H.valve);
    float bar = SFXR_BREAK(toolbox_valve_unwired) ? 0 : H.valve * 8.0f;
    vrui_gauge(VRUI_ID2(G_HINGE, 6), local(2.6f, 1.75f, -0.06f, I), bar, 0, 8, "bar");
    sfxr_report("pressure", bar);

    // --- a key switch on a post, its key hanging on a hook beside it
    SfxrPose hook = local(3.45f, 1.3f, -0.02f, I);   // the key hangs tip down: +Y up
    if (!H.key_placed) { H.key = hook; H.key_placed = true; }
    vrui_box(local(3.2f, 0.55f, -0.1f, I), (Vector3){ 0.08f, 1.1f, 0.08f }, metal);
    vrui_box(local(3.2f, 1.15f, -0.08f, I), (Vector3){ 0.2f, 0.2f, 0.04f }, (Color){ 50, 54, 62, 255 });
    vrui_box(local(3.45f, 1.39f, -0.04f, I), (Vector3){ 0.01f, 0.03f, 0.04f }, metal);   // the hook
    static const char *const IGNITION[] = { "OFF", "ON", "START" };
    VruiKeySpec ks = vrui_key_spec(3);
    ks.spring_last = true;
    ks.names = IGNITION;
    ks.label = "KEY";
    VruiKey k = vrui_key_switch(VRUI_ID2(G_HINGE, 7), local(3.2f, 1.15f, -0.06f, facing_you), &ks, &H.key, &H.ignition);
    // Let go of the key anywhere but the slot and it swings back to its hook
    // (a retractable chain: the app decides where a loose key goes).
    if (!k.held && !k.inserted) H.key = sfxr_pose_lerp(H.key, hook, 1.0f - expf(-sfxr_dt() * 6.0f));
    if (!k.inserted) vrui_line(sfxr_pose_apply(hook, (Vector3){ 0, 0.09f, 0 }), sfxr_pose_apply(H.key, (Vector3){ 0, 0.075f, 0 }),
                               (Color){ 180, 180, 190, 255 });
    sfxr_report("ignition", (float)H.ignition);
    sfxr_report("rings", (float)H.rings);
    if (H.ignition == 2) H.running = true;   // START cranks it
    if (H.ignition == 0) H.running = false;
    vrui_lamp(local(3.2f, 1.26f, -0.08f, I), H.running, (Color){ 90, 230, 120, 255 }, H.running ? "running" : "engine");

    station_sign(13.9f + 3.3f, "Hinges & cords", "a door, a chest lid, a bell cord, a radio dial,\na two-handed valve, a key switch");
}
