// sfxt.h - the test harness: drive an app's frame loop with scripted hands
// and check what happened. Plain C; the future .sfxt scenario files are a thin
// syntax over these same calls (docs/TESTING.md, "The test language").
//
// A test program is a scene plus named cases:
//
//   #include "sfxt.h"
//   static float knob; static VruiMech km;
//   static void scene(void) { km = vrui_rotary(1, KNOB_POSE, &spec, &knob); }
//
//   static void knob_orbit(void) {
//       sfxt_hand_to(SFXR_RIGHT, rim_point, 0.3f);    // move there over 0.3 s
//       sfxt_grip(SFXR_RIGHT, 1.0f);
//       sfxt_frames(2);
//       CHECK(km.held, "grabbed");
//       ...
//   }
//   static const SfxtCase CASES[] = {
//       { "knob/orbit-quarter-turn", knob_orbit, "vrui_mech_lateral_leak" },
//   };
//   int main(int argc, char **argv) { return sfxt_main(argc, argv, CASES, SFXT_COUNT(CASES), scene); }
//
// Each case runs in its own process with a fresh app (the runner,
// scripts/test.sh, does that). `fails_if` names the break switch that must
// make the case fail (sfxr_break.h); the runner proves it by running the case
// again with that switch on: passing both ways means the test is UNTRUSTED.
//
// Time is simulated: exactly 1/72 s per frame, and nothing happens between
// calls that don't advance frames. Hand and head poses are TRACKING-space
// meters, like a real runtime reports them: while the rig stays at the origin
// (it does unless the scene moves the player) they are world meters too. The
// head starts at 1.6 m looking down -Z.
//
// Hands: a scripted hand is a GRIP pose. Its aim (laser) pose is the same pose
// (pointing along the grip's -Z) and its poke point (the tip that presses
// buttons) is 1 cm ahead of that, exactly like sfxr derives them for
// controllers that don't report them.

#ifndef SFXT_H
#define SFXT_H

#include "sfxr.h"
#include "vrui.h"
#include "sfxr_break.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;        // "group/case-name"
    void (*run)(void);
    const char *fails_if;    // break switch that must make this case fail (NULL: unproven)
} SfxtCase;

#define SFXT_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Runs one case (argv[1]), or lists them (--list). The scene is called every
// frame between vrui_begin() and vrui_end(). Returns the process exit code.
int sfxt_main(int argc, char **argv, const SfxtCase *cases, int ncases, void (*scene)(void));

// --- time
void sfxt_frames(int n);             // run n frames
void sfxt_wait(float seconds);       // run frames for this long
uint64_t sfxt_frame(void);

// --- hands (grip pose; see above)
void     sfxt_hand_active(SfxrHandId h, bool active);   // hands start active, resting low at the sides
void     sfxt_hand_set(SfxrHandId h, SfxrPose grip);    // jump there now
SfxrPose sfxt_hand(SfxrHandId h);                       // current scripted grip pose (before noise)
void     sfxt_headset_worn(bool worn);                  // the player takes the headset off (false) / puts it on
void     sfxt_dashboard(bool open);                     // the SteamVR dashboard opens / closes (sfxr_attention)
// Move to a pose, easing in and out, over `seconds` (0: this frame).
void     sfxt_hand_to(SfxrHandId h, SfxrPose grip, float seconds);
// Put the poke point (tip) at `point`, pointing along `dir` (e.g. straight down).
SfxrPose sfxt_tip_pose(Vector3 point, Vector3 dir);
// General motion: pose(t) for t = 0..1 (already eased), sampled every frame.
void     sfxt_hand_path(SfxrHandId h, SfxrPose (*pose)(float t, void *user), void *user, float seconds);

// --- head (tracking space; the player walking or leaning)
void     sfxt_head_to(SfxrPose head, float seconds);    // 0 s: from the next frame
SfxrPose sfxt_head(void);

// --- bare hands (docs/INPUT.md, "Bare hands"). The grip pose above still
// places the hand; sfxt builds all 26 joints from it plus the finger curls.
typedef enum {
    SFXT_CONTROLLER = 0,    // holding a Steam Frame controller (the default)
    SFXT_BARE,              // bare hand, and the runtime has a hand-interaction profile
                            // (its own pinch -> trigger, grasp -> grip, aim and poke poses)
    SFXT_BARE_JOINTS_ONLY,  // bare hand, and the runtime reports only joints (sfxr builds the rest)
    SFXT_FRAME_SKELETON,    // holding Frame controllers, as SteamVR reports them: a skeleton from the touch
                            // sensors (thumb on the wrong control, index always curled) and a
                            // poke pose 12.5 cm under the grip
} SfxtHandKind;
void sfxt_hand_kind(SfxrHandId h, SfxtHandKind kind);
void sfxt_fingers(SfxrHandId h, float thumb, float index, float middle, float ring, float little); // curl 0..1
void sfxt_pinch(SfxrHandId h, float amount);          // thumb tip toward the index tip: 0 .. 1 touching
void sfxt_shape(SfxrHandId h, SfxrHandShape shape);   // curls (and pinch) that make this shape

