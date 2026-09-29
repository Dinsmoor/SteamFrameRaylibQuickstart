// sfxr.h - Steam Frame XR runtime layer for raylib + C.
//
// Owns everything between "raylib can draw" and "pixels land in the headset":
// the OpenXR instance/session/swapchains, controller input (actions), the
// player rig (locomotion offset), and a desktop simulator used when no
// OpenXR runtime is available so you can iterate without wearing the headset.
//
// The whole frame is drawn ONCE per frame: sfxr puts rlgl into raylib's
// built-in stereo mode, so every raylib draw call (DrawCube, DrawModel, ...)
// is emitted for both eyes into a side-by-side swapchain image.
//
//   SfxrConfig cfg = sfxr_default_config();
//   cfg.app_name = "hello";
//   if (!sfxr_init(&cfg)) return 1;
//   while (sfxr_frame_begin()) {
//       /* update: read sfxr_hand(), move things, run vrui logic */
//       if (sfxr_draw_begin(BLACK)) {
//           DrawCube(...);                      // world space, meters, Y up
//           sfxr_draw_end();
//       }
//       /* optional: 2D debug text drawn onto the desktop mirror window */
//       sfxr_frame_end();
//   }
//   sfxr_shutdown();
//
// Coordinate system: right-handed, +Y up, -Z forward, 1 unit = 1 meter,
// origin on the floor. Identical to OpenXR and raylib's 3D conventions.
//
// Backends (picked at runtime, see sfxr_backend()):
//   SFXR_BACKEND_XR_GL   OpenXR with XR_KHR_opengl_enable (GLX); raylib draws
//                        straight into the runtime's swapchain textures.
//   SFXR_BACKEND_XR_VK   OpenXR with XR_KHR_vulkan_enable2; raylib draws into a
//                        GL texture that aliases Vulkan memory, which is then
//                        blitted into the runtime's Vulkan swapchain. Used when
//                        the runtime has no GL binding.
//   SFXR_BACKEND_SIM     No headset: mono view in the window, mouse/keyboard
//                        drive the head and controllers (press F1 for keys).
//   SFXR_BACKEND_REPLAY  Plays back a recorded session (see "Testing" below).
//   SFXR_BACKEND_SCRIPT  Inputs come from a test (sfxt/include/sfxt.h); nothing drawn.
//
// Environment overrides (handy on-device):
//   SFXR_BACKEND=gl|vk|sim   force a backend
//   SFXR_MIRROR=0|1          hide/show the desktop mirror window
//   SFXR_LOG=1               verbose OpenXR logging
//   SFXR_FALLBACK_BINDINGS=1 on a Frame, also offer Touch/Index/simple controller
//                            bindings (off by default: Frame-only, no emulation notice)
//
// Testing (see scripts/regress.sh and CLAUDE.md):
//   SFXR_RECORD=f.sfxrec     record every frame's raw inputs (headset or simulator)
//   SFXR_REPLAY=f.sfxrec     replay them deterministically, offscreen
//   SFXR_SHOT=out.png [SFXR_SHOT_FRAME=N]   save a screenshot at frame N, then quit
//   SFXR_SHOT=out-%05d.png SFXR_SHOT_FRAMES="120,240,360"   several frames, then quit
// For replays to be deterministic, drive time-based logic ONLY from sfxr_dt(),
// sfxr_time() and sfxr_frame_index() -- never GetTime()/GetFrameTime()/GetFPS().

#ifndef SFXR_H
#define SFXR_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "raymath.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

typedef enum {
    SFXR_BACKEND_AUTO = 0,   // config only: try XR_GL, then XR_VK, then SIM
    SFXR_BACKEND_XR_GL,
    SFXR_BACKEND_XR_VK,
    SFXR_BACKEND_SIM,
    SFXR_BACKEND_REPLAY,     // plays back an input recording (SFXR_REPLAY=file)
    SFXR_BACKEND_SCRIPT,     // inputs come from a test (sfxt.h); nothing is rendered
} SfxrBackend;

