// onboarding.c - the setup as a table of tasks (see onboarding.h and
// docs/ONBOARDING.md).
//
// Each task is { instruction, hint, setup, update }. update() runs every
// frame, watches HOW the player does the task (which hand, which button, how
// hard, twist or drag, poke or laser) and returns true once it's done. The
// hint only appears if the player seems stuck (no success after a few
// seconds), so people who already know VR aren't lectured.
//
// While tasks run, vrui is set to accept every style (grip OR trigger grabs,
// a soft pull), so any natural attempt works and can be observed. At the end
// the player sees what was learned, can change it, and it's saved.

#include "onboarding.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { G_ONB = 0x70 };
#define ID(n) VRUI_ID2(G_ONB, (n))
#define HINT_AFTER 6.0f      // seconds without success before the hint shows

// What we saw, across tasks.
static struct {
    int   hand_votes[2];
    int   trigger_grabs, grip_grabs;
    float grip_peak;                // how hard they squeezed while holding the cube
    float twist_sum, orbit_sum;     // radians of each while turning the dial
    int   pokes, laser_presses, point_pokes;
    float trigger_peak;             // how hard they pulled to click the far target
} O;

static struct {
    bool active;
    int  task;
    double task_start;
    SfxrPose tray;                  // where the task props sit (placed at start)
    SfxrPose cube;
    float dial;                     // 0..1, target 0.8
    Vector3 dial_prev_grip; float dial_prev_twist; bool dial_holding;
    bool hold_seen; double hold_since;
    Prefs prefs;
    const char *path;
    VruiStyle saved_style;          // restored after the setup
} OB;

// --- preferences file -------------------------------------------------------

static void save_prefs(void)
{
    FILE *f = OB.path ? fopen(OB.path, "w") : NULL;
    if (!f) return;
    const Prefs *p = &OB.prefs;
    fprintf(f, "dominant=%s\ngrab=%d\npull=%d\npoint_to_press=%d\nknob_orbiter=%d\nlaser_first=%d\n",
            p->dominant == SFXR_LEFT ? "left" : "right", (int)p->grab, (int)p->pull, p->point_to_press,
            p->knob_orbiter, p->laser_first);
    fclose(f);
}

static bool load_prefs(void)
{
    FILE *f = OB.path ? fopen(OB.path, "r") : NULL;
    if (!f) return false;
    char line[128];
    Prefs p = { SFXR_RIGHT, VRUI_GRAB_GRIP, SFXR_PULL_FIRM, false, false, false, true };
    while (fgets(line, sizeof line, f)) {
        char key[64]; char val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) != 2) continue;
        if (!strcmp(key, "dominant")) p.dominant = strcmp(val, "left") ? SFXR_RIGHT : SFXR_LEFT;
        else if (!strcmp(key, "grab")) p.grab = (VruiGrabStyle)atoi(val);
        else if (!strcmp(key, "pull")) p.pull = (SfxrPull)atoi(val);
        else if (!strcmp(key, "point_to_press")) p.point_to_press = atoi(val) != 0;
        else if (!strcmp(key, "knob_orbiter")) p.knob_orbiter = atoi(val) != 0;
        else if (!strcmp(key, "laser_first")) p.laser_first = atoi(val) != 0;
    }
    fclose(f);
    OB.prefs = p;
    return true;
}

void onboarding_apply(const Prefs *p)
{
    vrui_style()->grab = p->grab;
    vrui_style()->pull = p->pull;
    // The app maps the rest itself (see toolbox main.c: the wrist panel goes
    // on the non-dominant hand, poke buttons require a point if preferred).
}

// --- helpers -------------------------------------------------------------------

static SfxrPose on_tray(float x, float y, float z) { return sfxr_pose_mul(OB.tray, (SfxrPose){ { x, y, z }, QuaternionIdentity() }); }
static float task_time(void) { return (float)(sfxr_time() - OB.task_start); }
static void vote(SfxrHandId h) { O.hand_votes[h == SFXR_RIGHT]++; }

