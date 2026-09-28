// sfxt.c - test harness (see sfxt.h): scripted hands in, the app's real frame
// loop (sfxr + vrui + your scene) in between, checks out.

#include "sfxt.h"
#include "sfxr_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S sfxr_state

extern void (*sfxr_script_fill)(void);
extern void (*sfxr_script_haptic)(int hand, float amplitude, float seconds);

typedef struct {
    bool     active;
    SfxrPose grip;
    float    trigger, squeeze;
    Vector2  stick;
    uint32_t buttons;       // RAW_BIT(SfxrControl)
    uint32_t touching;      // touched without pressing
    int      haptics;
    float    haptic_max;    // strongest amplitude since sfxt_haptic_reset
    Vector3  prev_pos;
    bool     have_prev;
    SfxtHandKind kind;      // controller, or a bare hand (two kinds of runtime)
    float    curl[5];       // bare hands: finger curls (SFXR_FINGER_*)
    float    pinch;         // bare hands: thumb tip toward the index tip
} Hand;

static struct {
    Hand hand[2];
    SfxrPose head;          // tracking space; starts at 1.6 m looking down -Z
    void (*scene)(void);
    const char *case_name;
    int failures;
    float n_pos, n_rot, n_analog;
    uint32_t rng;
    void (*draw)(void);     // draws the app, for the failure snapshot
    bool snapped;
} T;

// Events seen during the case (sfxr_event_listen), for `expect grab ...`.
#define MAX_EVENTS 2048
static struct { uint64_t frame; char kind[16]; char text[112]; } EV[MAX_EVENTS];
static int nev;

static void on_event(uint64_t frame, const char *kind, const char *text, void *user)
{
    (void)user;
    if (nev >= MAX_EVENTS) return;
    EV[nev].frame = frame;
    snprintf(EV[nev].kind, sizeof EV[0].kind, "%s", kind);
    snprintf(EV[nev].text, sizeof EV[0].text, "%s", text);
    nev++;
}

// --- deterministic noise -----------------------------------------------------

