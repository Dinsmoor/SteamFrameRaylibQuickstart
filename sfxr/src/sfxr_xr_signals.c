// sfxr_xr_signals.c - the headset's other signals over OpenXR: presence,
// refresh rate, blend modes, hand joints, controller batteries, performance
// counters, and the runtime's controller models.

#include "sfxr_xr_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Headset signals: presence, refresh rate, blend mode, hand joints,
// batteries, performance counters, controller models
// ---------------------------------------------------------------------------

// Once the session runs: the refresh-rate list, hand trackers, counter names.
void sfxr_xr_signals_session_started(void)
{
    if (X.ext_refresh) {
        uint32_t n = 0;
        if (XR_SUCCEEDED(X.xrEnumerateDisplayRefreshRatesFB(X.session, 16, &n, X.rates))) X.n_rates = (int)n;
        float now = 0;
        if (X.xrGetDisplayRefreshRateFB && XR_SUCCEEDED(X.xrGetDisplayRefreshRateFB(X.session, &now))) S.sig.refresh_hz = now;
        SFXR_LOG("display refresh %.0f Hz (%d rates offered)", S.sig.refresh_hz, X.n_rates);
    }
    if (X.hand_tracking_supported && !X.tracker[0]) {
        for (int h = 0; h < 2; h++) {
            XrHandTrackerCreateInfoEXT ci = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
            ci.hand = h ? XR_HAND_RIGHT_EXT : XR_HAND_LEFT_EXT;
            ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            // ask for both kinds: bare hands seen by the cameras, and hands
            // inferred while holding a controller
            XrHandTrackingDataSourceEXT srcs[2] = { XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT,
                                                    XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT };
            XrHandTrackingDataSourceInfoEXT dsi = { XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT };
            dsi.requestedDataSourceCount = 2;
            dsi.requestedDataSources = srcs;
            if (X.ext_hand_source) ci.next = &dsi;
            if (!XR_CHECK(X.xrCreateHandTrackerEXT(X.session, &ci, &X.tracker[h]))) X.tracker[h] = XR_NULL_HANDLE;
        }
    }
    if (X.ext_perf && !X.n_perf && X.xrEnumeratePerformanceMetricsCounterPathsMETA) {
        uint32_t n = 0;
        if (XR_SUCCEEDED(X.xrEnumeratePerformanceMetricsCounterPathsMETA(X.instance, 32, &n, X.perf_path))) {
            X.n_perf = (int)n;
            for (int i = 0; i < X.n_perf; i++) {
                uint32_t len = 0;
                xrPathToString(X.instance, X.perf_path[i], sizeof X.perf_name[i], &len, X.perf_name[i]);
            }
        }
    }
}

void sfxr_xr_locate_hand_joints(bool focused)
{
    for (int h = 0; h < 2; h++) {
        SfxrHandJoints *o = &S.sig.joints[h];
        o->valid = false;
        if (!focused || !X.tracker[h]) continue;
        XrHandJointLocationEXT jl[XR_HAND_JOINT_COUNT_EXT];
        XrHandJointLocationsEXT locs = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
        locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locs.jointLocations = jl;
        XrHandTrackingDataSourceStateEXT dss = { XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT };
        if (X.ext_hand_source) locs.next = &dss;
        XrHandJointsLocateInfoEXT li = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
        li.baseSpace = X.app_space;
        li.time = X.frame_state.predictedDisplayTime;
        if (XR_FAILED(X.xrLocateHandJointsEXT(X.tracker[h], &li, &locs)) || !locs.isActive) continue;
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((jl[XR_HAND_JOINT_PALM_EXT].locationFlags & need) != need) continue;
        o->valid = true;
        if (X.ext_hand_source && dss.isActive)
            o->source = dss.dataSource == XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT ? SFXR_SOURCE_CONTROLLER : SFXR_SOURCE_HAND;
        else
            o->source = (SfxrInputSource)S.raw[h].source;
        for (int j = 0; j < SFXR_JOINT_COUNT; j++) {
            o->joint[j] = sfxr_xr_pose(jl[j].pose);
            o->radius[j] = jl[j].radius;
        }
    }
}