typedef struct {
    const char *app_name;       // shown to the runtime and on the window title
    SfxrBackend backend;        // default AUTO
    bool  mirror_window;        // show the left eye in a desktop window (default true)
    int   mirror_width;         // mirror / simulator window size
    int   mirror_height;
    float resolution_scale;     // multiplies the runtime's recommended eye size (1.0)
    int   msaa_samples;         // 1 = off, 4 = default (cheap on tile GPUs, big win in VR)
    float near_clip;            // meters (0.05)
    float far_clip;             // meters (500)
    float preferred_refresh_hz; // 0 = runtime default; applied if XR_FB_display_refresh_rate exists
    bool  allow_sim_fallback;   // AUTO falls back to SIM when no runtime (default true)
} SfxrConfig;

SfxrConfig sfxr_default_config(void);

bool        sfxr_init(const SfxrConfig *cfg);
void        sfxr_shutdown(void);
SfxrBackend sfxr_backend(void);
const char *sfxr_backend_name(void);
const char *sfxr_runtime_name(void);    // e.g. "SteamVR/OpenXR", "Monado(XRT)...", "sfxr simulator"
const char *sfxr_system_name(void);     // HMD name reported by the runtime

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

// Polls runtime + window events, waits for the runtime's frame pacing, and
// updates head/hand poses. Returns false when the app should exit (runtime
// asked us to quit, or the mirror window was closed).
bool  sfxr_frame_begin(void);

// Seconds since last frame (predicted display period while in XR).
float sfxr_dt(void);
// Seconds since start, the sum of sfxr_dt(); deterministic under replay.
double sfxr_time(void);

// True if the runtime wants pixels this frame (false while the headset is off
// your head / the dashboard is up). Updates should still run when false.
bool  sfxr_should_render(void);

// Bind the stereo target and set up both eye cameras. Returns false if this
// frame should not be rendered -- then skip drawing AND sfxr_draw_end().
bool  sfxr_draw_begin(Color clear);
void  sfxr_draw_end(void);

// Submit to the runtime and present the mirror window. Anything drawn with
// raylib 2D calls between sfxr_draw_end() and here lands on the mirror window
// only (debug HUD); the headset never sees it.
void  sfxr_frame_end(void);

// Frame counter and the per-eye render size actually in use.
uint64_t sfxr_frame_index(void);
int   sfxr_eye_width(void);
float sfxr_frame_cpu_ms(void);      // the app's CPU time last frame: frame_begin returning .. frame_end (0 in replays and tests)
int   sfxr_eye_height(void);

// ---------------------------------------------------------------------------
// Poses & math helpers
// ---------------------------------------------------------------------------

typedef struct {
    Vector3    position;
    Quaternion orientation;
} SfxrPose;

SfxrPose sfxr_pose_identity(void);
Matrix   sfxr_pose_to_matrix(SfxrPose p);                 // model matrix (for DrawMesh / rlMultMatrixf)
Vector3  sfxr_pose_forward(SfxrPose p);                   // -Z of the pose
Vector3  sfxr_pose_up(SfxrPose p);                        // +Y
Vector3  sfxr_pose_right(SfxrPose p);                     // +X
Vector3  sfxr_pose_apply(SfxrPose p, Vector3 local);      // local point -> world
Vector3  sfxr_pose_apply_inv(SfxrPose p, Vector3 world);  // world point -> local
SfxrPose sfxr_pose_mul(SfxrPose parent, SfxrPose child);  // child expressed in parent -> world
SfxrPose sfxr_pose_inverse(SfxrPose p);
// Attaching one thing to another (docs/ATTACHING.md). A child keeps a LOCAL
// pose relative to its parent, and its world pose is worked out again every
// frame, so it goes wherever the parent goes:
//     child_world = sfxr_pose_mul(parent_world, local);
// To attach something WHERE IT IS NOW (picking it up, setting it down on a
// moving platform), take its pose relative to the new parent at that moment:
//     local = sfxr_pose_relative(parent_world, child_world);
// To detach, stop updating it: its last world pose is where it stays.
// Parents can be anything with a pose: a hand's grip, the head, the body
// (vrui_body), a turntable, another child (chains work the same way).
SfxrPose sfxr_pose_relative(SfxrPose parent, SfxrPose child_world);   // = inverse(parent) * child
SfxrPose sfxr_pose_lerp(SfxrPose a, SfxrPose b, float t);
// A yaw-only pose facing `target` from `from` (handy for spawning UI panels).
SfxrPose sfxr_pose_look_at_yaw(Vector3 from, Vector3 target);

