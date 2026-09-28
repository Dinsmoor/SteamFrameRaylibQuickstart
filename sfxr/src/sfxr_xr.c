// sfxr_xr.c - the OpenXR instance, session and frame loop shared by the GL
// and Vulkan backends (see sfxr_xr_internal.h for how the OpenXR code is split).

#include "sfxr_xr_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

SfxrXr sfxr_xr;

bool sfxr_xr_focused(void) { return X.state == XR_SESSION_STATE_FOCUSED; }

const char *sfxr_xr_session_state(void)
{
    switch (X.state) {
    case XR_SESSION_STATE_IDLE:         return "IDLE";
    case XR_SESSION_STATE_READY:        return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:      return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED:      return "FOCUSED";
    case XR_SESSION_STATE_STOPPING:     return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:      return "EXITING";
    default:                            return "UNKNOWN";
    }
}

bool sfxr_xr_check(XrResult r, const char *what, const char *file, int line)
{
    if (XR_SUCCEEDED(r)) return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (X.instance) xrResultToString(X.instance, r, buf);
    else snprintf(buf, sizeof(buf), "%d", (int)r);
    SFXR_WARN("%s failed: %s (%s:%d)", what, buf, file, line);
    return false;
}

XrPath sfxr_xr_path(const char *s)
{
    XrPath p = XR_NULL_PATH;
    xrStringToPath(X.instance, s, &p);
    return p;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

SfxrPose sfxr_xr_pose(XrPosef p)
{
    SfxrPose r;
    r.position = (Vector3){ p.position.x, p.position.y, p.position.z };
    r.orientation = (Quaternion){ p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w };
    return r;
}

bool sfxr_xr_locate(XrSpace space, SfxrPose *out, Vector3 *vel, Vector3 *angvel, bool *has_vel)
{
    XrSpaceVelocity v = { XR_TYPE_SPACE_VELOCITY };
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    loc.next = vel ? &v : NULL;
    if (XR_FAILED(xrLocateSpace(space, X.app_space, X.frame_state.predictedDisplayTime, &loc))) return false;
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return false;
    *out = sfxr_xr_pose(loc.pose);
    if (vel) {
        bool lv = (v.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
        bool av = (v.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
        *vel = lv ? (Vector3){ v.linearVelocity.x, v.linearVelocity.y, v.linearVelocity.z } : (Vector3){0};
        *angvel = av ? (Vector3){ v.angularVelocity.x, v.angularVelocity.y, v.angularVelocity.z } : (Vector3){0};
        *has_vel = lv;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Instance / session
// ---------------------------------------------------------------------------

static bool has_ext(const XrExtensionProperties *props, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(props[i].extensionName, name)) return true;
    return false;
}

static void request_refresh_rate(void)
{
    if (!X.ext_refresh || S.cfg.preferred_refresh_hz <= 0.0f) return;
    float rates[16];
    uint32_t n = 0;
    if (XR_FAILED(X.xrEnumerateDisplayRefreshRatesFB(X.session, 16, &n, rates))) return;
    float best = 0.0f;
    for (uint32_t i = 0; i < n; i++)
        if (fabsf(rates[i] - S.cfg.preferred_refresh_hz) < fabsf(best - S.cfg.preferred_refresh_hz)) best = rates[i];
    if (best > 0.0f && XR_SUCCEEDED(X.xrRequestDisplayRefreshRateFB(X.session, best)))
        SFXR_LOG("requested display refresh %.0f Hz", best);
}

static bool create_space(void)
{
    uint32_t n = 0;
    XrReferenceSpaceType types[16];
    xrEnumerateReferenceSpaces(X.session, 16, &n, types);
    bool has_stage = false, has_local_floor = false;
    for (uint32_t i = 0; i < n; i++) {
        if (types[i] == XR_REFERENCE_SPACE_TYPE_STAGE) has_stage = true;
        if (types[i] == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT) has_local_floor = true;
    }

    XrReferenceSpaceCreateInfo ci = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    ci.poseInReferenceSpace.orientation.w = 1.0f;
    const char *which;
    if (has_local_floor && X.ext_local_floor) {
        ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
        which = "LOCAL_FLOOR";
    } else if (has_stage) {
        ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        which = "STAGE";
    } else {
        // LOCAL's origin is the head at startup; drop it to an assumed floor.
        ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        ci.poseInReferenceSpace.position.y = -1.6f;
        which = "LOCAL (-1.6m)";
    }
    if (!XR_CHECK(xrCreateReferenceSpace(X.session, &ci, &X.app_space))) return false;
    SFXR_LOG("tracking space: %s%s", which, has_stage ? " (room-setup floor available)" : "");

    // The floor guard compares against the room-setup floor (see floor_guard()).
    const char *mode = sfxr_env_str("SFXR_FLOOR");
    if (has_stage && ci.referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE && !(mode && !strcmp(mode, "local"))) {
        XrReferenceSpaceCreateInfo si = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        si.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        si.poseInReferenceSpace.orientation.w = 1.0f;
        if (!XR_CHECK(xrCreateReferenceSpace(X.session, &si, &X.stage_space))) X.stage_space = XR_NULL_HANDLE;
    }

    ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    ci.poseInReferenceSpace = (XrPosef){ {0, 0, 0, 1}, {0, 0, 0} };
    return XR_CHECK(xrCreateReferenceSpace(X.session, &ci, &X.view_space));
}

// A depth swapchain the same size as the color one. Failing here only means
// no depth submission: the app keeps its private depth buffer.
static void create_depth_swapchain(const int64_t *formats, uint32_t n)
{
    int64_t fmt = X.gfx->choose_depth_format(formats, n);
    if (fmt == 0) { SFXR_LOG("depth submission: the runtime offers no usable depth format"); return; }
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.format = fmt;
    ci.sampleCount = 1;
    ci.width = (uint32_t)X.sc_width;
    ci.height = (uint32_t)X.sc_height;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!XR_CHECK(xrCreateSwapchain(X.session, &ci, &X.depth_swapchain))) { X.depth_swapchain = XR_NULL_HANDLE; return; }
    if (!X.gfx->setup_depth_images((void *)X.depth_swapchain)) {
        xrDestroySwapchain(X.depth_swapchain);
        X.depth_swapchain = XR_NULL_HANDLE;
        SFXR_LOG("depth submission: couldn't use the depth swapchain's images");
        return;
    }
    SFXR_LOG("depth submission on (format %lld): the compositor gets per-pixel depth", (long long)fmt);
}

static bool create_swapchain(void)
{
    uint32_t n = 0;
    xrEnumerateSwapchainFormats(X.session, 0, &n, NULL);
    if (n == 0) { SFXR_WARN("runtime offers no swapchain formats"); return false; }
    int64_t formats[64];
    if (n > 64) n = 64;
    xrEnumerateSwapchainFormats(X.session, n, &n, formats);
    int64_t fmt = X.gfx->choose_format(formats, n);
    if (fmt == 0) { SFXR_WARN("no usable swapchain format among %u", n); return false; }

    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | X.gfx->swapchain_usage;
    ci.format = fmt;
    ci.sampleCount = 1;
    ci.width = (uint32_t)(S.eye_w * 2);   // both eyes side by side
    ci.height = (uint32_t)S.eye_h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!XR_CHECK(xrCreateSwapchain(X.session, &ci, &X.swapchain))) return false;
    X.sc_width = (int)ci.width;
    X.sc_height = (int)ci.height;
    SFXR_LOG("swapchain %dx%d format %lld", X.sc_width, X.sc_height, (long long)fmt);
    if (!X.gfx->setup_images((void *)X.swapchain, X.sc_width, X.sc_height)) return false;
    if (X.ext_depth) create_depth_swapchain(formats, n);
    return true;
}

bool sfxr_xr_init(const SfxrXrGfx *gfx)
{
    memset(&X, 0, sizeof(X));
    X.gfx = gfx;

    uint32_t n = 0;
    XrResult r = xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL);
    if (XR_FAILED(r) || n == 0) {
        SFXR_WARN("no OpenXR runtime found (xrEnumerateInstanceExtensionProperties=%d)", (int)r);
        return false;
    }
    XrExtensionProperties props[256];
    if (n > 256) n = 256;
    for (uint32_t i = 0; i < n; i++) props[i] = (XrExtensionProperties){ XR_TYPE_EXTENSION_PROPERTIES };
    xrEnumerateInstanceExtensionProperties(NULL, n, &n, props);
    if (S.verbose) for (uint32_t i = 0; i < n; i++) SFXR_LOG("  runtime ext: %s v%u", props[i].extensionName, props[i].extensionVersion);

    if (!has_ext(props, n, gfx->required_extension)) {
        SFXR_WARN("runtime lacks %s", gfx->required_extension);
        return false;
    }
    const char *exts[24];
    uint32_t ne = 0;
    exts[ne++] = gfx->required_extension;
    if ((X.ext_frame_ctrl = has_ext(props, n, "XR_VALVE_frame_controller_interaction"))) exts[ne++] = "XR_VALVE_frame_controller_interaction";
    if ((X.ext_eye_gaze = has_ext(props, n, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME))) exts[ne++] = XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME;
    if ((X.ext_refresh = has_ext(props, n, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))) exts[ne++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    if ((X.ext_local_floor = has_ext(props, n, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME))) exts[ne++] = XR_EXT_LOCAL_FLOOR_EXTENSION_NAME;
    // Depth lets the compositor reproject (and synthesize) frames per pixel
    // instead of as a flat image at one distance. SFXR_DEPTH=0 turns it off.
    if (gfx->choose_depth_format && sfxr_env_flag("SFXR_DEPTH", true) &&
        (X.ext_depth = has_ext(props, n, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME)))
        exts[ne++] = XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME;
    if ((X.ext_hand_interaction = has_ext(props, n, "XR_EXT_hand_interaction"))) exts[ne++] = "XR_EXT_hand_interaction";
    if ((X.ext_palm_pose = has_ext(props, n, "XR_EXT_palm_pose"))) exts[ne++] = "XR_EXT_palm_pose";
    if ((X.ext_presence = has_ext(props, n, XR_EXT_USER_PRESENCE_EXTENSION_NAME))) exts[ne++] = XR_EXT_USER_PRESENCE_EXTENSION_NAME;
    if ((X.ext_hand_tracking = has_ext(props, n, XR_EXT_HAND_TRACKING_EXTENSION_NAME))) exts[ne++] = XR_EXT_HAND_TRACKING_EXTENSION_NAME;
    if (X.ext_hand_tracking && (X.ext_hand_source = has_ext(props, n, XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME)))
        exts[ne++] = XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME;
    if ((X.ext_battery = has_ext(props, n, XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME)))
        exts[ne++] = XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME;
    if ((X.ext_perf = has_ext(props, n, XR_META_PERFORMANCE_METRICS_EXTENSION_NAME))) exts[ne++] = XR_META_PERFORMANCE_METRICS_EXTENSION_NAME;
    // controller models need the render-model pair plus the UUID type they use
    if (has_ext(props, n, XR_EXT_RENDER_MODEL_EXTENSION_NAME) && has_ext(props, n, XR_EXT_INTERACTION_RENDER_MODEL_EXTENSION_NAME) &&
        has_ext(props, n, XR_EXT_UUID_EXTENSION_NAME)) {
        X.ext_render_model = true;
        exts[ne++] = XR_EXT_RENDER_MODEL_EXTENSION_NAME;
        exts[ne++] = XR_EXT_INTERACTION_RENDER_MODEL_EXTENSION_NAME;
        exts[ne++] = XR_EXT_UUID_EXTENSION_NAME;
    }

    XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
    snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", S.cfg.app_name);
    snprintf(ici.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "raylib+sfxr");
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = ne;
    ici.enabledExtensionNames = exts;
    if (!XR_CHECK(xrCreateInstance(&ici, &X.instance))) { X.instance = XR_NULL_HANDLE; return false; }

    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    xrGetInstanceProperties(X.instance, &ip);
    snprintf(S.runtime_name, sizeof(S.runtime_name), "%.100s %u.%u.%u", ip.runtimeName,
             XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    SFXR_LOG("runtime: %s", S.runtime_name);

    if (X.ext_refresh) {
        xrGetInstanceProcAddr(X.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&X.xrRequestDisplayRefreshRateFB);
        xrGetInstanceProcAddr(X.instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)&X.xrEnumerateDisplayRefreshRatesFB);
        xrGetInstanceProcAddr(X.instance, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction *)&X.xrGetDisplayRefreshRateFB);
        if (!X.xrRequestDisplayRefreshRateFB || !X.xrEnumerateDisplayRefreshRatesFB) X.ext_refresh = false;
    }
    #define LOAD(fn) xrGetInstanceProcAddr(X.instance, #fn, (PFN_xrVoidFunction *)&X.fn)
    if (X.ext_hand_tracking) {
        LOAD(xrCreateHandTrackerEXT); LOAD(xrDestroyHandTrackerEXT); LOAD(xrLocateHandJointsEXT);
        if (!X.xrCreateHandTrackerEXT || !X.xrLocateHandJointsEXT) X.ext_hand_tracking = false;
    }
    if (X.ext_perf) {
        LOAD(xrEnumeratePerformanceMetricsCounterPathsMETA); LOAD(xrSetPerformanceMetricsStateMETA);
        LOAD(xrQueryPerformanceMetricsCounterMETA);
        if (!X.xrQueryPerformanceMetricsCounterMETA || !X.xrSetPerformanceMetricsStateMETA) X.ext_perf = false;
    }
    if (X.ext_render_model) {
        LOAD(xrEnumerateInteractionRenderModelIdsEXT); LOAD(xrEnumerateRenderModelSubactionPathsEXT);
        LOAD(xrCreateRenderModelEXT); LOAD(xrDestroyRenderModelEXT); LOAD(xrGetRenderModelPropertiesEXT);
        LOAD(xrCreateRenderModelSpaceEXT); LOAD(xrCreateRenderModelAssetEXT); LOAD(xrDestroyRenderModelAssetEXT);
        LOAD(xrGetRenderModelAssetDataEXT);
        if (!X.xrEnumerateInteractionRenderModelIdsEXT || !X.xrGetRenderModelAssetDataEXT) X.ext_render_model = false;
    }
    #undef LOAD

    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XR_CHECK(xrGetSystem(X.instance, &sgi, &X.system))) goto fail;

    XrSystemEyeGazeInteractionPropertiesEXT egp = { XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
    XrSystemUserPresencePropertiesEXT upp = { XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT };
    XrSystemHandTrackingPropertiesEXT htp = { XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
    XrSystemProperties sp = { XR_TYPE_SYSTEM_PROPERTIES };
    void **chain = &sp.next;   // append each extension's properties struct
    if (X.ext_eye_gaze) { *chain = &egp; chain = &egp.next; }
    if (X.ext_presence) { *chain = &upp; chain = &upp.next; }
    if (X.ext_hand_tracking) { *chain = &htp; chain = &htp.next; }
    xrGetSystemProperties(X.instance, X.system, &sp);
    if (X.ext_presence && !upp.supportsUserPresence) X.ext_presence = false;
    X.hand_tracking_supported = X.ext_hand_tracking && htp.supportsHandTracking;

    XrEnvironmentBlendMode modes[8];
    uint32_t nm = 0;
    xrEnumerateEnvironmentBlendModes(X.instance, X.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &nm, modes);
    for (uint32_t i = 0; i < nm; i++) if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) X.blend_alpha = true;
    SFXR_LOG("signals: presence %s, hand joints %s, battery %s, perf counters %s, controller models %s, alpha blend %s",
             X.ext_presence ? "yes" : "no", X.hand_tracking_supported ? "yes" : "no", X.ext_battery ? "yes" : "no",
             X.ext_perf ? "yes" : "no", X.ext_render_model ? "yes" : "no", X.blend_alpha ? "yes" : "no");
    snprintf(S.system_name, sizeof(S.system_name), "%.127s", sp.systemName);
    X.eye_gaze_supported = X.ext_eye_gaze && egp.supportsEyeGazeInteraction;
    SFXR_LOG("system: %s (eye gaze: %s, frame controller ext: %s)", S.system_name,
             X.eye_gaze_supported ? "yes" : "no", X.ext_frame_ctrl ? "yes" : "no");

    XrViewConfigurationView vcv[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    uint32_t nv = 0;
    if (!XR_CHECK(xrEnumerateViewConfigurationViews(X.instance, X.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, vcv)) || nv != 2)
        goto fail;
    S.eye_w = (int)(vcv[0].recommendedImageRectWidth * S.cfg.resolution_scale);
    S.eye_h = (int)(vcv[0].recommendedImageRectHeight * S.cfg.resolution_scale);
    if (S.eye_w * 2 > (int)vcv[0].maxImageRectWidth * 2) S.eye_w = (int)vcv[0].maxImageRectWidth;
    if (S.eye_h > (int)vcv[0].maxImageRectHeight) S.eye_h = (int)vcv[0].maxImageRectHeight;
    SFXR_LOG("recommended eye size %ux%u -> using %dx%d", vcv[0].recommendedImageRectWidth,
             vcv[0].recommendedImageRectHeight, S.eye_w, S.eye_h);

    const void *binding = NULL;
    if (!gfx->create_binding((void *)X.instance, (uint64_t)X.system, &binding)) goto fail;

    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = binding;
    sci.systemId = X.system;
    if (!XR_CHECK(xrCreateSession(X.instance, &sci, &X.session))) goto fail;

    if (!create_space()) goto fail;
    if (!create_swapchain()) goto fail;
    if (!sfxr_xr_create_actions()) goto fail;

    for (int e = 0; e < 2; e++) {
        S.eye_stage[e] = sfxr_pose_identity();
        S.fov[e][0] = -0.8f; S.fov[e][1] = 0.8f; S.fov[e][2] = 0.8f; S.fov[e][3] = -0.8f;
    }
    X.state = XR_SESSION_STATE_UNKNOWN;
    return true;

fail:
    sfxr_xr_shutdown();
    return false;
}

void sfxr_xr_shutdown(void)
{
    sfxr_xr_unload_render_models();
    for (int h = 0; h < 2; h++) if (X.tracker[h] && X.xrDestroyHandTrackerEXT) X.xrDestroyHandTrackerEXT(X.tracker[h]);
    if (X.gfx && X.gfx->destroy) X.gfx->destroy();
    if (X.depth_swapchain) xrDestroySwapchain(X.depth_swapchain);
    if (X.swapchain) xrDestroySwapchain(X.swapchain);
    if (X.action_set) xrDestroyActionSet(X.action_set);
    if (X.app_space) xrDestroySpace(X.app_space);
    if (X.view_space) xrDestroySpace(X.view_space);
    if (X.stage_space) xrDestroySpace(X.stage_space);
    if (X.session) xrDestroySession(X.session);
    if (X.instance) xrDestroyInstance(X.instance);
    memset(&X, 0, sizeof(X));
}

static void poll_events(void)
{
    XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(X.instance, &ev) == XR_SUCCESS) {
        switch (ev.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const XrEventDataSessionStateChanged *e = (const XrEventDataSessionStateChanged *)&ev;
            X.state = e->state;
            SFXR_LOG("session state -> %s", sfxr_xr_session_state());
            if (X.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_CHECK(xrBeginSession(X.session, &bi))) {
                    X.running = true;
                    SFXR_LOG("session running");
                    request_refresh_rate();
                    sfxr_xr_signals_session_started();
                }
            } else if (X.state == XR_SESSION_STATE_STOPPING) {
                XR_CHECK(xrEndSession(X.session));
                X.running = false;
            } else if (X.state == XR_SESSION_STATE_EXITING || X.state == XR_SESSION_STATE_LOSS_PENDING) {
                X.exit_requested = true;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            X.exit_requested = true;
            break;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            sfxr_xr_update_profiles();
            break;
        case XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT: {
            const XrEventDataUserPresenceChangedEXT *e = (const XrEventDataUserPresenceChangedEXT *)&ev;
            S.sig.presence_known = 1;
            S.sig.present = e->isUserPresent ? 1 : 0;
            SFXR_LOG("headset %s", e->isUserPresent ? "put on" : "taken off");
            break;
        }
        case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
            const XrEventDataDisplayRefreshRateChangedFB *e = (const XrEventDataDisplayRefreshRateChangedFB *)&ev;
            S.sig.refresh_hz = e->toDisplayRefreshRate;
            SFXR_LOG("display refresh %.0f -> %.0f Hz", e->fromDisplayRefreshRate, e->toDisplayRefreshRate);
            break;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
            const XrEventDataReferenceSpaceChangePending *e = (const XrEventDataReferenceSpaceChangePending *)&ev;
            SFXR_LOG("tracking space change pending (space type %d, pose valid %d)", (int)e->referenceSpaceType, (int)e->poseValid);
            break;
        }
        case XR_TYPE_EVENT_DATA_INTERACTION_RENDER_MODELS_CHANGED_EXT:
            sfxr_xr_unload_render_models();
            X.rm_next_try = 0;
            break;
        default:
            break;
        }
        ev = (XrEventDataBuffer){ XR_TYPE_EVENT_DATA_BUFFER };
    }
}