// About once a second: each controller's battery.
void sfxr_xr_poll_battery(void)
{
    if (!X.ext_battery || S.frame % 72 != 0) return;
    for (int h = 0; h < 2; h++) {
        XrBatteryStateDisplayEXT bs = { XR_TYPE_BATTERY_STATE_DISPLAY_EXT };
        XrInteractionProfileState ps = { XR_TYPE_INTERACTION_PROFILE_STATE };
        ps.next = &bs;
        SfxrBattery *b = &S.sig.battery[h];
        memset(b, 0, sizeof *b);
        if (XR_FAILED(xrGetCurrentInteractionProfile(X.session, X.hand_path[h], &ps))) continue;
        if (!(bs.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_VALID_BIT_EXT)) continue;
        b->valid = true;
        b->level = bs.batteryLevel;
        b->charging = (bs.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_CHARGING_BIT_EXT) != 0;
        b->plugged_in = (bs.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_PLUGGED_IN_BIT_EXT) != 0;
        b->no_battery = (bs.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_NO_BATTERY_BIT_EXT) != 0;
    }
}

int sfxr_xr_refresh_rates(float *out, int max)
{
    int n = X.n_rates < max ? X.n_rates : max;
    for (int i = 0; i < n; i++) out[i] = X.rates[i];
    return n;
}

bool sfxr_xr_set_refresh_rate(float hz)
{
    if (!X.ext_refresh || !X.running) return false;
    bool ok = XR_CHECK(X.xrRequestDisplayRefreshRateFB(X.session, hz));
    if (ok) SFXR_LOG("requested display refresh %.0f Hz", hz);
    return ok;
}

bool sfxr_xr_blend_supported(SfxrBlendMode m) { return m == SFXR_BLEND_OPAQUE || (m == SFXR_BLEND_ALPHA && X.blend_alpha); }
bool sfxr_xr_hand_joints_supported(void) { return X.hand_tracking_supported; }

void sfxr_xr_perf_enable(bool on)
{
    if (!X.ext_perf || !X.session || X.perf_on == on) return;
    XrPerformanceMetricsStateMETA st = { XR_TYPE_PERFORMANCE_METRICS_STATE_META };
    st.enabled = on;
    if (XR_CHECK(X.xrSetPerformanceMetricsStateMETA(X.session, &st))) X.perf_on = on;
}

int sfxr_xr_perf_count(void) { return X.perf_on ? X.n_perf : 0; }
const char *sfxr_xr_perf_name(int i) { return i >= 0 && i < X.n_perf ? X.perf_name[i] : ""; }

bool sfxr_xr_perf_value(int i, float *value, const char **unit)
{
    if (!X.perf_on || i < 0 || i >= X.n_perf) return false;
    XrPerformanceMetricsCounterMETA c = { XR_TYPE_PERFORMANCE_METRICS_COUNTER_META };
    if (XR_FAILED(X.xrQueryPerformanceMetricsCounterMETA(X.session, X.perf_path[i], &c))) return false;
    if (!(c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META)) return false;
    if (value) *value = (c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META) ? c.floatValue : (float)c.uintValue;
    if (unit) {
        switch (c.counterUnit) {
        case XR_PERFORMANCE_METRICS_COUNTER_UNIT_PERCENTAGE_META:   *unit = "%"; break;
        case XR_PERFORMANCE_METRICS_COUNTER_UNIT_MILLISECONDS_META: *unit = "ms"; break;
        case XR_PERFORMANCE_METRICS_COUNTER_UNIT_BYTES_META:        *unit = "B"; break;
        case XR_PERFORMANCE_METRICS_COUNTER_UNIT_HERTZ_META:        *unit = "Hz"; break;
        default:                                                     *unit = ""; break;
        }
    }
    return true;
}

// --- controller models ----------------------------------------------------------

void sfxr_xr_unload_render_models(void)
{
    for (int h = 0; h < 2; h++) {
        if (X.rm[h].loaded) UnloadModel(X.rm[h].mesh);
        if (X.rm[h].space) xrDestroySpace(X.rm[h].space);
        if (X.rm[h].model && X.xrDestroyRenderModelEXT) X.xrDestroyRenderModelEXT(X.rm[h].model);
        memset(&X.rm[h], 0, sizeof X.rm[h]);
    }
}