// Push/pop a pose onto rlgl's matrix stack so plain raylib draw calls happen
// in that pose's local space (e.g. draw a controller model in grip space).
void sfxr_push_pose(SfxrPose p);
void sfxr_pop_pose(void);

// ---------------------------------------------------------------------------
// Head, rig & locomotion
// ---------------------------------------------------------------------------
//
// Tracking space ("stage") is where the runtime reports poses: origin on the
// physical floor. The RIG maps stage -> world: a position + yaw. All poses
// sfxr returns are already in WORLD space. Move the player by moving the rig.

SfxrPose sfxr_head(void);                   // world-space head (between the eyes)
Camera3D sfxr_head_camera(void);            // raylib camera at the head (for DrawBillboard etc.)
SfxrPose sfxr_eye(int eye);                 // 0 = left, 1 = right
Vector3  sfxr_rig_position(void);
float    sfxr_rig_yaw(void);                // radians
void     sfxr_rig_set(Vector3 position, float yaw);
void     sfxr_rig_move(Vector3 delta_world);
void     sfxr_rig_turn(float yaw_delta);    // rotates around the head, not the rig origin
void     sfxr_rig_teleport(Vector3 floor_target); // puts the HEAD's floor point at target
Vector3  sfxr_head_floor_point(void);       // head position projected to rig floor height

// Could a sphere (world space) be seen this frame by either eye? The test
// uses each eye's real field of view and the far clip, so it's the frustum
// the frame is drawn with. For culling: skip drawing what this says no to.
bool     sfxr_in_view(Vector3 center, float radius);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

typedef enum { SFXR_LEFT = 0, SFXR_RIGHT = 1, SFXR_HAND_COUNT = 2 } SfxrHandId;

// How far a trigger or grip must be pulled to count as "down". Each level has
// its own press/release points (hysteresis), so a finger resting near a
// threshold doesn't flicker. Measured on the Steam Frame: the trigger rests at
// exactly 0, passes half travel ~15 ms into a normal pull, and its hardware
// "click" only fires fully bottomed out (40-100 ms later, and sometimes never),
// so FIRM, not the click, is the default.
typedef enum {
    SFXR_PULL_SOFT = 0,  // a light touch, ~1/4 travel (press 0.25, release 0.15)
    SFXR_PULL_FIRM,      // about half travel (0.55 / 0.35) -- trigger_btn / squeeze_btn
    SFXR_PULL_FULL,      // bottomed out (0.97 / 0.85, or the hardware click if it comes first)
    SFXR_PULL_COUNT
} SfxrPull;

// Change a level's thresholds (0..1, release < press), e.g. for players who
// can't pull hard. Applies to both hands, triggers and grips.
void sfxr_set_pull_threshold(SfxrPull level, float press, float release);

typedef struct {
    bool down;      // held this frame
    bool pressed;   // went down this frame
    bool released;  // went up this frame
    bool touched;   // finger resting on it (capacitive; every Frame control has this)
} SfxrButton;

