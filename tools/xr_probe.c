// xr_probe - print what the active OpenXR runtime supports.
//
// Run this FIRST on a new device (e.g. over SSH on the Steam Frame). It
// answers the questions that decide how you render:
//   * which graphics bindings exist (XR_KHR_opengl_enable? _vulkan_enable2?)
//   * Steam Frame specifics (XR_VALVE_frame_controller_interaction, eye gaze,
//     foveation, refresh-rate control)
//   * system name, recommended eye resolution, blend modes
// No graphics context is needed, so it runs headless.
//
//   ./xr_probe            summary
//   ./xr_probe -v         also list every extension with its version
//   ./xr_probe --paths    ask the runtime which controller input paths exist
//
// --paths: OpenXR has no "list the inputs" call, but a runtime must reject a
// suggested binding to a path its profile doesn't have. So we try every
// plausible path, one suggestion at a time, and print what was accepted. A
// deliberately bogus path is tried first as a control: if the runtime accepts
// that too, it doesn't validate and the result is meaningless.

#include <openxr/openxr.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *INTERESTING[][2] = {
    { "XR_KHR_opengl_enable",                  "OpenGL (GLX) swapchains -> sfxr backend 'gl'" },
    { "XR_KHR_opengl_es_enable",               "OpenGL ES swapchains (Android-style)" },
    { "XR_MNDX_egl_enable",                    "EGL binding (headless GL context)" },
    { "XR_KHR_vulkan_enable2",                 "Vulkan swapchains -> sfxr backend 'vk'" },
    { "XR_VALVE_frame_controller_interaction", "Steam Frame controller profile" },
    { "XR_EXT_eye_gaze_interaction",           "eye tracking" },
    { "XR_FB_foveation",                       "foveated rendering (Vulkan swapchain ext)" },
    { "XR_META_foveation_eye_tracked",         "eye-tracked foveation" },
    { "XR_FB_display_refresh_rate",            "request 72/90/120/144 Hz" },
    { "XR_EXT_local_floor",                    "floor-level local space" },
    { "XR_KHR_composition_layer_depth",        "depth submission (better reprojection)" },
    { "XR_EXT_hand_tracking",                  "articulated hand tracking" },
};

// --- --paths ---------------------------------------------------------------

enum { T_BOOL, T_FLOAT, T_VEC2, T_POSE, T_HAPTIC, T_COUNT };
static XrAction probe_act[T_COUNT];

static bool try_path(XrInstance inst, const char *profile, const char *full, int type)
{
    XrPath pp, bp;
    if (XR_FAILED(xrStringToPath(inst, profile, &pp)) || XR_FAILED(xrStringToPath(inst, full, &bp))) return false;
    XrActionSuggestedBinding sb = { probe_act[type], bp };
    XrInteractionProfileSuggestedBinding s = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    s.interactionProfile = pp;
    s.countSuggestedBindings = 1;
    s.suggestedBindings = &sb;
    return XR_SUCCEEDED(xrSuggestInteractionProfileBindings(inst, &s));
}