// Fetch one model's glTF from the runtime and load it with raylib (which
// loads models from files, so it goes through a temporary .glb).
static bool load_render_model_asset(XrRenderModelEXT model, Model *out)
{
    XrRenderModelPropertiesGetInfoEXT pgi = { XR_TYPE_RENDER_MODEL_PROPERTIES_GET_INFO_EXT };
    XrRenderModelPropertiesEXT props = { XR_TYPE_RENDER_MODEL_PROPERTIES_EXT };
    if (!XR_CHECK(X.xrGetRenderModelPropertiesEXT(model, &pgi, &props))) return false;
    XrRenderModelAssetCreateInfoEXT aci = { XR_TYPE_RENDER_MODEL_ASSET_CREATE_INFO_EXT };
    aci.cacheId = props.cacheId;
    XrRenderModelAssetEXT asset = XR_NULL_HANDLE;
    if (!XR_CHECK(X.xrCreateRenderModelAssetEXT(X.session, &aci, &asset))) return false;
    XrRenderModelAssetDataGetInfoEXT dgi = { XR_TYPE_RENDER_MODEL_ASSET_DATA_GET_INFO_EXT };
    XrRenderModelAssetDataEXT data = { XR_TYPE_RENDER_MODEL_ASSET_DATA_EXT };
    bool ok = false;
    if (XR_CHECK(X.xrGetRenderModelAssetDataEXT(asset, &dgi, &data)) && data.bufferCountOutput > 0) {
        data.bufferCapacityInput = data.bufferCountOutput;
        data.buffer = malloc(data.bufferCapacityInput);
        if (data.buffer && XR_CHECK(X.xrGetRenderModelAssetDataEXT(asset, &dgi, &data))) {
            const char *dir = getenv("XDG_RUNTIME_DIR");
            char path[512];
            snprintf(path, sizeof path, "%s/sfxr-controller-%p.glb", dir && *dir ? dir : "/tmp", (void *)model);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(data.buffer, 1, data.bufferCountOutput, f);
                fclose(f);
                *out = LoadModel(path);
                ok = out->meshCount > 0;
                remove(path);
                SFXR_LOG("controller model: %u bytes of glTF, %d meshes", data.bufferCountOutput, out->meshCount);
            }
        }
        free(data.buffer);
    }
    X.xrDestroyRenderModelAssetEXT(asset);
    return ok;
}

static void enumerate_render_models(void)
{
    XrInteractionRenderModelIdsEnumerateInfoEXT ei = { XR_TYPE_INTERACTION_RENDER_MODEL_IDS_ENUMERATE_INFO_EXT };
    XrRenderModelIdEXT ids[8];
    uint32_t n = 0;
    if (XR_FAILED(X.xrEnumerateInteractionRenderModelIdsEXT(X.session, &ei, 8, &n, ids)) || n == 0) return;
    for (uint32_t i = 0; i < n; i++) {
        XrRenderModelCreateInfoEXT ci = { XR_TYPE_RENDER_MODEL_CREATE_INFO_EXT };
        ci.renderModelId = ids[i];
        XrRenderModelEXT model = XR_NULL_HANDLE;
        if (!XR_CHECK(X.xrCreateRenderModelEXT(X.session, &ci, &model))) continue;
        // which hand is it for?
        XrInteractionRenderModelSubactionPathInfoEXT si = { XR_TYPE_INTERACTION_RENDER_MODEL_SUBACTION_PATH_INFO_EXT };
        XrPath paths[4];
        uint32_t np = 0;
        int hand = -1;
        if (XR_SUCCEEDED(X.xrEnumerateRenderModelSubactionPathsEXT(model, &si, 4, &np, paths)))
            for (uint32_t k = 0; k < np; k++) {
                if (paths[k] == X.hand_path[0]) hand = 0;
                if (paths[k] == X.hand_path[1]) hand = 1;
            }
        if (hand < 0 || X.rm[hand].model) { X.xrDestroyRenderModelEXT(model); continue; }
        X.rm[hand].model = model;
        XrRenderModelSpaceCreateInfoEXT sci = { XR_TYPE_RENDER_MODEL_SPACE_CREATE_INFO_EXT };
        sci.renderModel = model;
        XR_CHECK(X.xrCreateRenderModelSpaceEXT(X.session, &sci, &X.rm[hand].space));
        X.rm[hand].loaded = load_render_model_asset(model, &X.rm[hand].mesh);
    }
}

const Model *sfxr_xr_controller_model(SfxrHandId hand, SfxrPose *pose_stage)
{
    int h = hand == SFXR_RIGHT ? 1 : 0;
    if (!X.ext_render_model || !X.running) return NULL;
    if (!X.rm[0].model && !X.rm[1].model && GetTime() >= X.rm_next_try) {
        X.rm_next_try = GetTime() + 2.0;   // controllers may not be bound yet; retry now and then
        enumerate_render_models();
    }
    if (!X.rm[h].loaded || !X.rm[h].space) return NULL;
    if (!sfxr_xr_locate(X.rm[h].space, pose_stage, NULL, NULL, NULL)) return NULL;
    pose_stage->position.y += X.floor_fix;   // same floor as everything else (floor guard)
    return &X.rm[h].mesh;
}