// FLOOR GUARD. The Frame's LOCAL_FLOOR space starts from an estimate of the
// floor that can be far off for the first seconds after the headset goes on
// (seen: 64 cm too high for ~12 s, so the player was a child-height camera
// until SteamVR corrected it). The room-setup floor (STAGE) doesn't have that
// problem, so while the two disagree by more than 10 cm, every tracked height
// is corrected to the room-setup floor. When SteamVR fixes its estimate the
// disagreement -- and the correction -- vanish in the same frame, so nothing
// jumps. SFXR_FLOOR=local turns it off.
#define FLOOR_GUARD_M 0.10f
static void floor_guard(void)
{
    float fix = 0.0f;
    if (X.stage_space) {
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xrLocateSpace(X.app_space, X.stage_space, X.frame_state.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            float disagree = loc.pose.position.y;   // our floor's height above the room-setup floor
            if (fabsf(disagree) > FLOOR_GUARD_M) fix = disagree;
        }
    }
    if ((fix != 0.0f) != (X.floor_fix != 0.0f) || (fix != 0.0f && !X.floor_fix_logged)) {
        if (fix != 0.0f) SFXR_LOG("floor guard: tracking floor is %+.2f m off the room-setup floor; correcting", fix);
        else SFXR_LOG("floor guard: floors agree again (correction off)");
        X.floor_fix_logged = fix != 0.0f;
    }
    X.floor_fix = fix;
    if (fix == 0.0f) return;
    S.head_stage.position.y += fix;
    for (int e = 0; e < 2; e++) S.eye_stage[e].position.y += fix;
    if (S.gaze_valid) S.gaze_stage.position.y += fix;
    for (int h = 0; h < 2; h++) {
        SfxrRawHand *r = &S.raw[h];
        r->grip.position.y += fix; r->aim.position.y += fix; r->poke.position.y += fix;
        r->pinch.position.y += fix; r->palm.position.y += fix;
        SfxrHandJoints *j = &S.sig.joints[h];
        if (j->valid) for (int k = 0; k < SFXR_JOINT_COUNT; k++) j->joint[k].position.y += fix;
    }
}