static float rnd(void)   // uniform -1..1
{
    T.rng ^= T.rng << 13;
    T.rng ^= T.rng >> 17;
    T.rng ^= T.rng << 5;
    return (float)(T.rng & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
}

static float noisy01(float v)
{
    if (T.n_analog <= 0) return v;
    return Clamp(v + rnd() * T.n_analog, 0.0f, 1.0f);
}

// --- feeding sfxr --------------------------------------------------------------

// A bare hand: joints always; the controller-like input only when the runtime
// has a hand-interaction profile (SFXT_BARE). Its values are the runtime's
// own idea of pinch and grasp, deliberately not sfxr's formulas.
static void fill_bare(int h, Hand *H, SfxrPose g, SfxrRawHand *o)
{
    SfxrHandJoints *j = &S.sig.joints[h];
    sfxr__hand_model(h, g, H->curl, H->pinch, j);
    if (H->kind == SFXT_BARE_JOINTS_ONLY) { H->have_prev = false; return; }
    o->active = true;
    o->source = SFXR_SOURCE_HAND;
    snprintf(o->profile, sizeof o->profile, "/interaction_profiles/ext/hand_interaction_ext");
    o->pose_valid = RAW_POSE_GRIP | RAW_POSE_AIM | RAW_POSE_POKE | RAW_POSE_PINCH | RAW_POSE_PALM;
    o->grip = g;
    o->aim = (SfxrPose){ j->joint[SFXR_JOINT_INDEX_PROXIMAL].position, g.orientation };
    o->poke = j->joint[SFXR_JOINT_INDEX_TIP];
    o->pinch = (SfxrPose){ Vector3Lerp(j->joint[SFXR_JOINT_INDEX_TIP].position, j->joint[SFXR_JOINT_THUMB_TIP].position, 0.5f),
                           g.orientation };
    o->palm = j->joint[SFXR_JOINT_PALM];
    float d = Vector3Distance(j->joint[SFXR_JOINT_INDEX_TIP].position, j->joint[SFXR_JOINT_THUMB_TIP].position);
    o->trigger = noisy01(Clamp((0.045f - d) / 0.03f, 0, 1));
    o->squeeze = noisy01(Clamp(((H->curl[2] + H->curl[3] + H->curl[4]) / 3.0f - 0.25f) / 0.5f, 0, 1));
    if (H->have_prev) {
        o->velocity = Vector3Scale(Vector3Subtract(g.position, H->prev_pos), 72.0f);
        o->has_velocity = true;
    }
    H->prev_pos = g.position;
    H->have_prev = true;
}

static void fill(void)
{
    S.head_stage = T.head;
    S.eye_stage[0] = S.eye_stage[1] = S.head_stage;
    for (int e = 0; e < 2; e++) {
        S.fov[e][0] = -0.785f; S.fov[e][1] = 0.785f; S.fov[e][2] = 0.785f; S.fov[e][3] = -0.785f;
    }
    for (int h = 0; h < 2; h++) {
        Hand *H = &T.hand[h];
        SfxrRawHand *o = &S.raw[h];
        memset(o, 0, sizeof *o);
        memset(&S.sig.joints[h], 0, sizeof S.sig.joints[h]);
        snprintf(o->profile, sizeof o->profile, "/interaction_profiles/sfxt/script");
        if (!H->active) { H->have_prev = false; continue; }
        SfxrPose g = H->grip;
        if (T.n_pos > 0) g.position = Vector3Add(g.position, (Vector3){ rnd() * T.n_pos, rnd() * T.n_pos, rnd() * T.n_pos });
        if (T.n_rot > 0) {
            Vector3 ax = Vector3Normalize((Vector3){ rnd(), rnd(), rnd() + 0.001f });
            g.orientation = QuaternionMultiply(QuaternionFromAxisAngle(ax, rnd() * T.n_rot * DEG2RAD), g.orientation);
        }
        if (H->kind != SFXT_CONTROLLER && H->kind != SFXT_FRAME_SKELETON) { fill_bare(h, H, g, o); continue; }
        o->active = true;
        o->source = SFXR_SOURCE_CONTROLLER;
        o->pose_valid = RAW_POSE_GRIP | RAW_POSE_AIM;
        o->grip = g;
        o->aim = g;
        if (H->have_prev) {
            o->velocity = Vector3Scale(Vector3Subtract(g.position, H->prev_pos), 72.0f);
            o->has_velocity = true;
        }
        H->prev_pos = g.position;
        H->have_prev = true;
        o->trigger = noisy01(H->trigger);
        o->squeeze = noisy01(H->squeeze);
        o->stick = H->stick;
        o->click = H->buttons;
        if (H->trigger >= 0.995f) o->click |= RAW_BIT(SFXR_CTL_TRIGGER);   // the Frame clicks only at the bottom
        if (H->squeeze >= 0.995f) o->click |= RAW_BIT(SFXR_CTL_SQUEEZE);
        o->touch = o->click | H->touching;
        if (H->trigger > 0) o->touch |= RAW_BIT(SFXR_CTL_TRIGGER);
        if (H->squeeze > 0) o->touch |= RAW_BIT(SFXR_CTL_SQUEEZE);
        if (H->kind == SFXT_FRAME_SKELETON) {   // SteamVR's skeleton from the controller, its thumb mirrored
            snprintf(o->profile, sizeof o->profile, "/interaction_profiles/valve/frame_controller_valve");
            SfxrHandJoints *j = &S.sig.joints[h];
            sfxr__hand_model(h, g, H->curl, 0, j);
            j->source = SFXR_SOURCE_CONTROLLER;
            sfxr__unmirror_thumb(g, j);
        }
    }
}

static void on_haptic(int hand, float amplitude, float seconds)
{
    (void)seconds;
    Hand *H = &T.hand[hand ? 1 : 0];
    H->haptics++;
    if (amplitude > H->haptic_max) H->haptic_max = amplitude;
}

static void run_frame(void)
{
    if (!sfxr_frame_begin()) return;
    vrui_begin();
    if (T.scene) T.scene();
    vrui_end();
    if (sfxr_draw_begin(BLACK)) { vrui_draw(); sfxr_draw_end(); }
    sfxr_frame_end();
}

// --- time ------------------------------------------------------------------------

void sfxt_frames(int n) { for (int i = 0; i < n; i++) run_frame(); }
void sfxt_wait(float seconds) { sfxt_frames((int)lroundf(seconds * 72.0f)); }
uint64_t sfxt_frame(void) { return sfxr_frame_index(); }

// --- hands -------------------------------------------------------------------------

void sfxt_hand_active(SfxrHandId h, bool active) { T.hand[h == SFXR_RIGHT].active = active; }
void sfxt_hand_set(SfxrHandId h, SfxrPose grip) { T.hand[h == SFXR_RIGHT].grip = grip; }
SfxrPose sfxt_hand(SfxrHandId h) { return T.hand[h == SFXR_RIGHT].grip; }

static float ease(float t) { t = Clamp(t, 0, 1); return t * t * (3.0f - 2.0f * t); }

void sfxt_hand_to(SfxrHandId h, SfxrPose target, float seconds)
{
    Hand *H = &T.hand[h == SFXR_RIGHT];
    SfxrPose from = H->grip;
    int n = (int)lroundf(seconds * 72.0f);
    for (int i = 1; i <= n; i++) {
        H->grip = sfxr_pose_lerp(from, target, ease((float)i / (float)n));
        run_frame();
    }
    H->grip = target;
}

void sfxt_head_to(SfxrPose target, float seconds)
{
    SfxrPose from = T.head;
    int n = (int)lroundf(seconds * 72.0f);
    for (int i = 1; i <= n; i++) {
        T.head = sfxr_pose_lerp(from, target, ease((float)i / (float)n));
        run_frame();
    }
    T.head = target;
}

SfxrPose sfxt_head(void) { return T.head; }

void sfxt_hand_path(SfxrHandId h, SfxrPose (*pose)(float t, void *user), void *user, float seconds)
{
    Hand *H = &T.hand[h == SFXR_RIGHT];
    int n = (int)lroundf(seconds * 72.0f);
    if (n < 1) n = 1;
    for (int i = 1; i <= n; i++) {
        H->grip = pose(ease((float)i / (float)n), user);
        run_frame();
    }
}

SfxrPose sfxt_tip_pose(Vector3 point, Vector3 dir)
{
    dir = Vector3Normalize(dir);
    // orientation whose -Z is `dir`
    Vector3 z = Vector3Negate(dir);
    Vector3 up = fabsf(z.y) > 0.95f ? (Vector3){ 0, 0, -1 } : (Vector3){ 0, 1, 0 };
    Vector3 x = Vector3Normalize(Vector3CrossProduct(up, z));
    Vector3 y = Vector3CrossProduct(z, x);
    Matrix m = { x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, 0, 0, 0, 1 };
    SfxrPose p;
    p.orientation = QuaternionFromMatrix(m);
    p.position = Vector3Subtract(point, Vector3Scale(dir, 0.01f));   // the tip is 1 cm ahead of the aim point
    return p;
}

// --- bare hands ---------------------------------------------------------------------

void sfxt_hand_kind(SfxrHandId h, SfxtHandKind kind) { T.hand[h == SFXR_RIGHT].kind = kind; }

void sfxt_fingers(SfxrHandId h, float thumb, float index, float middle, float ring, float little)
{
    Hand *H = &T.hand[h == SFXR_RIGHT];
    H->curl[0] = thumb; H->curl[1] = index; H->curl[2] = middle; H->curl[3] = ring; H->curl[4] = little;
}

void sfxt_pinch(SfxrHandId h, float amount) { T.hand[h == SFXR_RIGHT].pinch = amount; }

void sfxt_shape(SfxrHandId h, SfxrHandShape shape)
{
    sfxt_pinch(h, 0);
    switch (shape) {
    case SFXR_SHAPE_OPEN:      sfxt_fingers(h, 0, 0, 0, 0, 0); break;
    case SFXR_SHAPE_POINT:     sfxt_fingers(h, 0.7f, 0, 1, 1, 1); break;
    case SFXR_SHAPE_FIST:      sfxt_fingers(h, 0.7f, 1, 1, 1, 1); break;
    case SFXR_SHAPE_THUMBS_UP: sfxt_fingers(h, 0, 1, 1, 1, 1); break;
    case SFXR_SHAPE_PINCH:     sfxt_fingers(h, 0.3f, 0.3f, 0, 0, 0); sfxt_pinch(h, 1); break;
    default:                   sfxt_fingers(h, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f); break;
    }
}

// --- controls ---------------------------------------------------------------------

void sfxt_trigger(SfxrHandId h, float v) { T.hand[h == SFXR_RIGHT].trigger = v; }
void sfxt_grip(SfxrHandId h, float v) { T.hand[h == SFXR_RIGHT].squeeze = v; }
void sfxt_stick(SfxrHandId h, Vector2 v) { T.hand[h == SFXR_RIGHT].stick = v; }

void sfxt_button(SfxrHandId h, SfxrControl c, bool down)
{
    uint32_t *b = &T.hand[h == SFXR_RIGHT].buttons;
    if (down) *b |= RAW_BIT(c); else *b &= ~RAW_BIT(c);
}

void sfxt_touch(SfxrHandId h, SfxrControl c, bool touching)
{
    uint32_t *b = &T.hand[h == SFXR_RIGHT].touching;
    if (touching) *b |= RAW_BIT(c); else *b &= ~RAW_BIT(c);
}

void sfxt_trigger_ramp(SfxrHandId h, float to, float seconds)
{
    Hand *H = &T.hand[h == SFXR_RIGHT];
    float from = H->trigger;
    int n = (int)lroundf(seconds * 72.0f);
    for (int i = 1; i <= n; i++) {
        H->trigger = from + (to - from) * (float)i / (float)n;
        run_frame();
    }
    H->trigger = to;
}

void sfxt_noise(float pos_m, float rot_deg, float analog)
{
    T.n_pos = pos_m;
    T.n_rot = rot_deg;
    T.n_analog = analog;
}

int sfxt_haptic_count(SfxrHandId h) { return T.hand[h == SFXR_RIGHT].haptics; }
float sfxt_haptic_max(SfxrHandId h) { return T.hand[h == SFXR_RIGHT].haptic_max; }
void sfxt_haptic_reset(SfxrHandId h) { T.hand[h == SFXR_RIGHT].haptic_max = 0; }

// --- checks --------------------------------------------------------------------------

// --- named targets, events, snapshots -----------------------------------------------

static SfxrPose rig_pose(void)
{
    return (SfxrPose){ sfxr_rig_position(), QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, sfxr_rig_yaw()) };
}

