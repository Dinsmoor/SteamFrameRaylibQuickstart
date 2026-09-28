// sfxr_xr_internal.h - what the OpenXR files share:
//   sfxr_xr.c          instance, session, frame loop, events, backend vtables
//   sfxr_xr_input.c    actions and bindings (every Frame input), sampling, haptics
//   sfxr_xr_signals.c  presence, refresh rate, hand joints, batteries,
//                      performance counters, controller models
// Graphics-API specifics (GL / Vulkan swapchains) live behind SfxrXrGfx.

#ifndef SFXR_XR_INTERNAL_H
#define SFXR_XR_INTERNAL_H

#include "sfxr_internal.h"

#include <openxr/openxr.h>

#define S sfxr_state

typedef struct {
    const SfxrXrGfx *gfx;
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSpace app_space, view_space;
    XrSpace stage_space;                 // the room-setup floor, for the floor guard (may be null)
    float   floor_fix;                   // meters added to every tracked height (floor guard)
    bool    floor_fix_logged;
    XrSwapchain swapchain;
    int sc_width, sc_height;
    XrSessionState state;
    bool running, exit_requested, frame_began;
    XrFrameState frame_state;
    XrView views[2];
    uint32_t image_index;
    bool image_acquired;

    bool ext_frame_ctrl, ext_eye_gaze, ext_refresh, ext_local_floor;
    bool ext_hand_interaction, ext_palm_pose;
    bool ext_presence, ext_hand_tracking, ext_hand_source, ext_battery, ext_perf, ext_render_model;

    // headset signals
    bool  blend_alpha;                   // runtime offers ALPHA_BLEND
    float rates[16];
    int   n_rates;
    PFN_xrGetDisplayRefreshRateFB xrGetDisplayRefreshRateFB;

    // hand joints
    bool hand_tracking_supported;
    XrHandTrackerEXT tracker[2];
    PFN_xrCreateHandTrackerEXT  xrCreateHandTrackerEXT;
    PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT;
    PFN_xrLocateHandJointsEXT   xrLocateHandJointsEXT;

    // performance counters
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA xrEnumeratePerformanceMetricsCounterPathsMETA;
    PFN_xrSetPerformanceMetricsStateMETA              xrSetPerformanceMetricsStateMETA;
    PFN_xrQueryPerformanceMetricsCounterMETA          xrQueryPerformanceMetricsCounterMETA;
    XrPath perf_path[32];
    char   perf_name[32][96];
    int    n_perf;
    bool   perf_on;

    // controller render models
    struct {
        XrRenderModelEXT model;
        XrSpace space;
        Model mesh;
        bool loaded;
    } rm[2];
    double rm_next_try;                  // GetTime() of the next enumeration attempt
    PFN_xrEnumerateInteractionRenderModelIdsEXT xrEnumerateInteractionRenderModelIdsEXT;
    PFN_xrEnumerateRenderModelSubactionPathsEXT xrEnumerateRenderModelSubactionPathsEXT;
    PFN_xrCreateRenderModelEXT            xrCreateRenderModelEXT;
    PFN_xrDestroyRenderModelEXT           xrDestroyRenderModelEXT;
    PFN_xrGetRenderModelPropertiesEXT     xrGetRenderModelPropertiesEXT;
    PFN_xrCreateRenderModelSpaceEXT       xrCreateRenderModelSpaceEXT;
    PFN_xrCreateRenderModelAssetEXT       xrCreateRenderModelAssetEXT;
    PFN_xrDestroyRenderModelAssetEXT      xrDestroyRenderModelAssetEXT;
    PFN_xrGetRenderModelAssetDataEXT      xrGetRenderModelAssetDataEXT;
    bool eye_gaze_supported;
    PFN_xrRequestDisplayRefreshRateFB xrRequestDisplayRefreshRateFB;
    PFN_xrEnumerateDisplayRefreshRatesFB xrEnumerateDisplayRefreshRatesFB;

    XrActionSet action_set;
    XrPath hand_path[2];
    XrSpace grip_space[2], aim_space[2], poke_space[2], pinch_space[2], palm_space[2], gaze_space;
} SfxrXr;

extern SfxrXr sfxr_xr;
#define X sfxr_xr

// helpers (sfxr_xr.c)
bool     sfxr_xr_check(XrResult r, const char *what, const char *file, int line);
#define  XR_CHECK(call) sfxr_xr_check((call), #call, __FILE__, __LINE__)
XrPath   sfxr_xr_path(const char *s);
SfxrPose sfxr_xr_pose(XrPosef p);
// Locate a space at the predicted display time, in the app's space.
bool     sfxr_xr_locate(XrSpace space, SfxrPose *out, Vector3 *vel, Vector3 *angvel, bool *has_vel);

// sfxr_xr_input.c
bool sfxr_xr_create_actions(void);
void sfxr_xr_sample_input(void);
void sfxr_xr_update_profiles(void);

float sfxr_xr_floor_fix(void);   // floor guard correction in use (m)

// sfxr_xr_signals.c
void sfxr_xr_signals_session_started(void);
void sfxr_xr_locate_hand_joints(bool focused);
void sfxr_xr_poll_battery(void);
void sfxr_xr_unload_render_models(void);

#endif
