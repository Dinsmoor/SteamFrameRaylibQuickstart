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
// calls that don't advance frames. Positions are world meters (the rig stays
// at the origin, the head at 1.6 m looking down -Z).
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
// Move to a pose, easing in and out, over `seconds` (0: this frame).
void     sfxt_hand_to(SfxrHandId h, SfxrPose grip, float seconds);
// Put the poke point (tip) at `point`, pointing along `dir` (e.g. straight down).
SfxrPose sfxt_tip_pose(Vector3 point, Vector3 dir);
// General motion: pose(t) for t = 0..1 (already eased), sampled every frame.
void     sfxt_hand_path(SfxrHandId h, SfxrPose (*pose)(float t, void *user), void *user, float seconds);

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
int  sfxt_haptic_count(SfxrHandId h);   // haptic pulses sent to this hand so far

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