SfxrPose sfxt_to_tracking(SfxrPose world) { return sfxr_pose_relative(rig_pose(), world); }

bool sfxt_find(const char *target, SfxrPose *world)
{
    SfxrPose p;
    if (!vrui_find_widget(target, &p)) return false;
    if (world) *world = p;
    return true;
}

float sfxt_value(const char *widget)
{
    const VruiWidgetInfo *w = vrui_find_widget(widget, NULL);
    return w && w->has_value ? w->value : NAN;
}

void sfxt_hand_to_target(SfxrHandId h, const char *target, Vector3 offset, float seconds)
{
    Hand *H = &T.hand[h == SFXR_RIGHT];
    SfxrPose from = H->grip;
    int n = (int)lroundf(seconds * 72.0f);
    if (n < 1) n = 1;
    for (int i = 1; i <= n; i++) {
        SfxrPose w;
        if (!sfxt_find(target, &w)) { printf("  no widget '%s' (is it on screen? named?)\n", target); T.failures++; return; }
        // looked up every frame: a target that moves is followed
        Vector3 goal = sfxt_to_tracking((SfxrPose){ Vector3Add(w.position, offset), w.orientation }).position;
        H->grip.position = Vector3Lerp(from.position, goal, ease((float)i / (float)n));
        run_frame();
    }
}