static void instruction_panel(const char *title, const char *text, const char *hint)
{
    SfxrPose p = on_tray(0, 0.28f, -0.16f);
    vrui_panel_capture(VRUI_CAPTURE_POINT);
    if (!vrui_panel_begin(ID(1), &p, 0.65f, 0.26f, title)) return;
    vrui_layout_begin(vrui_panel_content(), 4);
    vrui_label(vrui_row(28), text);
    if (hint && task_time() > HINT_AFTER) vrui_label(vrui_row(24), TextFormat("Tip: %s", hint));
    Rectangle r = vrui_row(30);
    r.x += r.width - 120; r.width = 120;
    if (vrui_button(2, r, "Skip setup")) { OB.active = false; vrui_style()->grab = OB.saved_style.grab; vrui_style()->pull = OB.saved_style.pull; }
    vrui_panel_end();
}

static void tray(void)
{
    vrui_box(on_tray(0, -0.015f, 0), (Vector3){ 0.6f, 0.03f, 0.3f }, (Color){ 70, 74, 86, 255 });
}

// --- the tasks ------------------------------------------------------------------

static bool task_grab(void)
{
    tray();
    instruction_panel("Setup 1/4", "Pick up the cube.", "reach out and squeeze the grip with your middle finger");
    VruiGrab g = vrui_grab_region(ID(10), &OB.cube, (Vector3){ 0.03f, 0.03f, 0.03f });
    vrui_box(OB.cube, (Vector3){ 0.06f, 0.06f, 0.06f }, g.held ? (Color){ 255, 200, 90, 255 } : (Color){ 230, 150, 60, 255 });
    if (g.grabbed) {
        const SfxrHand *h = sfxr_hand(g.hand);
        vote(g.hand);
        if (h->trigger > h->squeeze) O.trigger_grabs++; else O.grip_grabs++;
        OB.hold_since = sfxr_time();
        O.grip_peak = 0;
    }
    if (g.held) {
        const SfxrHand *h = sfxr_hand(g.hand);
        O.grip_peak = fmaxf(O.grip_peak, fmaxf(h->squeeze, h->trigger));   // how hard they naturally hold
        if (sfxr_time() - OB.hold_since > 0.8) return true;                 // held it for a moment: done
    }
    return false;
}

static bool task_dial(void)
{
    tray();
    instruction_panel("Setup 2/4", "Turn the dial to the green mark.", "grab it, then twist your wrist or drag your hand around it");
    VruiMechSpec s = vrui_knob_spec();
    SfxrPose base = on_tray(0, 0, 0.02f);
    VruiMech m = vrui_rotary(ID(11), base, &s, &OB.dial);
    // the target mark at 0.8
    float a = -0.8f * s.travel;
    Quaternion q = QuaternionMultiply(base.orientation, QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, a));
    Vector3 p0 = Vector3Add(base.position, Vector3RotateByQuaternion((Vector3){ 0, 0.001f, -s.size * 1.2f }, q));
    Vector3 p1 = Vector3Add(base.position, Vector3RotateByQuaternion((Vector3){ 0, 0.001f, -s.size * 1.2f - 0.015f }, q));
    vrui_box((SfxrPose){ Vector3Lerp(p0, p1, 0.5f), q }, (Vector3){ 0.006f, 0.004f, 0.02f }, (Color){ 80, 220, 110, 255 });
    // observe: wrist twist vs hand dragging around the dial
    if (m.held) {
        const SfxrHand *h = sfxr_hand(m.hand);
        Vector3 axis = { 0, 1, 0 };
        float tw = 2.0f * atan2f(Vector3DotProduct((Vector3){ h->grip.orientation.x, h->grip.orientation.y, h->grip.orientation.z }, axis),
                                 h->grip.orientation.w);
        Vector3 r = Vector3Subtract(h->grip.position, base.position);
        if (OB.dial_holding) {
            float dt_w = tw - OB.dial_prev_twist;
            while (dt_w > PI) dt_w -= 2 * PI;
            while (dt_w < -PI) dt_w += 2 * PI;
            O.twist_sum += fabsf(dt_w);
            Vector3 pr = Vector3Subtract(OB.dial_prev_grip, base.position);
            float d_orbit = atan2f(r.z, r.x) - atan2f(pr.z, pr.x);
            while (d_orbit > PI) d_orbit -= 2 * PI;
            while (d_orbit < -PI) d_orbit += 2 * PI;
            if (sqrtf(r.x * r.x + r.z * r.z) > 0.02f) O.orbit_sum += fabsf(d_orbit);
        } else vote(m.hand);
        OB.dial_holding = true;
        OB.dial_prev_twist = tw;
        OB.dial_prev_grip = h->grip.position;
    } else OB.dial_holding = false;
    return !m.held && fabsf(OB.dial - 0.8f) < 0.06f;
}