static void probe_profile(XrInstance inst, const char *profile)
{
    static const char *ids[] = {
        "trigger", "squeeze", "thumbstick", "trackpad", "joystick", "a", "b", "x", "y",
        "menu", "view", "system", "select", "home", "back", "start", "bumper", "shoulder",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right", "dpad", "thumbrest", "grip",
        "grip_button", "paddle", "paddle_left", "paddle_right", "l4", "l5", "r4", "r5",
        "pinch_ext", "poke_ext", "aim_activate_ext", "grasp_ext", "steam", "quick_access",
        "fingers", "index", "middle", "ring", "pinky", "finger_index", "finger_middle",
        "finger_ring", "finger_pinky", "thumb", "capsense", "palm", "rear",
    };
    static const struct { const char *c; int t; } comps[] = {
        { "click", T_BOOL }, { "touch", T_BOOL }, { "proximity", T_BOOL }, { "value", T_FLOAT },
        { "force", T_FLOAT }, { "curl", T_FLOAT }, { "x", T_FLOAT }, { "y", T_FLOAT },
        { "twist", T_FLOAT }, { "ready_ext", T_BOOL }, { NULL, T_VEC2 },
    };
    static const char *poses[] = { "grip", "aim", "palm_ext", "grip_surface", "pinch_ext", "poke_ext" };
    static const char *hands[] = { "left", "right" };
    char full[256];
    printf("== %s\n", profile);
    snprintf(full, sizeof full, "/user/hand/left/input/nonsense_zz/click");
    if (try_path(inst, profile, full, T_BOOL)) {
        printf("   runtime accepted a bogus path -- it doesn't validate bindings; results skipped\n");
        return;
    }
    for (int h = 0; h < 2; h++) {
        printf("   /user/hand/%s:", hands[h]);
        int n = 0;
        for (size_t i = 0; i < sizeof poses / sizeof poses[0]; i++) {
            snprintf(full, sizeof full, "/user/hand/%s/input/%s/pose", hands[h], poses[i]);
            if (try_path(inst, profile, full, T_POSE)) { printf("%s%s/pose", n++ % 6 ? "  " : "\n      ", poses[i]); }
        }
        for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++)
            for (size_t c = 0; c < sizeof comps / sizeof comps[0]; c++) {
                if (comps[c].c) snprintf(full, sizeof full, "/user/hand/%s/input/%s/%s", hands[h], ids[i], comps[c].c);
                else snprintf(full, sizeof full, "/user/hand/%s/input/%s", hands[h], ids[i]);
                if (try_path(inst, profile, full, comps[c].t))
                    printf("%s%s%s%s", n++ % 6 ? "  " : "\n      ", ids[i], comps[c].c ? "/" : "", comps[c].c ? comps[c].c : "");
            }
        snprintf(full, sizeof full, "/user/hand/%s/output/haptic", hands[h]);
        if (try_path(inst, profile, full, T_HAPTIC)) printf("%soutput/haptic", n++ % 6 ? "  " : "\n      ");
        printf("\n");
    }
}

static int probe_paths(XrInstance inst)
{
    XrActionSet set;
    XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(asci.actionSetName, "probe");
    strcpy(asci.localizedActionSetName, "Probe");
    if (XR_FAILED(xrCreateActionSet(inst, &asci, &set))) { printf("xrCreateActionSet failed\n"); return 1; }
    static const XrActionType types[T_COUNT] = {
        XR_ACTION_TYPE_BOOLEAN_INPUT, XR_ACTION_TYPE_FLOAT_INPUT, XR_ACTION_TYPE_VECTOR2F_INPUT,
        XR_ACTION_TYPE_POSE_INPUT, XR_ACTION_TYPE_VIBRATION_OUTPUT,
    };
    XrPath sub[2];
    xrStringToPath(inst, "/user/hand/left", &sub[0]);
    xrStringToPath(inst, "/user/hand/right", &sub[1]);
    for (int t = 0; t < T_COUNT; t++) {
        XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
        aci.actionType = types[t];
        snprintf(aci.actionName, sizeof aci.actionName, "probe%d", t);
        snprintf(aci.localizedActionName, sizeof aci.localizedActionName, "Probe %d", t);
        aci.countSubactionPaths = 2;
        aci.subactionPaths = sub;
        if (XR_FAILED(xrCreateAction(set, &aci, &probe_act[t]))) { printf("xrCreateAction failed\n"); return 1; }
    }
    probe_profile(inst, "/interaction_profiles/valve/frame_controller_valve");
    probe_profile(inst, "/interaction_profiles/ext/hand_interaction_ext");
    return 0;
}

