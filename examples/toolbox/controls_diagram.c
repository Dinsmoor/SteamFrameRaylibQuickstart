// controls_diagram.c - the controls, where you start: both controllers,
// three times life size, with a callout on every control saying what it
// does here. A template for any game's "how to play" board.
//
// The models are the headset's own (sfxr_controller_model: the runtime's
// render model of the controller in your hand). Where the runtime has none
// (the simulator, the tests, or before a controller has been seen) a stand-in
// is drawn from boxes and cylinders placed where the Frame's controls are.
//
// Every control's spot is in GRIP space (the pose sfxr gives as hand->grip),
// measured from the Frame controllers' render models in SteamVR's driver, so
// the callouts land on the same spots whichever model is drawn. The labels
// follow the player's main hand (Hands-on setup): a left-handed player's
// radial menu is on the left bumper, and the board says so.

#include "toolbox.h"
#include "rlgl.h"

#define DIAGRAM_SCALE 3.5f

// Spots on the RIGHT controller, grip space (m); the left one mirrors x.
// `out` is the way the leader leaves the control (so the dot sits on the
// surface, not inside it).
typedef enum { C_STICK, C_TRIGGER, C_GRIP, C_BUMPER, C_A, C_B, C_X, C_Y, C_DPAD, C_MENU, C_PALM, C_WATCH, C_COUNT } Spot;
static const struct { Vector3 at, out; } SPOT[C_COUNT] = {
    [C_STICK]   = { { -0.0255f,  0.0150f, -0.0500f }, { -0.2f,  1.0f, -0.6f } },
    [C_TRIGGER] = { { -0.0100f, -0.0420f, -0.0480f }, {  0.0f, -1.0f, -0.3f } },
    [C_GRIP]    = { { -0.0230f, -0.0018f, -0.0138f }, { -1.0f,  0.0f,  0.0f } },
    [C_BUMPER]  = { { -0.0109f, -0.0279f, -0.0660f }, {  0.0f, -0.3f, -1.0f } },
    [C_A]       = { {  0.0017f,  0.0100f, -0.0530f }, { -0.2f,  1.0f, -0.6f } },
    [C_B]       = { {  0.0095f,  0.0050f, -0.0610f }, { -0.2f,  1.0f, -0.6f } },
    [C_X]       = { { -0.0078f,  0.0040f, -0.0580f }, { -0.2f,  1.0f, -0.6f } },
    [C_Y]       = { {  0.0000f, -0.0010f, -0.0660f }, { -0.2f,  1.0f, -0.6f } },
    [C_DPAD]    = { {  0.0013f,  0.0040f, -0.0590f }, { -0.2f,  1.0f, -0.6f } },   // (left only; mirrored like the rest)
    [C_MENU]    = { { -0.0160f, -0.0071f, -0.0700f }, {  0.0f,  0.2f, -1.0f } },   // Menu (right), View (left)
    [C_PALM]    = { { -0.0300f, -0.0100f,  0.0300f }, { -1.0f,  0.0f,  0.0f } },   // where the palm wraps the handle
    [C_WATCH]   = { {  0.0000f,  0.0150f,  0.0550f }, {  0.0f,  1.0f,  0.3f } },   // the back of the wrist, behind the controller
};

typedef struct { Spot spot; const char *text; } Callout;

static struct {
    const Model *model[2];
    SfxrPose     model_in_grip[2];   // the runtime model's pose relative to the grip
    bool         have_model[2];
    float        half_w, bottom;     // the board, sized to its labels (m, from its middle)
} D;

static Vector3 spot_at(int hand, Spot s)
{
    Vector3 p = SPOT[s].at;
    if (!hand) p.x = -p.x;
    return p;
}

static Vector3 spot_out(int hand, Spot s)
{
    Vector3 o = Vector3Normalize(SPOT[s].out);
    if (!hand) o.x = -o.x;
    return o;
}

// Where the diagram stands: straight ahead of where you start, behind the
// workbench and above it (under the Workbench sign), 1.5 m from your eyes.
static SfxrPose board_pose(void)
{
    return (SfxrPose){ { 0, 1.66f, TABLE_Z - TABLE_D * 0.5f - 0.3f }, QuaternionIdentity() };
}

// Each controller on the board face-on, like a gamepad in a manual: its
// buttons toward you, its front (the trigger end) up. Tipped back a little so
// the trigger and bumper along the top edge show too.
static SfxrPose controller_pose(int hand)
{
    Vector3 n = Vector3Normalize((Vector3){ hand ? -0.165f : 0.165f, 0.777f, -0.607f });   // out of the face
    Vector3 fwd = { 0, 0, -1 };
    Vector3 y = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(n, Vector3DotProduct(fwd, n))));
    Vector3 x = Vector3CrossProduct(y, n);
    Matrix m = { x.x, x.y, x.z, 0,  y.x, y.y, y.z, 0,  n.x, n.y, n.z, 0,  0, 0, 0, 1 };   // grip -> board
    Quaternion q = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -20.0f * DEG2RAD),
                                      QuaternionFromMatrix(m));
    SfxrPose at = { { hand ? 0.2f : -0.2f, -0.05f, 0.02f }, q };
    return sfxr_pose_mul(board_pose(), at);
}