static bool task_press(void)
{
    tray();
    instruction_panel("Setup 3/4", "Press the red button.", "poke it with the tip of the controller, or point the laser and pull the trigger");
    VruiPressSpec b = vrui_press_spec();
    SfxrPose base = on_tray(0.12f, 0, 0.03f);
    VruiPress p = vrui_press(ID(12), base, &b, NULL);
    if (p.pressed) {
        // poked (a tip is right at the cap) or pressed by laser?
        int near_h = -1;
        for (int h = 0; h < 2; h++)
            if (Vector3Distance(sfxr_hand((SfxrHandId)h)->poke.position, p.part.position) < 0.04f) near_h = h;
        if (near_h >= 0) {
            O.pokes++;
            vote((SfxrHandId)near_h);
            if (sfxr_hand((SfxrHandId)near_h)->shape == SFXR_SHAPE_POINT) O.point_pokes++;
        } else O.laser_presses++;
        return true;
    }
    return false;
}

static bool task_far(void)
{
    tray();
    instruction_panel("Setup 4/4", "Point at the far target and click it.", "aim the laser from your controller at it and pull the trigger");
    SfxrPose far = on_tray(0, 0.6f, -2.2f);
    far.orientation = QuaternionMultiply(OB.tray.orientation, QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, PI / 2));
    for (int h = 0; h < 2; h++) O.trigger_peak = fmaxf(O.trigger_peak, vrui_hand_hovering((SfxrHandId)h) ? sfxr_hand((SfxrHandId)h)->trigger : 0);
    VruiPressSpec b = vrui_press_spec();
    b.radius = 0.12f;
    b.color = (Color){ 80, 170, 255, 255 };
    VruiPress p = vrui_press(ID(13), far, &b, NULL);
    if (p.pressed) {
        for (int h = 0; h < 2; h++) if (vrui_hand_hovering((SfxrHandId)h)) vote((SfxrHandId)h);
        return true;
    }
    return false;
}

// What it all means, and a chance to change it.
static bool task_summary(void)
{
    Prefs *p = &OB.prefs;
    SfxrPose pp = on_tray(0, 0.3f, -0.16f);
    if (!vrui_panel_begin(ID(2), &pp, 0.65f, 0.546f, "Setup: what I noticed")) return false;
    vrui_layout_begin(vrui_panel_content(), 5);
    Rectangle cols[2];
    // the choices shown are read from (and written back to) the preferences
    int dom = p->dominant == SFXR_RIGHT;
    int grab = p->grab == VRUI_GRAB_GRIP ? 0 : 1;
    int pull = p->pull == SFXR_PULL_SOFT ? 0 : 1;
    vrui_label(vrui_row(24), "Main hand");
    static const char *const hands[] = { "Left", "Right" };
    if (vrui_segmented(3, vrui_row(30), hands, 2, &dom)) p->dominant = dom ? SFXR_RIGHT : SFXR_LEFT;
    vrui_label(vrui_row(24), "Grab things with");
    static const char *const grabs[] = { "Grip", "Grip or trigger" };
    if (vrui_segmented(4, vrui_row(30), grabs, 2, &grab)) p->grab = grab ? VRUI_GRAB_GRIP_OR_TRIGGER : VRUI_GRAB_GRIP;
    vrui_label(vrui_row(24), "Pull needed");
    static const char *const pulls[] = { "Light", "Half way" };
    if (vrui_segmented(5, vrui_row(30), pulls, 2, &pull)) p->pull = pull ? SFXR_PULL_FIRM : SFXR_PULL_SOFT;
    vrui_label(vrui_row(22), TextFormat("Knobs: you %s.  Buttons: you %s.", p->knob_orbiter ? "drag around" : "twist",
                                        p->point_to_press ? "point" : "poke or click"));
    vrui_row_cols(34, 2, cols);
    bool done = vrui_button(6, cols[0], "Save and go");
    if (vrui_button(7, cols[1], "Try again")) onboarding_start();
    vrui_panel_end();
    return done;
}