// Extensions --paths enables when present (profiles and pose paths they add).
static const char *PATH_EXTS[] = {
    "XR_VALVE_frame_controller_interaction", "XR_EXT_hand_interaction", "XR_EXT_palm_pose",
    "XR_KHR_maintenance1",
};

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    int paths = argc > 1 && !strcmp(argv[1], "--paths");
    uint32_t n = 0;
    XrResult r = xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL);
    if (XR_FAILED(r)) {
        printf("No OpenXR runtime available (result %d).\n"
               "On the Frame: is SteamVR running? On a desktop: set XR_RUNTIME_JSON.\n", (int)r);
        return 1;
    }
    XrExtensionProperties props[256];
    if (n > 256) n = 256;
    for (uint32_t i = 0; i < n; i++) props[i] = (XrExtensionProperties){ XR_TYPE_EXTENSION_PROPERTIES };
    xrEnumerateInstanceExtensionProperties(NULL, n, &n, props);

    printf("== Extensions (%u)\n", n);
    for (size_t k = 0; k < sizeof INTERESTING / sizeof INTERESTING[0]; k++) {
        int found = 0;
        for (uint32_t i = 0; i < n; i++) if (!strcmp(props[i].extensionName, INTERESTING[k][0])) found = 1;
        printf("  [%s] %-40s %s\n", found ? "x" : " ", INTERESTING[k][0], INTERESTING[k][1]);
    }
    if (verbose)
        for (uint32_t i = 0; i < n; i++) printf("    %s v%u\n", props[i].extensionName, props[i].extensionVersion);

    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    const char *enable[8];
    uint32_t ne = 0;
    if (paths)
        for (size_t k = 0; k < sizeof PATH_EXTS / sizeof PATH_EXTS[0]; k++)
            for (uint32_t i = 0; i < n; i++)
                if (!strcmp(props[i].extensionName, PATH_EXTS[k])) enable[ne++] = PATH_EXTS[k];
    ci.enabledExtensionCount = ne;
    ci.enabledExtensionNames = enable;
    strcpy(ci.applicationInfo.applicationName, "xr_probe");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    XrInstance inst;
    if (XR_FAILED(r = xrCreateInstance(&ci, &inst))) { printf("xrCreateInstance failed: %d\n", (int)r); return 1; }

    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    xrGetInstanceProperties(inst, &ip);
    printf("== Runtime: %s %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
           XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sys;
    if (XR_FAILED(r = xrGetSystem(inst, &sgi, &sys))) {
        printf("No HMD system (result %d) -- headset asleep/disconnected?\n", (int)r);
        xrDestroyInstance(inst);
        return 1;
    }
    XrSystemProperties sp = { XR_TYPE_SYSTEM_PROPERTIES };
    xrGetSystemProperties(inst, sys, &sp);
    printf("== System: %s (vendor 0x%x)\n", sp.systemName, sp.vendorId);
    printf("   max swapchain %ux%u, max layers %u, orientation tracking %d, position tracking %d\n",
           sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight,
           sp.graphicsProperties.maxLayerCount, sp.trackingProperties.orientationTracking,
           sp.trackingProperties.positionTracking);

    XrViewConfigurationView v[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    uint32_t nv = 0;
    if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, v)))
        for (uint32_t i = 0; i < nv; i++)
            printf("   eye %u: recommended %ux%u (max %ux%u), samples %u\n", i,
                   v[i].recommendedImageRectWidth, v[i].recommendedImageRectHeight,
                   v[i].maxImageRectWidth, v[i].maxImageRectHeight, v[i].recommendedSwapchainSampleCount);

    XrEnvironmentBlendMode modes[8];
    uint32_t nm = 0;
    xrEnumerateEnvironmentBlendModes(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &nm, modes);
    printf("   blend modes:");
    for (uint32_t i = 0; i < nm; i++)
        printf(" %s", modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ? "opaque" :
                      modes[i] == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE ? "additive" : "alpha_blend");
    printf("\n");

    int rc = paths ? probe_paths(inst) : 0;
    xrDestroyInstance(inst);
    return rc;
}