// A grip-space point on the diagram's controller, in the world.
static Vector3 on_diagram(int hand, Vector3 grip_local)
{
    return sfxr_pose_apply(controller_pose(hand), Vector3Scale(grip_local, DIAGRAM_SCALE));
}

// What each control does in the toolbox, for the main hand and the other.
static int callouts(int hand, Callout *out)
{
    bool main = (SfxrHandId)hand == menus_main_hand();
    bool right = hand == 1;
    int n = 0;
    out[n++] = (Callout){ C_STICK, "Stick: push forward, let go: jump there\nleft or right: turn" };
    out[n++] = (Callout){ C_TRIGGER, "Trigger: use, or click with the laser" };
    out[n++] = (Callout){ C_GRIP, "Grip: grab and hold" };
    if (main) out[n++] = (Callout){ C_BUMPER, "Bumper: hold for the hand menu,\ntilt the stick, let go" };
    else      out[n++] = (Callout){ C_BUMPER, "Bumper: in the garden, hold and speak" };
    if (right) out[n++] = (Callout){ C_A, "A: the guns' second action" };
    else       out[n++] = (Callout){ C_DPAD, "D-pad down: the guns' second action" };
    if (!main) out[n++] = (Callout){ C_MENU, right ? "Menu: the tablet menu" : "View: the tablet menu" };
    if (!main) out[n++] = (Callout){ C_PALM, "Palm to your face: 3 quick buttons" };
    if (!main) out[n++] = (Callout){ C_WATCH, "Wrist to your face: the watch" };
    return n;
}

void controls_diagram(void)
{
    // Learn each runtime model's place relative to the grip while the
    // controller is in your hand; the board draws it there ever after.
    for (int h = 0; h < 2; h++) {
        SfxrPose mp;
        const Model *m = sfxr_controller_model((SfxrHandId)h, &mp);
        const SfxrHand *hand = sfxr_hand((SfxrHandId)h);
        if (m && hand->active && hand->source == SFXR_SOURCE_CONTROLLER) {
            D.model[h] = m;
            D.model_in_grip[h] = sfxr_pose_relative(hand->grip, mp);
            D.have_model[h] = true;
        }
    }

    SfxrPose board = board_pose();
    Vector3 up = sfxr_pose_up(board);
    // Printed on the board, so one size: 1.5 degrees tall from where you
    // start (the board grows to fit its labels).
    const float th = 0.04f;
    float widest = 0, lowest = 0;

    SfxrPose face = sfxr_pose_mul(board, (SfxrPose){ { 0, 0, -0.065f }, QuaternionIdentity() });   // the board's front
    vrui_text_at(sfxr_pose_mul(face, (SfxrPose){ { 0, 0.37f, 0 }, QuaternionIdentity() }), "Controls", th * 1.8f, RAYWHITE);
    vrui_text_at(sfxr_pose_mul(face, (SfxrPose){ { 0, 0.29f, 0 }, QuaternionIdentity() }),
                 "Bare hands: a pinch is the trigger, a fist is the grip", th, (Color){ 200, 205, 215, 255 });

    // Callouts in two columns, the left controller's on the left: sorted top
    // to bottom by where the control is, so the leaders don't cross.
    const Color ink[2] = { { 255, 190, 150, 255 }, { 160, 200, 255, 255 } };
    for (int h = 0; h < 2; h++) {
        Callout c[C_COUNT];
        int n = callouts(h, c);
        float key[C_COUNT];
        for (int i = 0; i < n; i++)
            key[i] = Vector3DotProduct(Vector3Subtract(on_diagram(h, spot_at(h, c[i].spot)), board.position), up);
        for (int i = 1; i < n; i++)   // insertion sort, highest first
            for (int k = i; k > 0 && key[k] > key[k - 1]; k--) {
                Callout t = c[k]; c[k] = c[k - 1]; c[k - 1] = t;
                float f = key[k]; key[k] = key[k - 1]; key[k - 1] = f;
            }
        float side = h ? 1.0f : -1.0f;
        float y = 0.22f, col_x = 0.42f * side;
        for (int i = 0; i < n; i++) {
            Vector2 sz = vrui_text_size(c[i].text, th);
            // printed on the board: the label's near edge on the column line
            float cx = col_x + side * (sz.x * 0.5f + 0.012f), cy = y - sz.y * 0.5f;
            vrui_text_at(sfxr_pose_mul(face, (SfxrPose){ { cx, cy, 0 }, QuaternionIdentity() }), c[i].text, th, ink[h]);
            Vector3 edge = sfxr_pose_apply(face, (Vector3){ col_x, cy, 0.002f });
            Vector3 anchor = on_diagram(h, spot_at(h, c[i].spot));
            Vector3 dot = Vector3Add(anchor, Vector3Scale(Vector3RotateByQuaternion(spot_out(h, c[i].spot),
                                                                                    controller_pose(h).orientation), 0.004f));
            vrui_sphere(dot, 0.006f, ink[h]);
            vrui_line(dot, edge, ink[h]);
            y -= sz.y + th * 0.9f;
            widest = fmaxf(widest, sz.x);
        }
        lowest = fminf(lowest, y);
    }
    D.half_w = 0.42f + widest + 0.06f;
    D.bottom = lowest - 0.02f;

    // the board behind the controllers, and its post down to the ground
    // (scenery: they never move)
    float top = 0.44f, bottom = D.bottom, w = D.half_w * 2;
    scenery_box(sfxr_pose_mul(board, (SfxrPose){ { 0, (top + bottom) * 0.5f, -0.08f }, QuaternionIdentity() }),
                (Vector3){ w, top - bottom, 0.02f }, (Color){ 44, 50, 64, 255 }, MAT_PAINT);
    float post = board.position.y + bottom;
    scenery_box(sfxr_pose_mul(board, (SfxrPose){ { 0, bottom - post * 0.5f, -0.1f }, QuaternionIdentity() }),
                (Vector3){ 0.05f, post, 0.05f }, (Color){ 70, 74, 84, 255 }, MAT_PAINT);
}

