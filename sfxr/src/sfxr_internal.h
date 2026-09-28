// sfxr_internal.h - state shared between the sfxr core and its backends.
//
// Layering:
//   sfxr.c        core: config, rig, stereo matrices, MSAA target, mirror,
//                 button edge detection, public getters.
//   sfxr_xr.c     OpenXR instance/session/actions/frame loop (both XR backends)
//   sfxr_xr_gl.c  swapchain half for XR_KHR_opengl_enable
//   sfxr_xr_vk.c  swapchain half for XR_KHR_vulkan_enable2 (+ GL interop)
//   sfxr_sim.c    desktop simulator backend
//
// Backends report everything in TRACKING (stage) space; the core applies the
// rig to produce world-space values for the public API.

#ifndef SFXR_INTERNAL_H
#define SFXR_INTERNAL_H

#include "sfxr.h"
#include "sfxr_break.h"
#include <stdio.h>

#define BREAK SFXR_BREAK_DECLARE
#include "sfxr_breaks.def"
#undef BREAK

#define SFXR_LOG(...)  TraceLog(LOG_INFO,    "SFXR: " __VA_ARGS__)
#define SFXR_WARN(...) TraceLog(LOG_WARNING, "SFXR: " __VA_ARGS__)
#define SFXR_ERR(...)  TraceLog(LOG_ERROR,   "SFXR: " __VA_ARGS__)

// Raw per-hand input as sampled by a backend (tracking space, levels only;
// the core derives pressed/released edges and the pull-level buttons).
// This is also exactly what recordings store (sfxr_rec.h), so a replay feeds
// the app the same thing the headset did.
enum { RAW_POSE_GRIP = 1, RAW_POSE_AIM = 2, RAW_POSE_POKE = 4, RAW_POSE_PINCH = 8, RAW_POSE_PALM = 16 };

typedef struct {
    bool     active;
    uint8_t  source;          // SfxrInputSource
    uint8_t  pose_valid;      // RAW_POSE_* bits (missing poses are derived from grip/aim)
    uint8_t  pad;
    SfxrPose grip, aim, poke, pinch, palm;
    Vector3  velocity, angular_velocity;
    bool     has_velocity;
    float    trigger, squeeze;
    Vector2  stick;
    uint32_t click;           // bit (1 << SfxrControl): hardware click down
    uint32_t touch;           // bit (1 << SfxrControl): finger touching
    char     profile[128];
} SfxrRawHand;

#define RAW_BIT(c) (1u << (c))

// Headset / system signals as sampled by a backend. Recorded with every
// frame (sfxr_rec.h) so replays see the same presence, refresh rate,
// batteries and hand joints. Joints are in tracking (stage) space here.
typedef struct {
    uint8_t        presence_known, present, pad[2];
    float          refresh_hz;
    SfxrBattery    battery[2];
    SfxrHandJoints joints[2];
} SfxrSignals;

typedef struct {
    // Lifecycle
    bool (*init)(void);
    void (*shutdown)(void);
    // Waits for the frame, samples views + input into sfxr_state. false = quit.
    bool (*frame_begin)(void);
    // Returns the single-sample FBO (with depth) + its color texture to render
    // this frame's side-by-side image into. false = skip rendering.
    bool (*acquire)(unsigned *fbo, unsigned *tex);
    // Rendering into the acquired target is finished (still bound as GL state).
    void (*release)(void);
    // Submit (rendered = whether acquire/release happened this frame).
    void (*frame_end)(bool rendered);
    void (*haptic)(SfxrHandId hand, float amplitude, float seconds, float freq);
} SfxrBackendVtbl;

typedef struct {
    SfxrConfig cfg;
    SfxrBackend backend;
    const SfxrBackendVtbl *vt;
    bool initialized;
    bool window_open;
    bool verbose;

    char runtime_name[128];
    char system_name[128];

    // timing
    float    dt;
    uint64_t frame;
    double   last_time;

    // frame state (set by backend.frame_begin)
    bool should_render;
    bool in_draw;
    bool rendered_this_frame;

    // render size (per eye) and view data in TRACKING space
    int      eye_w, eye_h;
    SfxrPose eye_stage[2];
    float    fov[2][4];          // tan-free angles: left, right, up, down (radians)
    SfxrPose head_stage;
    bool     views_valid;

    // input (tracking space, from backend)
    SfxrRawHand raw[2];
    bool     gaze_valid;
    SfxrPose gaze_stage;

    SfxrSignals sig;
    SfxrHandShape shape_pending[2];   // hand-shape debounce (derived, not recorded)
    int shape_frames[2];

    // derived world-space input
    SfxrHand hands[2];
    SfxrPose gaze_world;
    SfxrHandJoints joints_world[2];
    SfxrHandGestures gestures[2];     // sfxr_hands.c
    Vector3 joint_hand_prev[2];       // joint-derived hands: last grip position (velocity)
    bool joint_hand_had_prev[2];
    SfxrBlendMode blend;

    // rig: world_from_stage
    Vector3 rig_pos;
    float   rig_yaw;

    // MSAA render target (owned by core); 0 when msaa_samples <= 1
    unsigned msaa_fbo, msaa_color_rb, msaa_depth_rb;
    int      msaa_samples;
    unsigned cur_fbo, cur_tex;   // backend target for this frame

    // mirror
    bool show_help;

    // replay / time
    bool   replay_stereo;
    double time;
} SfxrState;