typedef struct { bool (*update)(void); } Task;
static const Task TASKS[] = { { task_grab }, { task_dial }, { task_press }, { task_far }, { task_summary } };
#define NTASKS ((int)(sizeof TASKS / sizeof TASKS[0]))

// Turn observations into preferences (the player can still change them).
static void infer(void)
{
    Prefs *p = &OB.prefs;
    p->dominant = O.hand_votes[0] > O.hand_votes[1] ? SFXR_LEFT : SFXR_RIGHT;
    p->grab = O.trigger_grabs > 0 ? VRUI_GRAB_GRIP_OR_TRIGGER : VRUI_GRAB_GRIP;
    // never squeezed past about half while holding -> they want a light pull
    float peak = fmaxf(O.grip_peak, O.trigger_peak);
    p->pull = peak > 0 && peak < 0.5f ? SFXR_PULL_SOFT : SFXR_PULL_FIRM;
    p->point_to_press = O.point_pokes > 0;
    p->knob_orbiter = O.orbit_sum > O.twist_sum;
    p->laser_first = O.laser_presses > O.pokes;
    p->valid = true;
}

// --- driver ----------------------------------------------------------------------

void onboarding_start(void)
{
    memset(&O, 0, sizeof O);
    OB.task = 0;
    OB.task_start = sfxr_time();
    OB.tray = vrui_in_front_of_head(0.3f, 0.4f);   // floats just in front of you, above any table
    OB.cube = on_tray(-0.15f, 0.035f, 0.02f);
    OB.dial = 0.2f;
    if (!OB.active) OB.saved_style = *vrui_style();   // restored if skipped ("Try again" keeps the first copy)
    OB.active = true;
    // accept every style while watching: any grab button, a light pull
    vrui_style()->grab = VRUI_GRAB_GRIP_OR_TRIGGER;
    vrui_style()->pull = SFXR_PULL_SOFT;
}

void onboarding_init(const char *prefs_path)
{
    OB.path = prefs_path;
    if (load_prefs()) onboarding_apply(&OB.prefs);
}

// The station: what the setup is for, the current preferences, Start / Stop.
void onboarding_panel(SfxrPose *pose)
{
    if (!vrui_panel_begin(ID(3), pose, 0.572f, 0.468f, "Hands-on setup")) return;
    vrui_layout_begin(vrui_panel_content(), 4);
    vrui_paragraph("Watches how you grab, turn, press and point, then fits the controls to you (docs/ONBOARDING.md).");
    const Prefs *p = &OB.prefs;
    if (p->valid) {
        vrui_label(vrui_row(22), TextFormat("Now: %s hand, grab with %s, %s pull",
                   p->dominant == SFXR_LEFT ? "left" : "right",
                   p->grab == VRUI_GRAB_GRIP ? "grip" : p->grab == VRUI_GRAB_CLOSE ? "closing hand" : "grip or trigger",
                   p->pull == SFXR_PULL_SOFT ? "light" : "half-way"));
    } else {
        vrui_label(vrui_row(22), "Now: defaults (not run yet)");
    }
    vrui_space(6);
    if (OB.active) {
        vrui_label(vrui_row(22), TextFormat("Running: step %d of %d", OB.task + 1, NTASKS));
        if (vrui_button(1, vrui_row(34), "Stop")) {
            OB.active = false;
            vrui_style()->grab = OB.saved_style.grab;
            vrui_style()->pull = OB.saved_style.pull;
        }
    } else if (vrui_button(1, vrui_row(34), "Start: the tasks appear in front of you")) {
        onboarding_start();
    }
    vrui_panel_end();
}

bool onboarding_active(void) { return OB.active; }
const Prefs *onboarding_prefs(void) { return &OB.prefs; }

void onboarding_update(void)
{
    if (!OB.active) return;
    if (!TASKS[OB.task].update()) return;
    vrui_haptic_pulse(SFXR_LEFT, 0.3f, 0.05f, 0);     // a little "done" on both hands
    vrui_haptic_pulse(SFXR_RIGHT, 0.3f, 0.05f, 0);
    if (++OB.task >= NTASKS) {
        OB.active = false;
        onboarding_apply(&OB.prefs);
        save_prefs();
        return;
    }
    OB.task_start = sfxr_time();
    if (OB.task == NTASKS - 1) infer();
}