void sfxt_point_at(SfxrHandId h, const char *target, float seconds)
{
    Hand *H = &T.hand[h == SFXR_RIGHT];
    SfxrPose from = H->grip, w;
    if (!sfxt_find(target, &w)) { printf("  no widget '%s'\n", target); T.failures++; return; }
    Vector3 at = sfxt_to_tracking(w).position;
    SfxrPose aim = sfxt_tip_pose(Vector3Add(from.position, Vector3Scale(Vector3Normalize(Vector3Subtract(at, from.position)), 0.01f)),
                                 Vector3Subtract(at, from.position));
    aim.position = from.position;
    sfxt_hand_to(h, aim, seconds);
}

void sfxt_look_at(const char *target, float seconds)
{
    SfxrPose w;
    if (!sfxt_find(target, &w)) { printf("  no widget '%s'\n", target); T.failures++; return; }
    Vector3 at = sfxt_to_tracking(w).position;
    SfxrPose look = sfxt_tip_pose(at, Vector3Subtract(at, T.head.position));
    look.position = T.head.position;
    sfxt_head_to(look, seconds);
}

int sfxt_events(const char *kind, const char *label, int hand, uint64_t since_frame)
{
    int n = 0;
    size_t ll = label ? strlen(label) : 0;
    for (int i = 0; i < nev; i++) {
        if (EV[i].frame < since_frame || strcmp(EV[i].kind, kind)) continue;
        const char *t = EV[i].text;
        if (label && (strncmp(t, label, ll) || (t[ll] && t[ll] != ' '))) continue;
        if (hand >= 0) {   // "SKY R hand", "SKY L laser+grip", "SKY R"
            const char *h = t + ll;
            while (*h == ' ') h++;
            if (*h != (hand ? 'R' : 'L')) continue;
        }
        n++;
    }
    return n;
}