extern SfxrState sfxr_state;

// Backends
extern const SfxrBackendVtbl sfxr_backend_xr_gl;
extern const SfxrBackendVtbl sfxr_backend_xr_vk;
extern const SfxrBackendVtbl sfxr_backend_sim;
extern const SfxrBackendVtbl sfxr_backend_replay;
extern const SfxrBackendVtbl sfxr_backend_script;   // sfxt.c

// Input recording (sfxr_replay.c)
void sfxr_record_start(void);
void sfxr_record_frame(void);
void sfxr_record_stop(void);

// Helpers shared by backends
SfxrPose sfxr_rig_pose(void);                   // world_from_stage as a pose
bool     sfxr_env_flag(const char *name, bool def);
// sfxr_input.c: raw hands + signals -> SfxrHand, shapes, world-space joints (each frame)
void     sfxr__derive_input(void);
float    sfxr__finger_curl(const SfxrHandJoints *j, int finger);   // 0 straight .. 1 curled (SFXR_FINGER_*)
// sfxr_hand_model.c: a procedural skeleton (tracking space) from a grip pose,
// finger curls (SFXR_FINGER_*) and a pinch amount 0..1. Tests and the simulator.
void     sfxr__hand_model(int hand, SfxrPose grip, const float curl[5], float pinch, SfxrHandJoints *out);
// sfxr_hands.c: gestures from the joints; joint-only hands get raw input (called first by derive_input)
void     sfxr__hands_update(void);
const char *sfxr_env_str(const char *name);

// XR-common entry points implemented in sfxr_xr.c; the GL/VK files plug in
// their graphics-specific pieces through this struct.
typedef struct {
    const char *name;                 // "gl" / "vk"
    const char *required_extension;   // XR_KHR_opengl_enable / XR_KHR_vulkan_enable2
    // Called after xrGetSystem; fills *binding (pointer to a graphics binding
    // struct valid until shutdown). false = backend unusable.
    bool (*create_binding)(void *xr_instance, uint64_t xr_system, const void **binding);
    // Pick swapchain format from the runtime's list (int64 formats).
    int64_t (*choose_format)(const int64_t *formats, uint32_t count);
    // Swapchain images were created: enumerate + wrap them.
    bool (*setup_images)(void *xr_swapchain, int width, int height);
    // Per-frame: image index acquired by the core -> fbo/tex to draw into
    bool (*image_target)(uint32_t index, unsigned *fbo, unsigned *tex);
    // After rendering into image `index` (VK path copies here)
    void (*image_rendered)(uint32_t index);
    void (*destroy)(void);
    uint64_t swapchain_usage;         // XrSwapchainUsageFlags
} SfxrXrGfx;

extern const SfxrXrGfx sfxr_xr_gfx_gl;
extern const SfxrXrGfx sfxr_xr_gfx_vk;

// Implemented in sfxr_xr.c, used by both XR vtables
bool sfxr_xr_init(const SfxrXrGfx *gfx);
void sfxr_xr_shutdown(void);
bool sfxr_xr_frame_begin(void);
bool sfxr_xr_acquire(unsigned *fbo, unsigned *tex);
void sfxr_xr_release(void);
void sfxr_xr_frame_end(bool rendered);
void sfxr_xr_haptic(SfxrHandId hand, float amplitude, float seconds, float freq);
const char *sfxr_xr_session_state(void);   // for logs
// Hardware signals only the XR backends have (sfxr_xr.c); callers check the backend.
int  sfxr_xr_refresh_rates(float *out, int max);
bool sfxr_xr_set_refresh_rate(float hz);
bool sfxr_xr_blend_supported(SfxrBlendMode m);
bool sfxr_xr_hand_joints_supported(void);
void sfxr_xr_perf_enable(bool on);
int  sfxr_xr_perf_count(void);
const char *sfxr_xr_perf_name(int i);
bool sfxr_xr_perf_value(int i, float *value, const char **unit);
const Model *sfxr_xr_controller_model(SfxrHandId hand, SfxrPose *pose_stage);

// Creates an FBO around `tex` with the given depth renderbuffer attached.
unsigned sfxr_make_fbo(unsigned tex, unsigned depth_rb);

#endif // SFXR_INTERNAL_H