float sfxr_xr_floor_fix(void) { return X.floor_fix; }

bool sfxr_xr_frame_begin(void)
{
    X.frame_began = false;
    poll_events();
    if (X.exit_requested) return false;

    if (!X.running) {
        S.should_render = false;
        for (int h = 0; h < 2; h++) S.raw[h].active = false;
        sleep_ms(10);
        return true;
    }

    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    X.frame_state = (XrFrameState){ XR_TYPE_FRAME_STATE };
    if (!XR_CHECK(xrWaitFrame(X.session, &wi, &X.frame_state))) return true;
    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    if (!XR_CHECK(xrBeginFrame(X.session, &bi))) return true;
    X.frame_began = true;

    float period = (float)((double)X.frame_state.predictedDisplayPeriod * 1e-9);
    if (period > 0.0f && period < 0.1f) S.dt = period;
    S.should_render = X.frame_state.shouldRender;

    XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = X.frame_state.predictedDisplayTime;
    vli.space = X.app_space;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    X.views[0] = (XrView){ XR_TYPE_VIEW };
    X.views[1] = (XrView){ XR_TYPE_VIEW };
    uint32_t nv = 0;
    S.views_valid = false;
    if (XR_SUCCEEDED(xrLocateViews(X.session, &vli, &vs, 2, &nv, X.views)) && nv == 2 &&
        (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        S.views_valid = true;
        for (int e = 0; e < 2; e++) {
            S.eye_stage[e] = sfxr_xr_pose(X.views[e].pose);
            S.fov[e][0] = X.views[e].fov.angleLeft;
            S.fov[e][1] = X.views[e].fov.angleRight;
            S.fov[e][2] = X.views[e].fov.angleUp;
            S.fov[e][3] = X.views[e].fov.angleDown;
        }
    }
    SfxrPose head;
    if (sfxr_xr_locate(X.view_space, &head, NULL, NULL, NULL)) S.head_stage = head;
    if (!S.views_valid) S.should_render = false;

    sfxr_xr_sample_input();
    floor_guard();
    return true;
}

bool sfxr_xr_acquire(unsigned *fbo, unsigned *tex)
{
    if (!X.frame_began || !S.should_render) return false;
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (!XR_CHECK(xrAcquireSwapchainImage(X.swapchain, &ai, &X.image_index))) return false;
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (!XR_CHECK(xrWaitSwapchainImage(X.swapchain, &wi))) return false;
    X.image_acquired = true;
    if (X.depth_swapchain) {
        if (XR_CHECK(xrAcquireSwapchainImage(X.depth_swapchain, &ai, &X.depth_index)) &&
            XR_CHECK(xrWaitSwapchainImage(X.depth_swapchain, &wi))) {
            X.depth_acquired = true;
            X.gfx->attach_depth(X.image_index, X.depth_index);
        }
    }
    return X.gfx->image_target(X.image_index, fbo, tex);
}

void sfxr_xr_release(void)
{
    if (!X.image_acquired) return;
    X.gfx->image_rendered(X.image_index);
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XR_CHECK(xrReleaseSwapchainImage(X.swapchain, &ri));
    X.image_acquired = false;
    if (X.depth_acquired) {
        XR_CHECK(xrReleaseSwapchainImage(X.depth_swapchain, &ri));
        X.depth_acquired = false;
    }
}

void sfxr_xr_frame_end(bool rendered)
{
    if (!X.frame_began) return;
    if (X.image_acquired) sfxr_xr_release();

    XrCompositionLayerProjectionView pv[2];
    XrCompositionLayerDepthInfoKHR depth[2];
    XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    const XrCompositionLayerBaseHeader *layers[1];
    uint32_t nl = 0;

    if (rendered && S.views_valid) {
        for (int e = 0; e < 2; e++) {
            pv[e] = (XrCompositionLayerProjectionView){ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            pv[e].pose = X.views[e].pose;
            pv[e].fov = X.views[e].fov;
            pv[e].subImage.swapchain = X.swapchain;
            pv[e].subImage.imageRect.offset.x = e * S.eye_w;
            pv[e].subImage.imageRect.offset.y = 0;
            pv[e].subImage.imageRect.extent.width = S.eye_w;
            pv[e].subImage.imageRect.extent.height = S.eye_h;
            pv[e].subImage.imageArrayIndex = 0;
            if (X.depth_swapchain) {
                // The same GL-style projection sfxr draws with: window depth 0..1 from near to far.
                depth[e] = (XrCompositionLayerDepthInfoKHR){ XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR };
                depth[e].subImage = pv[e].subImage;
                depth[e].subImage.swapchain = X.depth_swapchain;
                depth[e].minDepth = 0.0f;
                depth[e].maxDepth = 1.0f;
                depth[e].nearZ = S.cfg.near_clip;
                depth[e].farZ = S.cfg.far_clip;
                pv[e].next = &depth[e];
            }
        }
        layer.space = X.app_space;
        if (S.blend == SFXR_BLEND_ALPHA && X.blend_alpha) layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layer.viewCount = 2;
        layer.views = pv;
        layers[nl++] = (const XrCompositionLayerBaseHeader *)&layer;
    }

    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    ei.displayTime = X.frame_state.predictedDisplayTime;
    ei.environmentBlendMode = S.blend == SFXR_BLEND_ALPHA && X.blend_alpha ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
                                                                          : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = nl;
    ei.layers = layers;
    XR_CHECK(xrEndFrame(X.session, &ei));
    X.frame_began = false;
}


// ---------------------------------------------------------------------------
// Backend vtables (GL / VK differ only in the gfx plug-in)
// ---------------------------------------------------------------------------

static bool init_gl(void) { return sfxr_xr_init(&sfxr_xr_gfx_gl); }
static bool init_vk(void) { return sfxr_xr_init(&sfxr_xr_gfx_vk); }

const SfxrBackendVtbl sfxr_backend_xr_gl = {
    init_gl, sfxr_xr_shutdown, sfxr_xr_frame_begin, sfxr_xr_acquire,
    sfxr_xr_release, sfxr_xr_frame_end, sfxr_xr_haptic,
};
const SfxrBackendVtbl sfxr_backend_xr_vk = {
    init_vk, sfxr_xr_shutdown, sfxr_xr_frame_begin, sfxr_xr_acquire,
    sfxr_xr_release, sfxr_xr_frame_end, sfxr_xr_haptic,
};