// Every physical control on a Steam Frame controller pair, as reported by the
// runtime (checked with `tools/xr_probe --paths`). Each has a click and a
// capacitive touch. The right controller has A/B/X/Y + menu; the left has a
// 4-way D-pad + view. Both have trigger, grip, stick and bumper.
// (The Steam/system button also exists but belongs to SteamVR's dashboard.)
typedef enum {
    SFXR_CTL_TRIGGER = 0,   // both: its "click" only fires fully bottomed out -- see SfxrPull
    SFXR_CTL_SQUEEZE,       // both: the grip
    SFXR_CTL_STICK,         // both: stick click / thumb on stick
    SFXR_CTL_BUMPER,        // both: shoulder button above the trigger
    SFXR_CTL_A, SFXR_CTL_B, SFXR_CTL_X, SFXR_CTL_Y,          // right hand
    SFXR_CTL_MENU,          // right hand
    SFXR_CTL_VIEW,          // left hand
    SFXR_CTL_DPAD_UP, SFXR_CTL_DPAD_DOWN, SFXR_CTL_DPAD_LEFT, SFXR_CTL_DPAD_RIGHT, // left hand
    SFXR_CTL_COUNT
} SfxrControl;

const char *sfxr_control_name(SfxrControl c);           // "A", "D-pad up", ...
bool        sfxr_control_on_hand(SfxrControl c, SfxrHandId hand); // does this controller have it?

// What is driving a hand right now.
typedef enum {
    SFXR_SOURCE_NONE = 0,
    SFXR_SOURCE_CONTROLLER,  // a Steam Frame controller (or another runtime's controller)
    SFXR_SOURCE_HAND,        // bare hand tracking: pinch = trigger, grasp = grip, poses as below
} SfxrInputSource;

// Hand shape, read from the Frame controller's touch sensors (index on the
// trigger, fingers on the grip, thumb on any thumb control) -- or from the
// hand joints when the runtime has them (bare hands, or inferred while holding).
typedef enum {
    SFXR_SHAPE_RELAXED = 0,  // none of the below
    SFXR_SHAPE_OPEN,         // fingers off everything: an open hand (push things with the palm)
    SFXR_SHAPE_POINT,        // index extended, other fingers closed (poke buttons)
    SFXR_SHAPE_FIST,         // everything closed (grab, punch)
    SFXR_SHAPE_THUMBS_UP,    // fist with the thumb lifted off
    SFXR_SHAPE_PINCH,        // index and thumb together, others open
    SFXR_SHAPE_COUNT
} SfxrHandShape;
const char *sfxr_hand_shape_name(SfxrHandShape s);

enum { SFXR_FINGER_THUMB = 0, SFXR_FINGER_INDEX, SFXR_FINGER_MIDDLE, SFXR_FINGER_RING, SFXR_FINGER_LITTLE };

typedef struct {
    bool       active;      // tracked & bound this frame
    SfxrInputSource source;
    SfxrHandShape shape;    // debounced (a shape must hold for 3 frames)
    float      curl[5];     // per finger (SFXR_FINGER_*): 0 straight .. 1 fully curled
    bool       curl_from_joints; // true: measured from hand joints; false: estimated from touch sensors
    SfxrPose   grip;        // palm/handle pose: attach held objects here
    SfxrPose   aim;         // pointing pose: origin at the tip, -Z points forward
    SfxrPose   poke;        // fingertip point for pressing things (index tip / controller tip)
    SfxrPose   pinch;       // where thumb and index meet
    SfxrPose   palm;        // palm center, -Y out of the palm
    Vector3    velocity;    // world-space linear velocity of the grip (m/s)
    Vector3    angular_velocity; // world-space, rad/s

    float      trigger;     // 0..1 (bare hands: pinch strength)
    float      squeeze;     // 0..1 grip (bare hands: grasp strength)
    SfxrButton trigger_btn; // trigger at the FIRM level (same as trigger_at[SFXR_PULL_FIRM])
    SfxrButton squeeze_btn; // grip at the FIRM level
    SfxrButton trigger_at[SFXR_PULL_COUNT]; // trigger at each pull level (see SfxrPull)
    SfxrButton squeeze_at[SFXR_PULL_COUNT];

    Vector2    stick;       // thumbstick, -1..1, +Y = forward

    // Every control, raw: down = its hardware click, touched = finger on it.
    // For trigger/grip prefer the pull levels above; button[] holds the raw
    // click (bottomed out) and touch.
    SfxrButton button[SFXR_CTL_COUNT];

    // Named copies of button[] for readable code:
    SfxrButton stick_btn, bumper;
    SfxrButton a, b, x, y, menu;                          // right hand
    SfxrButton view, dpad_up, dpad_down, dpad_left, dpad_right; // left hand
    // "The two buttons under this thumb": right A / B, left D-pad down / up.
    SfxrButton primary, secondary;
} SfxrHand;