void sfxt_set_draw(void (*draw)(void)) { T.draw = draw; }

// The failing moment as a picture: the app drawn from the head, 960 x 540.
static void snapshot(void)
{
    const char *path = getenv("SFXT_SNAPSHOT");
    if (!path || !*path || T.snapped) return;
    T.snapped = true;
    RenderTexture2D rt = LoadRenderTexture(960, 540);
    if (!rt.id) return;
    BeginTextureMode(rt);
    ClearBackground((Color){ 60, 70, 90, 255 });
    Camera3D cam = sfxr_head_camera();
    cam.fovy = 75.0f;
    BeginMode3D(cam);
    if (T.draw) T.draw();
    else sfxr_draw_floor_grid(10, (Color){ 255, 255, 255, 60 }, (Color){ 255, 255, 255, 25 });
    vrui_draw();
    EndMode3D();
    EndTextureMode();
    Image im = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&im);
    ExportImage(im, path);
    UnloadImage(im);
    UnloadRenderTexture(rt);
}

void sfxt_check(bool ok, const char *expr, const char *file, int line, const char *fmt, ...)
{
    if (ok) return;
    snapshot();
    T.failures++;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("  %s:%d: frame %llu: %s  [%s]\n", file, line, (unsigned long long)sfxr_frame_index(), msg, expr);
}

// --- runner entry --------------------------------------------------------------------

// Start a case: a fresh sfxr + vrui in this process (the runner starts one
// process per case), hands at the sides, the head at 1.6 m looking ahead.
void sfxt__begin(const char *name, void (*scene)(void))
{
    if (!getenv("SFXT_VERBOSE")) SetTraceLogLevel(LOG_WARNING);
    memset(&T, 0, sizeof T);
    nev = 0;
    T.scene = scene;
    T.case_name = name;
    T.rng = 2166136261u;
    for (const char *p = name; *p; p++) T.rng = (T.rng ^ (uint8_t)*p) * 16777619u;   // seed per case
    if (!T.rng) T.rng = 1;
    sfxt_noise(0.001f, 0.2f, 0.01f);

    sfxr_script_fill = fill;
    sfxr_script_haptic = on_haptic;
    sfxr_event_listen(on_event, NULL);
    SfxrConfig cfg = sfxr_default_config();
    cfg.app_name = name;
    cfg.backend = SFXR_BACKEND_SCRIPT;
    cfg.mirror_window = false;
    cfg.mirror_width = 64;
    cfg.mirror_height = 64;
    cfg.msaa_samples = 1;
    if (!sfxr_init(&cfg)) { printf("FAIL %s\n  sfxr_init failed (no X display? run under Xvfb)\n", name); exit(1); }
    vrui_init();

    // Hands start active, low at the sides, pointing ahead and down: out of
    // the way of anything on a table in front of the player.
    Quaternion ahead_down = QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, -0.6f);
    T.hand[0] = (Hand){ .active = true, .grip = { { -0.3f, 0.7f, 0.1f }, ahead_down } };
    T.hand[1] = (Hand){ .active = true, .grip = { { 0.3f, 0.7f, 0.1f }, ahead_down } };
    T.head = (SfxrPose){ { 0, 1.6f, 0 }, QuaternionIdentity() };
}

int sfxt__finish(void)
{
    const char *brk = getenv("SFXR_BREAK");
    printf("%s %s", T.failures ? "FAIL" : "PASS", T.case_name);
    if (brk && *brk) printf("  [break: %s]", brk);
    printf("\n");
    vrui_shutdown();
    sfxr_shutdown();
    return T.failures ? 1 : 0;
}

int sfxt_main(int argc, char **argv, const SfxtCase *cases, int ncases, void (*scene)(void))
{
    if (argc < 2 || !strcmp(argv[1], "--list")) {
        for (int i = 0; i < ncases; i++) printf("%s %s\n", cases[i].name, cases[i].fails_if ? cases[i].fails_if : "-");
        return argc < 2 ? 2 : 0;
    }
    const SfxtCase *c = NULL;
    for (int i = 0; i < ncases; i++) if (!strcmp(cases[i].name, argv[1])) c = &cases[i];
    if (!c) { fprintf(stderr, "no case '%s' (--list shows them)\n", argv[1]); return 2; }
    sfxt__begin(c->name, scene);
    sfxt_frames(3);
    c->run();
    return sfxt__finish();
}