// The stand-in: boxes and cylinders where the Frame controller's parts are
// (grip space, right hand; mirrored for the left).
static void stand_in(int hand, Color body)
{
    float mx = hand ? 1.0f : -1.0f;
    Color dark = ColorBrightness(body, -0.45f), key = (Color){ 225, 228, 235, 255 };
    // handle, running back from the head along +Z
    DrawCylinderEx((Vector3){ -0.012f * mx, -0.012f, -0.035f }, (Vector3){ -0.010f * mx, -0.018f, 0.055f }, 0.021f, 0.018f, 12, body);
    // head: a slab under the face, tipped to it
    rlPushMatrix();
        rlTranslatef(-0.012f * mx, -0.012f, -0.058f);
        rlRotatef(-38.0f, 1, 0, 0);
        DrawCube((Vector3){ 0 }, 0.064f, 0.026f, 0.042f, body);
    rlPopMatrix();
    Vector3 face = { -0.165f * mx, 0.777f, -0.607f };
    Vector3 stick = spot_at(hand, C_STICK);
    DrawCylinderEx(Vector3Subtract(stick, Vector3Scale(face, 0.006f)), Vector3Add(stick, Vector3Scale(face, 0.006f)), 0.009f, 0.009f, 12, dark);
    DrawSphereEx(Vector3Add(stick, Vector3Scale(face, 0.007f)), 0.009f, 6, 10, dark);
    if (hand) {
        Spot keys[4] = { C_A, C_B, C_X, C_Y };
        for (int i = 0; i < 4; i++) {
            Vector3 p = spot_at(hand, keys[i]);
            DrawCylinderEx(Vector3Subtract(p, Vector3Scale(face, 0.004f)), Vector3Add(p, Vector3Scale(face, 0.003f)), 0.005f, 0.005f, 10, key);
        }
    } else {
        Vector3 p = spot_at(hand, C_DPAD);
        rlPushMatrix();
            rlTranslatef(p.x, p.y, p.z);
            rlRotatef(-38.0f, 1, 0, 0);
            DrawCube((Vector3){ 0 }, 0.020f, 0.004f, 0.007f, key);
            DrawCube((Vector3){ 0 }, 0.007f, 0.004f, 0.020f, key);
        rlPopMatrix();
    }
    Vector3 t = spot_at(hand, C_TRIGGER), b = spot_at(hand, C_BUMPER), g = spot_at(hand, C_GRIP), m = spot_at(hand, C_MENU);
    DrawCube(Vector3Add(t, (Vector3){ 0, 0.006f, 0 }), 0.018f, 0.018f, 0.012f, dark);
    DrawCube(Vector3Add(b, (Vector3){ 0, 0, 0.006f }), 0.030f, 0.010f, 0.012f, dark);
    DrawCube(Vector3Add(g, (Vector3){ 0.004f * mx, 0, 0 }), 0.006f, 0.022f, 0.020f, dark);
    DrawCube(Vector3Add(m, (Vector3){ 0, 0, 0.003f }), 0.009f, 0.005f, 0.005f, key);
}

void controls_diagram_draw(void)
{
    for (int h = 0; h < 2; h++) {
        sfxr_push_pose(controller_pose(h));
            rlScalef(DIAGRAM_SCALE, DIAGRAM_SCALE, DIAGRAM_SCALE);
            if (D.have_model[h]) {
                sfxr_push_pose(D.model_in_grip[h]);
                    DrawModel(*D.model[h], (Vector3){ 0 }, 1.0f, WHITE);
                sfxr_pop_pose();
            } else {
                stand_in(h, h ? (Color){ 70, 120, 200, 255 } : (Color){ 200, 110, 70, 255 });
            }
        sfxr_pop_pose();
    }
}