const SfxrHand *sfxr_hand(SfxrHandId hand);
const char     *sfxr_interaction_profile(SfxrHandId hand); // e.g. "/interaction_profiles/valve/frame_controller_valve"

// Vibrate a controller. amplitude 0..1, duration seconds, frequency 0 = runtime default.
void sfxr_haptic(SfxrHandId hand, float amplitude, float seconds, float frequency_hz);

// Eye gaze (XR_EXT_eye_gaze_interaction). Returns false when unavailable or
// not tracked this frame. In the simulator, gaze = head forward.
bool sfxr_gaze(SfxrPose *out_world);

// ---------------------------------------------------------------------------
// Hand joints (XR_EXT_hand_tracking): 26 joints per hand, OpenXR order
// ---------------------------------------------------------------------------
//
// The Frame reports joints both for bare hands (its cameras) and while holding
// the controllers (inferred from the controller's touch sensors) -- `source`
// says which. World space, like every other pose.

#define SFXR_JOINT_COUNT 26
typedef enum {
    SFXR_JOINT_PALM = 0, SFXR_JOINT_WRIST,
    SFXR_JOINT_THUMB_METACARPAL, SFXR_JOINT_THUMB_PROXIMAL, SFXR_JOINT_THUMB_DISTAL, SFXR_JOINT_THUMB_TIP,
    SFXR_JOINT_INDEX_METACARPAL, SFXR_JOINT_INDEX_PROXIMAL, SFXR_JOINT_INDEX_INTERMEDIATE, SFXR_JOINT_INDEX_DISTAL, SFXR_JOINT_INDEX_TIP,
    SFXR_JOINT_MIDDLE_METACARPAL, SFXR_JOINT_MIDDLE_PROXIMAL, SFXR_JOINT_MIDDLE_INTERMEDIATE, SFXR_JOINT_MIDDLE_DISTAL, SFXR_JOINT_MIDDLE_TIP,
    SFXR_JOINT_RING_METACARPAL, SFXR_JOINT_RING_PROXIMAL, SFXR_JOINT_RING_INTERMEDIATE, SFXR_JOINT_RING_DISTAL, SFXR_JOINT_RING_TIP,
    SFXR_JOINT_LITTLE_METACARPAL, SFXR_JOINT_LITTLE_PROXIMAL, SFXR_JOINT_LITTLE_INTERMEDIATE, SFXR_JOINT_LITTLE_DISTAL, SFXR_JOINT_LITTLE_TIP,
} SfxrJoint;

typedef struct {
    bool     valid;                        // joints located this frame
    SfxrInputSource source;                // HAND: seen by the cameras; CONTROLLER: inferred while holding one
    SfxrPose joint[SFXR_JOINT_COUNT];      // world space
    float    radius[SFXR_JOINT_COUNT];     // m
} SfxrHandJoints;

bool sfxr_hand_joints_supported(void);
const SfxrHandJoints *sfxr_hand_joints(SfxrHandId hand);   // never NULL; check ->valid

// ---------------------------------------------------------------------------
// Bare hands: gestures read from the joints (docs/INPUT.md, "Bare hands")
// ---------------------------------------------------------------------------
//
// With bare hands, sfxr_hand() already works like a controller: pinch is the
// trigger, closing the hand is the grip, the index tip pokes, and the aim pose
// points. That comes from the runtime's hand-interaction profile -- or, when a
// runtime only reports joints, from these gestures (SFXR_HANDS=joints forces
// that, to compare the two). The gestures below are measured from the joints
// every frame, with or without a controller in hand, for apps that want more
// than "trigger and grip": a second pinch, the palm facing up for a wrist
// menu, a steady ray.
//
// Every "button" here has hysteresis (it closes at one distance and opens at a
// wider one), so a hand held right at the edge doesn't flicker.