// --- controls (take effect on the next frame)
void sfxt_trigger(SfxrHandId h, float v);
void sfxt_grip(SfxrHandId h, float v);
void sfxt_stick(SfxrHandId h, Vector2 v);
void sfxt_button(SfxrHandId h, SfxrControl c, bool down);
void sfxt_touch(SfxrHandId h, SfxrControl c, bool touching);   // finger resting on it, not pressing
                                                                 // (trigger/grip touch also follow their values)
// Ramp the trigger from its current value to `to` over `seconds`.
void sfxt_trigger_ramp(SfxrHandId h, float to, float seconds);

// --- noise (deterministic per case): hand position (m, uniform +-), wrist
// rotation (deg), trigger/grip (+-). Default: 1 mm, 0.2 deg, 0.01 -- real
// hands are never still. sfxt_noise(0, 0, 0) for exact geometry.
void sfxt_noise(float pos_m, float rot_deg, float analog);

// --- what the app did
int   sfxt_haptic_count(SfxrHandId h);  // haptic updates sent to this hand so far
float sfxt_haptic_max(SfxrHandId h);    // strongest amplitude since the last reset
void  sfxt_haptic_reset(SfxrHandId h);

// --- named targets: widgets by their registry name (vrui.h section 12),
// e.g. "table.sky" (where it's mounted) or "table.sky/handle" (its moving
// part), looked up from the last frame. Poses are world space; the harness
// converts to tracking space (sfxt_to_tracking) for the hands.
bool     sfxt_find(const char *target, SfxrPose *world);
float    sfxt_value(const char *widget);                       // NAN when not found
SfxrPose sfxt_to_tracking(SfxrPose world);
// Move the grip to a target (+ offset, world axes), following it if it moves.
void     sfxt_hand_to_target(SfxrHandId h, const char *target, Vector3 offset, float seconds);
void     sfxt_point_at(SfxrHandId h, const char *target, float seconds);   // aim the laser at it
void     sfxt_look_at(const char *target, float seconds);

// --- events (sfxr_event), for "did it grab / press / click?": how many
// events of `kind` for the widget labelled `label` (NULL: any) by `hand`
// (-1: either) since frame `since`.
int      sfxt_events(const char *kind, const char *label, int hand, uint64_t since);

// --- failure artifacts: at the first failed check the harness draws the app
// from the head and saves it (SFXT_SNAPSHOT, set by the runner). Give it
// your app's world drawing; vrui's queue is drawn either way.
void     sfxt_set_draw(void (*draw)(void));
void     sfxt_set_sky(Color (*sky)(void));        // the snapshot's background (default slate gray)

// --- scenario files (.sfxt): every *.sfxt in `dir` is a set of cases
// named <file>/<case>; `setup` runs once after sfxr and vrui start (your
// app's init), `scene` every frame, `draw` for failure snapshots. The
// language: docs/TESTING.md, "The test language".
int sfxt_scenario_main(int argc, char **argv, const char *dir, void (*scene)(void), void (*setup)(void), void (*draw)(void));
void sfxt__begin(const char *name, void (*scene)(void));   // (used by the scenario runner)
int  sfxt__finish(void);

// --- checks: record a failure and keep going (all failures get reported)
void sfxt_check(bool ok, const char *expr, const char *file, int line, const char *fmt, ...);
#define CHECK(cond, ...) sfxt_check((cond), #cond, __FILE__, __LINE__, __VA_ARGS__)
// |a - b| <= tol, with both numbers in the message
#define CHECK_NEAR(a, b, tol, what) \
    sfxt_check(fabsf((float)(a) - (float)(b)) <= (float)(tol), #a " ~= " #b, __FILE__, __LINE__, \
               "%s: got %.4f, want %.4f +- %.4f", what, (double)(a), (double)(b), (double)(tol))

#ifdef __cplusplus
}
#endif

#endif // SFXT_H