typedef struct {
    bool       valid;           // joints tracked this frame (everything below is zero otherwise)
    float      pinch_dist[4];   // thumb tip to index / middle / ring / little tip, meters
    float      pinch_strength;  // index pinch: 0 at 5 cm apart or more .. 1 touching (1.5 cm or less)
    SfxrButton pinch;           // index pinch: closes under 2 cm, opens over 3.5 cm
    SfxrButton middle_pinch;    // thumb to middle finger: a second "button" (same distances)
    float      grasp_strength;  // middle, ring and little finger curl: 0 open .. 1 closed
    SfxrButton grasp;           // hand closed: closes at 0.65, opens under 0.45
    Vector3    palm_normal;     // out of the palm (world, unit)
    bool       palm_up;         // palm to the sky: within 40 degrees, stays until past 55
    bool       palm_to_head;    // palm facing your eyes (a wrist-menu cue): within 40, until past 55
    SfxrPose   ray;             // a steady bare-hand laser: from an estimated shoulder through the
                                // index knuckle, -Z forward. It doesn't dip when you pinch, unlike
                                // a ray from the fingertips.
    Vector3    index_tip, thumb_tip;   // world
} SfxrHandGestures;

const SfxrHandGestures *sfxr_hand_gestures(SfxrHandId hand);   // never NULL; check ->valid

// ---------------------------------------------------------------------------
// Event log (SFXR_EVENTS=<file>): one line per thing that happened
// ---------------------------------------------------------------------------
//
// sfxr and vrui log what matters for reading a session afterwards: session
// state, worn / taken off, hands appearing or switching between controller
// and bare hand, grabs and releases (with the widget's label), presses,
// clicks, teleports, climbing. Headset launches write it next to each session
// recording; a failing test keeps its own (docs/TESTING.md). Add your app's
// moments the same way:
//
//   sfxr_event("level", "done in %.1f s", t);
//
// A line: seconds since start, frame number, kind, text.
void sfxr_event(const char *kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
bool sfxr_events_on(void);   // someone is listening (skip building expensive text otherwise)
// Tests listen in-process (sfxt's `expect grab ...`): every event, as it happens.
typedef void (*SfxrEventListener)(uint64_t frame, const char *kind, const char *text, void *user);
void sfxr_event_listen(SfxrEventListener fn, void *user);

// A screenshot of the next frame, as it's shown: in the headset both eyes
// side by side (what SFXR_SHOT saves), in the simulator the window. PNG;
// its folder is made if missing; the event log notes it ("screenshot").
// On the headset a relative path is in the app's folder: the toolbox's
// "Screenshot" menu item saves into shots/, and `scripts/frame.sh pull`
// brings them back.
void sfxr_screenshot(const char *path);

// App state a test can check (`expect app sky >= 0.9` in a scenario): report
// the values that say your app did the right thing, every frame. Cheap: a
// small table of named numbers.
void sfxr_report(const char *key, float value);
bool sfxr_reported(const char *key, float *value);

// ---------------------------------------------------------------------------
// Headset and system signals
// ---------------------------------------------------------------------------

// Worn or not (XR_EXT_user_presence). When the runtime can't tell, known is
// false and the headset counts as worn. Not worn usually also means the
// runtime stops asking for frames (sfxr_should_render() == false).
bool  sfxr_user_presence_known(void);
bool  sfxr_user_present(void);
// The app has the player's attention: false while the SteamVR dashboard (or
// another system overlay) is up, so pause and ignore input. Always true in
// the simulator and replays.
bool  sfxr_focused(void);

// Is the player here? The two ways a VR player leaves without quitting:
// taking the headset off, and opening the SteamVR dashboard. A game should
// PAUSE (freeze the simulation, stop the clock, sfxr_audio_pause), SAVE
// (sfxr_store_save: the system may close the app while they're away), and
// on return show a pause menu rather than throw them back in mid-fight. The
// event log notes every change ("attention"). The simulator fakes both: F2
// and F3. docs/INPUT.md, "Attention".
typedef enum { SFXR_HERE = 0, SFXR_AWAY_HEADSET_OFF, SFXR_AWAY_DASHBOARD } SfxrAttention;
SfxrAttention sfxr_attention(void);
const char   *sfxr_attention_name(SfxrAttention a);

// Saving: a small file of named values (settings, progress, best scores),
// next to the app (on the headset: in its folder, like shots/). Open it once
// at start; read with a default for keys it doesn't have yet; set; save.
// Saving writes a new file and then swaps it in, so a crash or a power-off
// mid-save leaves the last good one. Replays and tests never touch the disk:
// the store starts empty and saving does nothing.
//
//   sfxr_store_open("toolbox");                       // save/toolbox.cfg
//   bool fog = sfxr_store_int("fog", 1);
//   sfxr_store_set_int("fog", fog); sfxr_store_save();
bool        sfxr_store_open(const char *name);        // false: no file yet (fine: defaults)
int         sfxr_store_int(const char *key, int fallback);
float       sfxr_store_float(const char *key, float fallback);
const char *sfxr_store_str(const char *key, const char *fallback);
void        sfxr_store_set_int(const char *key, int value);
void        sfxr_store_set_float(const char *key, float value);
void        sfxr_store_set_str(const char *key, const char *value);
bool        sfxr_store_save(void);                    // false: couldn't write (or a replay/test)

// Display refresh rate (XR_FB_display_refresh_rate). The Frame offers
// 72/90/120/144 Hz. A request is applied by the runtime a few frames later;
// sfxr_refresh_rate() reports what's actually running.
float sfxr_refresh_rate(void);                  // 0 when unknown
int   sfxr_refresh_rates(float *out, int max);  // the choices, ascending
bool  sfxr_set_refresh_rate(float hz);

// How the rendered image is composited. ALPHA lets what's behind transparent
// pixels show through (on headsets with passthrough, the room): clear with
// alpha 0 and draw only what should be there.
typedef enum { SFXR_BLEND_OPAQUE = 0, SFXR_BLEND_ALPHA } SfxrBlendMode;
bool          sfxr_blend_supported(SfxrBlendMode m);
void          sfxr_set_blend_mode(SfxrBlendMode m);
SfxrBlendMode sfxr_blend_mode(void);

// Controller batteries (XR_EXT_interaction_profile_battery_state_display),
// refreshed about once a second.
typedef struct {
    bool  valid;       // the runtime reported a state
    float level;       // 0..1
    bool  charging, plugged_in, no_battery;
} SfxrBattery;
SfxrBattery sfxr_battery(SfxrHandId hand);

// Runtime performance counters (XR_META_performance_metrics): GPU/CPU frame
// times, utilization, ... Off until enabled (querying them has a small cost).
void  sfxr_perf_enable(bool on);
int   sfxr_perf_count(void);
const char *sfxr_perf_name(int i);              // e.g. "/perfmetrics_meta/app/gpu_frametime"
bool  sfxr_perf_value(int i, float *value, const char **unit);   // unit: "ms", "%", "Hz", "B", ""

// The runtime's own 3D model of each controller (XR_EXT_render_model +
// XR_EXT_interaction_render_model), loaded on first use. Returns NULL when
// unavailable (simulator, replay, other runtimes). Static meshes: buttons
// and triggers don't animate yet.
const Model *sfxr_controller_model(SfxrHandId hand, SfxrPose *pose_out);

// ---------------------------------------------------------------------------
// Debug drawing (call inside sfxr_draw_begin/end)
// ---------------------------------------------------------------------------

void sfxr_draw_controllers(void);            // simple controller boxes + aim rays
void sfxr_draw_floor_grid(int half_extent_m, Color major, Color minor);

// Simulator-only: draw the key help overlay (called automatically on the mirror
// window when F1 is toggled; exposed for custom HUDs).
void sfxr_sim_draw_help(int x, int y);

#ifdef __cplusplus
}
#endif

#endif // SFXR_H
